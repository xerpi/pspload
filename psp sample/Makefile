TARGET = pspload_sample
OBJS = pspload_sample.o

INCDIR =
CFLAGS = -G0 -Wall -O2
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS =
LIBS= -lpspload -lpspsystemctrl_user -lpspgu -lpspnet -lpspdisplay -lpspgum -lz -lm

BUILD_PRX=1 
PSP_FW_VERSION=371

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = pspload sample by xerpi

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak


copy: EBOOT.PBP
	mkdir -p "/media/$(USER)/disk/PSP/GAME/$(notdir $(CURDIR))"
	cp EBOOT.PBP "/media/$(USER)/disk/PSP/GAME/$(notdir $(CURDIR))"
	sync
