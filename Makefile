CELL_MK_DIR = $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = main.c
PPU_TARGET = EBOOT.BIN
PPU_PRX_LDFLAGS += -lnet -lsysutil -lsysmodule

include $(CELL_MK_DIR)/sdk.target.mk