GEREFI_LIB_INC += $(GEREFI_LIB)/util/include $(GEREFI_LIB)/util/include/gerefi/containers $(GEREFI_LIB)/can $(GEREFI_LIB)/board_id

GEREFI_LIB_CPP += \
	$(GEREFI_LIB)/util/src/util_dummy.cpp \
	$(GEREFI_LIB)/util/src/crc.cpp \
	$(GEREFI_LIB)/util/src/efistringutil.cpp \
	$(GEREFI_LIB)/util/src/fragments.cpp \
	$(GEREFI_LIB)/util/src/math.cpp \

GEREFI_LIB_CPP_TEST += \
	$(GEREFI_LIB)/util/test/test_arrays.cpp \
	$(GEREFI_LIB)/util/test/test_crc.cpp \
	$(GEREFI_LIB)/util/test/test_cyclic_buffer.cpp \
	$(GEREFI_LIB)/util/test/test_efistringutil.cpp \
	$(GEREFI_LIB)/util/test/test_fragments.cpp \
	$(GEREFI_LIB)/util/test/test_interpolation.cpp \
	$(GEREFI_LIB)/util/test/test_scaled.cpp \
	$(GEREFI_LIB)/util/test/test_manifest.cpp \
	$(GEREFI_LIB)/util/test/test_wraparound.cpp \
	$(GEREFI_LIB)/util/test/test_math.cpp \

