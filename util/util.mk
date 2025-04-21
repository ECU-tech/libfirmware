ECU-TECH_LIB_INC += $(ECU-TECH_LIB)/util/include $(ECU-TECH_LIB)/util/include/ecu-tech/containers $(ECU-TECH_LIB)/can $(ECU-TECH_LIB)/board_id

ECU-TECH_LIB_CPP += \
	$(ECU-TECH_LIB)/util/src/util_dummy.cpp \
	$(ECU-TECH_LIB)/util/src/crc.cpp \
	$(ECU-TECH_LIB)/util/src/efistringutil.cpp \
	$(ECU-TECH_LIB)/util/src/fragments.cpp \
	$(ECU-TECH_LIB)/util/src/math.cpp \

ECU-TECH_LIB_CPP_TEST += \
	$(ECU-TECH_LIB)/util/test/test_arrays.cpp \
	$(ECU-TECH_LIB)/util/test/test_crc.cpp \
	$(ECU-TECH_LIB)/util/test/test_cyclic_buffer.cpp \
	$(ECU-TECH_LIB)/util/test/test_efistringutil.cpp \
	$(ECU-TECH_LIB)/util/test/test_fragments.cpp \
	$(ECU-TECH_LIB)/util/test/test_interpolation.cpp \
	$(ECU-TECH_LIB)/util/test/test_scaled.cpp \
	$(ECU-TECH_LIB)/util/test/test_manifest.cpp \
	$(ECU-TECH_LIB)/util/test/test_wraparound.cpp \
	$(ECU-TECH_LIB)/util/test/test_math.cpp \

