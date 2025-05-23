/*
 * sent_decoder.cpp
 *
 * SENT protocol decoder
 *
 * TODO support MAF sensors like 04E906051 see https://github.com/gerefi/gerefi-hardware/issues/146
 *
 * @date Oct 01, 2022
 * @author Andrey Gusakov <dron0gus@gmail.com>, (c) 2022-2024
 */

#include <stddef.h>
#include "sent_decoder.h"

/*==========================================================================*/
/* Misc helpers.															*/
/*==========================================================================*/
#define BIT(n) (UINT32_C(1) << (n))

/*==========================================================================*/
/* Protocol definitions.													*/
/*==========================================================================*/

/* Signals only */
#define SENT_MSG_DATA_SIZE      6
/* Status + two 12-bit signals + CRC: 8 pulses */
#define SENT_MSG_PAYLOAD_SIZE   (1 + SENT_MSG_DATA_SIZE + 1)  // Size of payload
/* Sync + Status + Signals + CRC: 9 pulses */
#define SENT_MSG_TOTAL			(1 + SENT_MSG_PAYLOAD_SIZE)

#define SENT_OFFSET_INTERVAL	12
#define SENT_SYNC_INTERVAL		(56 - SENT_OFFSET_INTERVAL) // 56 ticks - 12

#define SENT_MIN_INTERVAL		12
#define SENT_MAX_INTERVAL		15

#define SENT_CRC_SEED           0x05

/* use 3 full frames + one additional pulse for unit time calibration */
#define SENT_CALIBRATION_PULSES	(1 + 3 * SENT_MSG_PAYLOAD_SIZE)

/*==========================================================================*/
/* Decoder configuration													*/
/*==========================================================================*/

/*==========================================================================*/
/* Decoder																	*/
/*==========================================================================*/

/* Helpers for Msg manipulations */
/* nibbles order: status, sig0_MSN, sig0_MidN, sig0_LSN, sig1_MSN, sig1_MidN, sig1_LSN, CRC */
/* we shift rxReg left for 4 bits on each nibble received and put newest nibble
 * in [3:0] bits of rxReg, so when full message is received:
 * CRC is [3:0] - nibble 7
 * status is [31:28] - nibble 0
 * sig0 is [27:16], sig1 is [15:4] */
#define MsgGetNibble(msg, n)	(((msg) >> (4 * (7 - (n)))) & 0xf)
#define MsgGetStat(msg)			MsgGetNibble(msg, 0)
#define MsgGetSig0(msg)			(((msg) >> (4 * 4)) & 0xfff)
#define MsgGetSig1(msg)			(((msg) >> (1 * 4)) & 0xfff)
#define MsgGetCrc(msg)			MsgGetNibble(msg, 7)

/* convert CPU ticks to float Us */
#define TicksToUs(ticks)		((float)(ticks) * 1000.0 * 1000.0 / CORE_CLOCK)

void sent_channel::restart() {
	state = SENT_STATE_CALIB;
	pulseCounter = 0;
	currentStatePulseCounter = 0;
	pausePulseReceived = false;
	tickPerUnit = 0;

	/* reset slow channels */
	SlowChannelDecoderReset();

	#if SENT_STATISTIC_COUNTERS
		statistic.ShortIntervalErr = 0;
		statistic.LongIntervalErr = 0;
		statistic.SyncErr = 0;
		statistic.CrcErrCnt = 0;
		statistic.FrameCnt = 0;
		statistic.PauseCnt = 0;
		statistic.sc12 = 0;
		statistic.sc16 = 0;
		statistic.scCrcErr = 0;
		statistic.RestartCnt++;
	#endif
}

void sent_channel::calcTickPerUnit(uint32_t clocks) {
	/* int division with rounding */
	tickPerUnit =  (clocks + (SENT_SYNC_INTERVAL + SENT_OFFSET_INTERVAL) / 2) /
			(SENT_SYNC_INTERVAL + SENT_OFFSET_INTERVAL);
}

