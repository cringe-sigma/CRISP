# Makefile for multi_proc_pmu + all TACLeBench kernel benches
#
# Build flow per bench <name>:
#   1. Compile every .c in bench/bench/kernel/<name>/ into a private .o under
#      build/<name>/, with -Dmain=<name>_main_disabled to neutralize the bench's
#      own int main().
#   2. Combine those into a relocatable build/<name>.combined.o via `ld -r`.
#   3. Run objcopy --keep-global-symbol=<name>_{init,main,return} so every
#      other symbol becomes local to that single .o. This isolates each bench
#      and prevents cross-bench symbol collisions (e.g. cosf/cubic/isqrt all
#      ship identical copies of wcclibm.c).
#   4. Final link merges multi_proc_pmu.o with all per-bench .o files.

CC      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy
CFLAGS  ?= -O0 -Wall -Wno-unused-result -Wno-unknown-pragmas
LDFLAGS ?=

BENCH_DIR := bench/bench/kernel
BUILD_DIR := build

# Auto-discover every kernel bench directory.
BENCHES := $(notdir $(patsubst %/,%,$(wildcard $(BENCH_DIR)/*/)))

BENCH_OBJS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(BENCHES)))

TARGET := multi_proc_pmu
OBJS   := multi_proc_pmu.o $(BENCH_OBJS)

.PHONY: all clean list-benches
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

multi_proc_pmu.o: multi_proc_pmu.c
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Per-bench template ----------------------------------------------------

# $(1) = bench name
define BENCH_TEMPLATE
$(1)_SRCS := $$(wildcard $$(BENCH_DIR)/$(1)/*.c)
$(1)_OBJS := $$(patsubst $$(BENCH_DIR)/$(1)/%.c,$$(BUILD_DIR)/$(1)/%.o,$$($(1)_SRCS))

$$(BUILD_DIR)/$(1)/%.o: $$(BENCH_DIR)/$(1)/%.c
	@mkdir -p $$(@D)
	$$(CC) $$(CFLAGS) -Dmain=$(1)_main_disabled -c -o $$@ $$<

$$(BUILD_DIR)/$(1).o: $$($(1)_OBJS)
	@mkdir -p $$(@D)
	$$(LD) -r -o $$@.tmp $$^
	$$(OBJCOPY) \
	    --keep-global-symbol=$(1)_init \
	    --keep-global-symbol=$(1)_main \
	    --keep-global-symbol=$(1)_return \
	    $$@.tmp $$@
	@rm -f $$@.tmp
endef

$(foreach b,$(BENCHES),$(eval $(call BENCH_TEMPLATE,$(b))))

list-benches:
	@echo $(BENCHES)

clean:
	rm -rf $(BUILD_DIR) $(TARGET) multi_proc_pmu.o
