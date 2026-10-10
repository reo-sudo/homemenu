ifeq ($(strip $(DEVKITARM)),)
$(error "Set DEVKITARM first, e.g. export DEVKITARM=/opt/devkitpro/devkitARM")
endif

DEVKITPRO ?= /opt/devkitpro
TARGET    := homemenu
PREFIX    := $(DEVKITARM)/bin/arm-none-eabi-
CC        := $(PREFIX)gcc
LIBCTRU   := $(DEVKITPRO)/libctru
PORTLIBS  := $(DEVKITPRO)/portlibs/3ds
TOOLS     := $(DEVKITPRO)/tools/bin

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) -D__3DS__ \
           -I$(LIBCTRU)/include -I$(PORTLIBS)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(LIBCTRU)/lib -L$(PORTLIBS)/lib
LIBS    := -lcitro2d -lcitro3d -lctru -lm

SRC := $(wildcard source/*.c)
OBJ := $(SRC:.c=.o)

all: $(TARGET).3dsx

$(TARGET).3dsx: $(TARGET).elf
	$(TOOLS)/3dsxtool $< $@

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).3dsx

.PHONY: all clean
