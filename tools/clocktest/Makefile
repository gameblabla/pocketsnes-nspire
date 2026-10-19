# The CX II clock test (see clocktest.c):
#   . ~/nspire/build/env.sh && make -C tools/clocktest
# makes pocketsnes_clocktest.tns. The switch itself is PocketSNES's.
TARGET  = pocketsnes_clocktest
CC      = nspire-gcc
CFLAGS  = -Os -Wall -Wextra -marm -march=armv5te
ZEHNFLAGS = --name "PocketSNES clock test" --240x320-support true --ndless-min 31
SWITCH  = ../../frontend/clock_switch_nspire.S

all: $(TARGET).tns

$(TARGET).elf: clocktest.c $(SWITCH)
	$(CC) $(CFLAGS) clocktest.c $(SWITCH) -o $@

$(TARGET).tns: $(TARGET).elf
	genzehn --input $< --output $(TARGET).zehn $(ZEHNFLAGS)
	make-prg $(TARGET).zehn $@
	rm -f $(TARGET).zehn

clean:
	rm -f $(TARGET).elf $(TARGET).zehn $(TARGET).tns

.PHONY: all clean