float sent_channel::getTickTime() {
	return tickPerUnit;
}

bool sent_channel::isSyncPulse(uint32_t clocks)
{
	/* check if pulse looks like sync with allowed +/-20% deviation */
	uint32_t syncClocks = (SENT_SYNC_INTERVAL + SENT_OFFSET_INTERVAL) * tickPerUnit;

	if (((100 * clocks) >= (syncClocks * 80)) &&
		((100 * clocks) <= (syncClocks * 120)))
		return 1;

	return 0;
}

int sent_channel::FastChannelDecoder(uint32_t clocks) {
	pulseCounter++;

	/* special case - tick time calculation */
	if (state == SENT_STATE_CALIB) {
		if ((tickPerUnit == 0) || (currentStatePulseCounter == 0)) {
			/* invalid or not yet calculated tickPerUnit */
			calcTickPerUnit(clocks);
			/* lets assume this is sync pulse... */
			currentStatePulseCounter = 1;
		} else {
			/* some tickPerUnit calculated...
			 * Check next 1 + 6 + 1 pulses if they are valid with current tickPerUnit */
			int checkInterval = (clocks + tickPerUnit / 2) / tickPerUnit - SENT_OFFSET_INTERVAL;
			if ((checkInterval >= 0) && (checkInterval <= SENT_MAX_INTERVAL)) {
				currentStatePulseCounter++;
				/* Should end up with CRC pulse */
				if (currentStatePulseCounter == (1 + SENT_MSG_PAYLOAD_SIZE)) {
					pulseCounter = 0;
					currentStatePulseCounter = 0;
					state = SENT_STATE_INIT;
				}
			} else {
				currentStatePulseCounter = 1;
				calcTickPerUnit(clocks);
			}
		}
		if (pulseCounter >= SENT_CALIBRATION_PULSES) {
			/* failed to calculate valid tickPerUnit, restart */
			restart();
		}
		return 0;
	}

	/* special case for out-of-sync state */
	if (state == SENT_STATE_INIT) {
		if (isSyncPulse(clocks)) {
			/* adjust unit time */
			calcTickPerUnit(clocks);
			/* we get here from calibration phase. calibration phase end with CRC nibble
			 * if we had to skip ONE pulse before we get sync - that means device may send pause
			 * pulse in between of messages */
			pausePulseReceived = false;
			if (currentStatePulseCounter == 1) {
				pausePulseReceived = true;
			}
			/* next state */
			currentStatePulseCounter = 0;
			state = SENT_STATE_STATUS;
		} else {
			currentStatePulseCounter++;
			/* 3 frames skipped, no SYNC detected - recalibrate */
			if (currentStatePulseCounter >= (SENT_MSG_TOTAL * 3)) {
				restart();
			}
		}
		/* done for this pulse */
		return 0;
	}

	int interval = (clocks + tickPerUnit / 2) / tickPerUnit - SENT_OFFSET_INTERVAL;

	if (interval < 0) {
		#if SENT_STATISTIC_COUNTERS
			statistic.ShortIntervalErr++;
		#endif //SENT_STATISTIC_COUNTERS
		state = SENT_STATE_INIT;
		return -1;
	}

	switch(state)
	{
		case SENT_STATE_CALIB:
		case SENT_STATE_INIT:
			/* handled above, should not get in here */
			return -1;

		case SENT_STATE_SYNC:
			if (isSyncPulse(clocks))
			{
				/* measured tick interval will be used until next sync pulse */
				calcTickPerUnit(clocks);
				rxReg = 0;
				state = SENT_STATE_STATUS;
			}
			else
			{
				if (pausePulseReceived) {
					#if SENT_STATISTIC_COUNTERS
						// Increment sync interval err count
						statistic.SyncErr++;
						if (interval > SENT_SYNC_INTERVAL)
						{
							statistic.LongIntervalErr++;
						}
						else
						{
							statistic.ShortIntervalErr++;
						}
					#endif // SENT_STATISTIC_COUNTERS
					/* wait for next sync and recalibrate tickPerUnit */
					state = SENT_STATE_INIT;
					return -1;
				} else {
					/* This is possibly pause pulse */
					/* TODO: check:
					 * Minimum Length 12 ticks (equivalent to a nibble with 0 value) - this is already checked
					 * Maximum Length 768 ticks (3 * 256) */
					#if SENT_STATISTIC_COUNTERS
						statistic.PauseCnt++;
					#endif // SENT_STATISTIC_COUNTERS
					pausePulseReceived = true;
				}
			}
			return 0;

		case SENT_STATE_STATUS:
			/* it is possible that pause pulse was threaded as sync and we are here with sync pulse */
			if ((pausePulseReceived == false) && isSyncPulse(clocks)) {
				#if SENT_STATISTIC_COUNTERS
					statistic.PauseCnt++;
				#endif // SENT_STATISTIC_COUNTERS
				/* measured tick interval will be used until next sync pulse */
				calcTickPerUnit(clocks);
				return 0;
			}
			// fallthrough
		case SENT_STATE_SIG1_DATA1:
		case SENT_STATE_SIG1_DATA2:
		case SENT_STATE_SIG1_DATA3:
		case SENT_STATE_SIG2_DATA1:
		case SENT_STATE_SIG2_DATA2:
		case SENT_STATE_SIG2_DATA3:
		case SENT_STATE_CRC:
			if (interval > SENT_MAX_INTERVAL)
			{
				#if SENT_STATISTIC_COUNTERS
					statistic.LongIntervalErr++;
				#endif

				state = SENT_STATE_INIT;
				return -1;
			}

			rxReg = (rxReg << 4) | (uint32_t)interval;

			if (state != SENT_STATE_CRC)
			{
				/* TODO: refactor */
				state = (SENT_STATE_enum)((int)state + 1);
				return 0;
			}

			#if SENT_STATISTIC_COUNTERS
				statistic.FrameCnt++;
			#endif // SENT_STATISTIC_COUNTERS
			pausePulseReceived = false;
			state = SENT_STATE_SYNC;
			/* CRC check */
			/* TODO: find correct way to calculate CRC */
			if ((MsgGetCrc(rxReg) == crc4(rxReg)) ||
				(MsgGetCrc(rxReg) == crc4_gm(rxReg)) ||
				(MsgGetCrc(rxReg) == crc4_gm_v2(rxReg)))
			{
				/* Full packet with correct CRC has been received */
				rxLast = rxReg;
				hasValidFast = true;
				/* TODO: add timestamp? */
				return 1;
			}
			else
			{
				#if SENT_STATISTIC_COUNTERS
					statistic.CrcErrCnt++;
				#endif // SENT_STATISTIC_COUNTERS
				return -1;
			}
			return 0;
	}

	return 0;
}

