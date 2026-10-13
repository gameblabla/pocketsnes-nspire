# Tests of the ARM assembler CPU core (pocketsnes/arm) against the C core.
# The core is built for ARM with the Ndless toolchain and newlib's
# semihosting library (files through the host), and run under qemu-arm.
# From the repository root:
#   make -f tests/asm/Makefile       builds build/asm-test/opcode_test and game_test
#   tests/asm/run.sh                 builds and runs them
# The toolchain is the Ndless one (. ~/nspire/build/env.sh puts it on PATH).

CXX = arm-none-eabi-g++
CC  = arm-none-eabi-gcc

BUILD = build/asm-test

ARCH    = -marm -mcpu=arm926ej-s
DEFINES = -DRC_OPTIMIZED -D__LINUX__ -DFOREVER_16_BIT -DNO_ASM -DARM_ASM_CPU -DARM_ASM_FX
# zlib.h from the Ndless SDK (the core's port.h includes it)
ZLIB    = $(HOME)/nspire/build/Ndless/ndless-sdk/thirdparty/zlib
INCLUDE = -Ipocketsnes -Ipocketsnes/include -Ipocketsnes/linux -Ipocketsnes/snes9x -isystem $(ZLIB)
CORE_FLAGS = $(ARCH) -O2 -fno-strict-aliasing $(DEFINES) $(INCLUDE) -w
TEST_FLAGS = $(ARCH) -O2 -fno-strict-aliasing $(DEFINES) $(INCLUDE) -Wall -Wno-unused-function
LDFLAGS = $(ARCH) -specs=rdimon.specs
LIBS    = $(ZLIB)/libz.a

CORE_SRC = $(wildcard pocketsnes/snes9x/*.cpp)
CORE_OBJ = $(patsubst %.cpp,$(BUILD)/%.o,$(CORE_SRC))
ASM_DEPS = $(wildcard pocketsnes/arm/*.s)
COMMON   = $(BUILD)/tests/asm/stubs.o $(BUILD)/tests/asm/newlib_shim.o $(BUILD)/arm/global.o \
           $(BUILD)/frontend/romzip.o $(BUILD)/arm/fx_run_arm.o

all: $(BUILD)/opcode_test $(BUILD)/game_test $(BUILD)/game_trace $(BUILD)/fx_test

$(BUILD)/pocketsnes/%.o: pocketsnes/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_FLAGS) -MMD -c $< -o $@

# The ROM loader reads zipped ROMs through the front end's romzip.cpp.
$(BUILD)/frontend/romzip.o: frontend/romzip.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(TEST_FLAGS) -MMD -c $< -o $@

$(BUILD)/tests/asm/%.o: tests/asm/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(TEST_FLAGS) -MMD -c $< -o $@

# -fno-builtin: GCC would turn the shim's calloc (malloc + memset) into a
# call to calloc.
$(BUILD)/tests/asm/%.o: tests/asm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -O2 -fno-builtin -c $< -o $@

$(BUILD)/arm/global.o: pocketsnes/arm/os9x_65c816_global.s $(ASM_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -Ipocketsnes/arm -c $< -o $@

# The Super FX interpreter; the _counted one stops after a given number of
# instructions, for fx_test.
$(BUILD)/arm/fx_run_arm.o: pocketsnes/arm/fx_run_arm.s
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -c $< -o $@

$(BUILD)/arm/fx_run_arm_counted.o: pocketsnes/arm/fx_run_arm.s
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -Wa,--defsym,FX_COUNTED=1 -c $< -o $@

# fx_test (tests/fx_test.cpp): fxinst.cpp also built with the original loop
$(BUILD)/fxtest/%.o: pocketsnes/snes9x/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_FLAGS) -DFX_REFERENCE_RUN -MMD -c $< -o $@

$(BUILD)/tests/fx_test.o: tests/fx_test.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(TEST_FLAGS) -MMD -c $< -o $@

$(BUILD)/fx_test: $(BUILD)/tests/fx_test.o $(BUILD)/fxtest/fxinst.o $(BUILD)/fxtest/fxemu.o \
		  $(BUILD)/arm/fx_run_arm.o $(BUILD)/arm/fx_run_arm_counted.o $(BUILD)/tests/asm/newlib_shim.o
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD)/arm/cpu.o: pocketsnes/arm/os9x_65c816_cpu.s $(ASM_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -Ipocketsnes/arm -c $< -o $@

# One opcode per call, for the opcode test.
$(BUILD)/arm/cpu_step.o: pocketsnes/arm/os9x_65c816_cpu.s $(ASM_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -Ipocketsnes/arm -Wa,--defsym,ASM_SINGLE_STEP=1 -c $< -o $@

# Calls asm_trace before every opcode, for game_trace.
$(BUILD)/arm/cpu_trace.o: pocketsnes/arm/os9x_65c816_cpu.s $(ASM_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -Ipocketsnes/arm -Wa,--defsym,ASM_TRACE=1 -c $< -o $@

$(BUILD)/tests/asm/game_trace.o: tests/asm/game_test.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(TEST_FLAGS) -DGAME_TRACE -MMD -c $< -o $@

$(BUILD)/libcore.a: $(CORE_OBJ)
	rm -f $@
	arm-none-eabi-ar rcs $@ $^

$(BUILD)/opcode_test: $(BUILD)/tests/asm/opcode_test.o $(BUILD)/arm/cpu_step.o $(COMMON) $(BUILD)/libcore.a
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD)/game_test: $(BUILD)/tests/asm/game_test.o $(BUILD)/arm/cpu.o $(COMMON) $(BUILD)/libcore.a
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

# game_test that prints every opcode of chosen frames, on either core
$(BUILD)/game_trace: $(BUILD)/tests/asm/game_trace.o $(BUILD)/arm/cpu_trace.o $(COMMON) $(BUILD)/libcore.a
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean

-include $(CORE_OBJ:.o=.d) $(BUILD)/tests/asm/opcode_test.d $(BUILD)/tests/asm/game_test.d \
	 $(BUILD)/tests/asm/game_trace.d
