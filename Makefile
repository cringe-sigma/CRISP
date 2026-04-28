CC      ?= gcc
# -O0 is required: we measure the workload as written, with no compiler
# optimizations. Higher levels would let the compiler hoist, vectorize or
# eliminate parts of the measured code and skew the cycles/instructions.
CFLAGS  ?= -O0 -g -Wall -Wextra -std=gnu11
LDFLAGS ?=

TARGET  := multi_proc_pmu
SRC     := multi_proc_pmu.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)