int sent_channel::Decoder(uint32_t clocks, uint8_t flags) {
	int ret;

	#if SENT_STATISTIC_COUNTERS
		if (flags & SENT_FLAG_HW_OVERFLOW) {
			statistic.hwOverflowCnt++;
		}
	#endif

	/* TODO: handle flags */
	(void)flags;

	ret = FastChannelDecoder(clocks);
	if (ret > 0) {
		/* valid packet received, can process slow channels */
		SlowChannelDecoder();
	} else if (ret < 0) {
		/* packet is incorrect, reset slow channel state machine */
		SlowChannelDecoderReset();
	}

	return ret;
}

int sent_channel::GetMsg(uint32_t* rx) {
	if (rx) {
		*rx = rxLast;
	}

    if (!hasValidFast) {
        return -1;
    }
	/* TODO: add check for time since last message received */
	return 0;
}

int sent_channel::GetSignals(uint8_t *pStat, uint16_t *pSig0, uint16_t *pSig1) {
	uint32_t rx;
	int ret = GetMsg(&rx);

	if (ret < 0) {
		return ret;
	}

	/* NOTE different MSB packing for sig0 and sig1
	 * is it protocol-defined or device-specific?
	 * Also looks like some devices send 16 + 8 bit, not 12 + 12 */
	if (pStat) {
		*pStat = MsgGetStat(rx);
	}

	if (pSig0) {
		uint16_t tmp = MsgGetSig0(rx);
		*pSig0 = tmp;
	}

	if (pSig1) {
		uint16_t tmp = MsgGetSig1(rx);
		/* swap */
		tmp = 	((tmp >> 8) & 0x00f) |
				((tmp << 8) & 0xf00) |
				(tmp & 0x0f0);
		*pSig1 = tmp;
	}

	return 0;
}

