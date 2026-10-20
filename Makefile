# PC test build: the same core and frontend as the calculator build, with
# frontend/platform_host.cpp standing in for the calculator. See TESTING.md.
#   make                 window (SDL2) + headless script mode
#   make SDL=0           headless script mode only
# The calculator build is Makefile.nspire.

TARGET = pocketsnes-host
BUILD  = build/host

CXX = g++

# SDL2 headers: the system ones, or the libsdl2-dev package unpacked into
# ~/nspire/build/hostdeps (needs no root). Links against the installed runtime.
SDL ?= 1
SDL_INCLUDE ?= $(firstword $(wildcard /usr/include/SDL2/SDL.h $(HOME)/nspire/build/hostdeps/root/usr/include/SDL2/SDL.h))
SDL_LIB ?= $(firstword $(wildcard /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 /usr/lib/libSDL2-2.0.so.0))

INCLUDE = -Ipocketsnes -Ipocketsnes/include -Ipocketsnes/linux -Ipocketsnes/snes9x -Ifrontend
# The frontend sees the core's headers as system headers, so their warnings stay quiet.
FRONTEND_INCLUDE = -Ifrontend -isystem pocketsnes -isystem pocketsnes/include -isystem pocketsnes/linux
BENCH ?= 0

DEFINES = -DRC_OPTIMIZED -D__LINUX__ -DFOREVER_16_BIT -DNO_ASM
ifeq ($(BENCH),1)
DEFINES += -DAUTO_BENCH
endif
OPT     = -O2 -g -fno-strict-aliasing
LIBS    = -lz

ifeq ($(SDL),1)
ifneq ($(SDL_INCLUDE),)
ifneq ($(SDL_LIB),)
DEFINES += -DHOST_SDL
SDL_FLAGS = -I$(dir $(SDL_INCLUDE)).. -I$(dir $(SDL_INCLUDE))../x86_64-linux-gnu -D_REENTRANT
LIBS    += $(SDL_LIB)
endif
endif
endif

CORE_FLAGS     = $(OPT) $(DEFINES) $(INCLUDE) -w
FRONTEND_FLAGS = $(OPT) $(DEFINES) $(FRONTEND_INCLUDE) $(SDL_FLAGS) -Wall -Wextra -Wno-unused-parameter

CORE_SRC     = $(wildcard pocketsnes/snes9x/*.cpp)
FRONTEND_SRC = frontend/savemanager.cpp frontend/romzip.cpp frontend/romtypes.cpp frontend/paths.cpp frontend/selfstore.cpp frontend/brightness.cpp frontend/keys.cpp frontend/draw.cpp frontend/ui.cpp frontend/config.cpp frontend/states.cpp \
               frontend/gui.cpp frontend/browser.cpp frontend/menu.cpp frontend/overclock.cpp frontend/emu.cpp \
               frontend/main.cpp frontend/rotate.cpp frontend/platform_host.cpp

CORE_OBJ     = $(patsubst %.cpp,$(BUILD)/%.o,$(CORE_SRC))
FRONTEND_OBJ = $(patsubst %.cpp,$(BUILD)/%.o,$(FRONTEND_SRC))

all: $(TARGET)

$(BUILD)/pocketsnes/%.o: pocketsnes/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_FLAGS) -MMD -c $< -o $@

$(BUILD)/frontend/%.o: frontend/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FRONTEND_FLAGS) -MMD -c $< -o $@

$(TARGET): $(CORE_OBJ) $(FRONTEND_OBJ)
	$(CXX) $^ -o $@ $(LIBS)

# Unit tests (tests/unit_tests.cpp): the core as a library, so only what the
# tests use is linked, plus the frontend files they test.
UNIT_TESTS = tests/unit_tests
UNIT_OBJ   = $(BUILD)/tests/unit_tests.o $(BUILD)/frontend/config.o $(BUILD)/frontend/keys.o \
             $(BUILD)/frontend/rotate.o $(BUILD)/frontend/brightness.o $(BUILD)/frontend/paths.o $(BUILD)/frontend/selfstore.o $(BUILD)/frontend/romzip.o \
             $(BUILD)/frontend/romtypes.o

$(BUILD)/libcore.a: $(CORE_OBJ)
	rm -f $@
	ar rcs $@ $^

$(BUILD)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(OPT) $(DEFINES) $(INCLUDE) -w -MMD -c $< -o $@

$(UNIT_TESTS): $(UNIT_OBJ) $(BUILD)/libcore.a
	$(CXX) $^ -o $@ -lz

unit-tests: $(UNIT_TESTS)
	./$(UNIT_TESTS)

clean:
	rm -rf $(BUILD) $(TARGET) $(UNIT_TESTS)

.PHONY: all clean unit-tests

-include $(CORE_OBJ:.o=.d) $(FRONTEND_OBJ:.o=.d) $(BUILD)/tests/unit_tests.d
