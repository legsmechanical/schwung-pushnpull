CC ?= cc
CFLAGS = -O2 -std=gnu11 -I../src/dsp -Wall -Wextra
DSP = ../src/dsp/svf_core.c ../src/dsp/smoother.c ../src/dsp/model_ladder.c ../src/dsp/shaper.c ../src/dsp/clock.c
SRCS = measure.c ../src/dsp/modulation.c $(DSP)
all: measure dump_contract
measure: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o measure -lm
dump_contract: dump_contract.c ../src/dsp/pushnpull.c ../src/dsp/ui_contract.h $(DSP)
	$(CC) $(CFLAGS) dump_contract.c $(DSP) -o dump_contract -lm
clean:
	rm -f measure dump_contract
.PHONY: all clean