int sent_channel::StoreSlowChannelValue(uint8_t id, uint16_t data)
{
	size_t i;

	/* Update already allocated messagebox? */
	for (i = 0; i < SENT_SLOW_CHANNELS_MAX; i++) {
		if ((scMsg[i].valid) && (scMsg[i].id == id)) {
			scMsg[i].data = data;
			return 0;
		}
	}

	/* New message? Allocate messagebox */
	for (i = 0; i < SENT_SLOW_CHANNELS_MAX; i++) {
		if (scMsg[i].valid == false)
		 {
			scMsg[i].data = data;
			scMsg[i].id = id;
			scMsg[i].valid = true;
			return 0;
		}
	}

	/* No free mailboxes for new ID */
	return -1;
}

int sent_channel::GetSlowChannelValue(uint8_t id)
{
	size_t i;

	for (i = 0; i < SENT_SLOW_CHANNELS_MAX; i++) {
		if ((scMsg[i].valid) && (scMsg[i].id == id)) {
			return scMsg[i].data;
		}
	}

	/* not found */
	return -1;
}

int sent_channel::SlowChannelDecoder()
{
	/* bit 2 and bit 3 from status nibble are used to transfer short messages */
	bool b2 = !!(MsgGetStat(rxLast) & BIT(2));
	bool b3 = !!(MsgGetStat(rxLast) & BIT(3));

	/* shift in new data */
	scShift2 = (scShift2 << 1) | b2;
	scShift3 = (scShift3 << 1) | b3;
	scCrcShift = (scCrcShift << 2) | ((uint32_t)b2 << 1) | b3;

	if (1) {
		/* Short Serial Message format */

		/* 0b1000.0000.0000.0000? */
		if ((scShift3 & 0xffff) == 0x8000) {
			/* Done receiving */

			/* TODO: add crc check */

			uint8_t id = (scShift2 >> 12) & 0x0f;
			uint16_t data = (scShift2 >> 4) & 0xff;

			return StoreSlowChannelValue(id, data);
		}
	}
	if (1) {
		/* Enhanced Serial Message format */

		/* 0b11.1111.0xxx.xx0x.xxx0 ? */
		if ((scShift3 & 0x3f821) == 0x3f000) {
			/* C-flag: configuration bit is used to indicate 16 bit format */
			bool sc16Bit = !!(scShift3 & (1 << 10));

			uint8_t crc = (scShift2 >> 12) & 0x3f;
			#if SENT_STATISTIC_COUNTERS
				if (sc16Bit) {
					statistic.sc16++;
				} else {
					statistic.sc12++;
				}
			#endif
			if (crc == crc6(scCrcShift)) {
				if (!sc16Bit) {
					/* 12 bit message, 8 bit ID */
					uint8_t id = ((scShift3 >> 1) & 0x0f) |
						 ((scShift3 >> 2) & 0xf0);
					uint16_t data = scShift2 & 0x0fff; /* 12 bit */

					return StoreSlowChannelValue(id, data);
				} else {
					/* 16 bit message, 4 bit ID */
					uint8_t id = (scShift3 >> 6) & 0x0f;
					uint16_t data = (scShift2 & 0x0fff) |
						   (((scShift3 >> 1) & 0x0f) << 12);

					return StoreSlowChannelValue(id, data);
				}
			} else {
				#if SENT_STATISTIC_COUNTERS
					statistic.scCrcErr++;
				#endif
			}
		}
	}

	return 0;
}

