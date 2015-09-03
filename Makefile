
# Define the applications properties here:

TARGET = PocketSNES

CC  := gcc
CXX := g++
STRIP := strip

SYSROOT := $(shell $(CC) --print-sysroot)
SDL_CFLAGS := $(shell $(SYSROOT)/usr/bin/sdl-config --cflags)
SDL_LIBS := $(shell $(SYSROOT)/usr/bin/sdl-config --libs)

ifdef V
	CMD:=
	SUM:=@\#
else
	CMD:=@
	SUM:=@echo
endif

INCLUDE = -I pocketsnes \
		-I sal/linux/include -I sal/include \
		-I pocketsnes/include \
		-I menu -I pocketsnes/linux -I pocketsnes/snes9x

CFLAGS = $(INCLUDE) -DRC_OPTIMIZED -D__LINUX__ -DFOREVER_16_BIT -DNO_ASM \
		 -O3 -g -pg $(SDL_CFLAGS)

CXXFLAGS = $(CFLAGS)

LDFLAGS = $(CXXFLAGS) -lz -lpng -lm -lstdc++ $(SDL_LIBS)

# Find all source files
SOURCE = pocketsnes/snes9x menu sal/linux sal
SRC_CPP = $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.cpp))
SRC_C   = $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.c))
OBJ_CPP = $(patsubst %.cpp, %.o, $(SRC_CPP))
OBJ_C   = $(patsubst %.c, %.o, $(SRC_C))
OBJS    = $(OBJ_CPP) $(OBJ_C)

.PHONY : all
all : $(TARGET)

.PHONY: opk
opk: $(TARGET).opk

$(TARGET) : $(OBJS)
	$(SUM) "  LD      $@"
	$(CMD)$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TARGET).opk: $(TARGET)
	$(SUM) "  OPK     $@"
	$(CMD)rm -rf .opk_data
	$(CMD)cp -r data .opk_data
	$(CMD)cp $< .opk_data/pocketsnes.gcw0
	$(CMD)$(STRIP) .opk_data/pocketsnes.gcw0
	$(CMD)mksquashfs .opk_data $@ -all-root -noappend -no-exports -no-xattrs -no-progress >/dev/null

%.o: %.c
	$(SUM) "  CC      $@"
	$(CMD)$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(SUM) "  CXX     $@"
	$(CMD)$(CXX) $(CFLAGS) -c $< -o $@

.PHONY : clean
clean :
	$(SUM) "  CLEAN   ."
	$(CMD)rm -f $(OBJS) $(TARGET)
	$(CMD)rm -rf .opk_data $(TARGET).opk
