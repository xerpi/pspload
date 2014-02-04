TARGET_LIB = libpspload.a
OBJS = pspload.o

CFLAGS = -G0 -Wall -O2
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
PSP_FW_VERSION=371

PSPSDK=$(shell psp-config --pspsdk-path)
PSPDEV=$(shell psp-config --pspdev-path)
include $(PSPSDK)/lib/build.mak

all:
	@echo "Type 'make install' to install the library!"

install: $(TARGET_LIB)
	cp $(TARGET_LIB) $(PSPDEV)/psp/lib
	cp pspload.h $(PSPDEV)/psp/include
	@echo "libpspload installed!"
	