void sent_channel::SlowChannelDecoderReset()
{
	/* packet is incorrect, reset slow channel state machine */
	scShift2 = 0;
	scShift3 = 0;

	for (size_t i = 0; i < SENT_SLOW_CHANNELS_MAX; i++) {
		scMsg[i].valid = false;
	}
}

/* This is correct for Si7215 */
/* This CRC is calculated for WHOLE message expect last nibble (CRC) */
uint8_t sent_channel::crc4(uint32_t data)
{
	size_t i;
	uint8_t crc = SENT_CRC_SEED; // initialize checksum with seed "0101"
	const uint8_t CrcLookup[16] = {0, 13, 7, 10, 14, 3, 9, 4, 1, 12, 6, 11, 15, 2, 8, 5};

	for (i = 0; i < 7; i++) {
		crc = crc ^ MsgGetNibble(data, i);
		crc = CrcLookup[crc];
	}

	return crc;
}

/* TODO: double check two following and use same CRC routine? */

/* This is correct for GM throttle body */
/* This CRC is calculated for message expect status nibble and minus CRC nibble */
uint8_t sent_channel::crc4_gm(uint32_t data)
{
	size_t i;
	uint8_t crc = SENT_CRC_SEED; // initialize checksum with seed "0101"
	const uint8_t CrcLookup[16] = {0, 13, 7, 10, 14, 3, 9, 4, 1, 12, 6, 11, 15, 2, 8, 5};

	for (i = 1; i < 7; i++) {
		crc = CrcLookup[crc];
		crc = (crc ^ MsgGetNibble(data, i)) & 0xf;
	}

	return crc;
}

/* This is correct for GDI fuel pressure sensor */
/* This CRC is calculated for message expect status nibble and minus CRC nibble */
uint8_t sent_channel::crc4_gm_v2(uint32_t data)
{
	size_t i;
	uint8_t crc = SENT_CRC_SEED; // initialize checksum with seed "0101"
	const uint8_t CrcLookup[16] = {0, 13, 7, 10, 14, 3, 9, 4, 1, 12, 6, 11, 15, 2, 8, 5};

	for (i = 1; i < 7; i++) {
		crc = CrcLookup[crc];
		crc = (crc ^ MsgGetNibble(data, i)) & 0xf;
	}
	// One more round with 0 as input
	crc = CrcLookup[crc];

	return crc;
}

uint8_t sent_channel::crc6(uint32_t data)
{
	size_t i;
	/* Seed 0x15 (21) */
	uint8_t crc = 0x15;
	/* CRC table for poly = 0x59 (x^6 + x^4 + x^3 + 1) */
	const uint8_t crc6_table[64] = {
		 0, 25, 50, 43, 61, 36, 15, 22, 35, 58, 17,  8, 30,  7, 44, 53,
		31,  6, 45, 52, 34, 59, 16,  9, 60, 37, 14, 23,  1, 24, 51, 42,
		62, 39, 12, 21,  3, 26, 49, 40, 29,  4, 47, 54, 32, 57, 18, 11,
		33, 56, 19, 10, 28,  5, 46, 55,  2, 27, 48, 41, 63, 38, 13, 20 };

	for (i = 0; i < 4; i++) {
		uint8_t tmp = (data >> (24 - 6 * (i + 1))) & 0x3f;
		crc = tmp ^ crc6_table[crc];
	}
	// Extra round with 0 input
	crc = 0 ^ crc6_table[crc];

	return crc;
}
