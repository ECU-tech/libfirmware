/*
 * sent_decoder.h
 *
 * SENT protocol decoder header
 *
 * @date Oct 01, 2022
 * @author Andrey Gusakov <dron0gus@gmail.com>, (c) 2022-2024
 */

#pragma once

#include <stdint.h>

/* Maximum slow shannel mailboxes */
#define SENT_SLOW_CHANNELS_MAX  32

/* collect statistic */
#define SENT_STATISTIC_COUNTERS	1

typedef enum
{
	SENT_STATE_CALIB = 0,
	SENT_STATE_INIT,
	SENT_STATE_SYNC,
	SENT_STATE_STATUS,
	SENT_STATE_SIG1_DATA1,
	SENT_STATE_SIG1_DATA2,
	SENT_STATE_SIG1_DATA3,
	SENT_STATE_SIG2_DATA1,
	SENT_STATE_SIG2_DATA2,
	SENT_STATE_SIG2_DATA3,
	SENT_STATE_CRC,
} SENT_STATE_enum;

struct sent_channel_stat {
	uint32_t hwOverflowCnt;

	uint32_t ShortIntervalErr;
	uint32_t LongIntervalErr;
	uint32_t SyncErr;
	uint32_t CrcErrCnt;
	uint32_t FrameCnt;
	uint32_t PauseCnt;
	uint32_t RestartCnt;

	/* Slow channel */
	uint32_t sc12;	//12-bit data, 8-bit message ID
	uint32_t sc16;	//16-bit data, 4-bit message ID
	uint32_t scCrcErr;

	uint32_t getTotalError() {
		return ShortIntervalErr + LongIntervalErr + SyncErr + CrcErrCnt;
	}

	float getErrorRate() {
		return 1.0 * getTotalError() / (FrameCnt + getTotalError());
	}
};

#define SENT_FLAG_HW_OVERFLOW	(1 << 0)

class sent_channel {
private:
	SENT_STATE_enum state = SENT_STATE_CALIB;

	/* Unit interval in timer clocks - adjusted on SYNC */
	uint32_t tickPerUnit = 0;
	uint32_t pulseCounter = 0;
	/* pulses skipped in init or calibration state while waiting for SYNC */
	uint32_t currentStatePulseCounter = 0;
	bool pausePulseReceived = false;

	/* fast channel shift register*/
	uint32_t rxReg;
	bool hasValidFast = false;
	/* fast channel last received valid message */
	uint32_t rxLast;

	/* slow channel shift registers */
	uint32_t scShift2;	/* shift register for bit 2 from status nibble */
	uint32_t scShift3;	/* shift register for bit 3 from status nibble */
	uint32_t scCrcShift;	/* shift register for special order for CRC6 calculation */
	/* Slow channel decoder and helpers */
	int StoreSlowChannelValue(uint8_t id, uint16_t data);
	int FastChannelDecoder(uint32_t clocks);
	int SlowChannelDecoder();
	void SlowChannelDecoderReset();

	/* CRC */
	uint8_t crc4(uint32_t data);
	uint8_t crc4_gm(uint32_t data);
	uint8_t crc4_gm_v2(uint32_t data);
	/* Slow channel CRC6 */
	uint8_t crc6(uint32_t data);

	/* calc unit tick time from sync pulse */
	void calcTickPerUnit(uint32_t clocks);

	/* check if current pulse looks like sync pulse */
	bool isSyncPulse(uint32_t clocks);

	void restart();

public:
	/* slow channel data */
	struct {
		uint16_t data;
		uint8_t id;
		bool valid;
	} scMsg[SENT_SLOW_CHANNELS_MAX];

	/* Statistic counters */
#if SENT_STATISTIC_COUNTERS
	sent_channel_stat statistic;
#endif // SENT_STATISTIC_COUNTERS

	/* Decoder */
	int Decoder(uint32_t clocks, uint8_t flags = 0);

	/* Get last raw message */
	int GetMsg(uint32_t* rx);
	/* Unpack last valid message to status, signal0 and signal1
	 * Note:
	 * sig0 is nibbles 0 .. 2, where nibble 0 is MSB
	 * sig1 is nibbles 5 .. 3, where nibble 5 is MSB */
	int GetSignals(uint8_t *pStat, uint16_t *pSig0, uint16_t *pSig1);

	/* Get slow channel value for given ID 8*/
	int GetSlowChannelValue(uint8_t id);

	/* Current tick time in CPU/timer clocks */
	float getTickTime();

	/* Show status */
	void Info();
};
