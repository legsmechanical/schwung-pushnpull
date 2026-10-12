CC ?= cc
CFLAGS = -O2 -std=gnu11 -I../src/dsp -Wall -Wextra
DSP = ../src/dsp/svf_core.c ../src/dsp/smoother.c ../src/dsp/model_ladder.c ../src/dsp/shaper.c ../src/dsp/clock.c
SRCS = measure.c ../src/dsp/modulation.c $(DSP)
all: measure dump_contract host_beat host_beat_old
measure: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o measure -lm
dump_contract: dump_contract.c ../src/dsp/pushnpull.c ../src/dsp/ui_contract.h $(DSP)
	$(CC) $(CFLAGS) dump_contract.c $(DSP) -o dump_contract -lm
# -rdynamic: on Linux dlsym only finds a symbol the executable exports.
host_beat: host_beat.c ../src/dsp/pushnpull.c $(DSP)
	$(CC) $(CFLAGS) -rdynamic -DWITH_SHIM_SYMBOL host_beat.c $(DSP) -o host_beat -lm
host_beat_old: host_beat.c ../src/dsp/pushnpull.c $(DSP)
	$(CC) $(CFLAGS) -rdynamic host_beat.c $(DSP) -o host_beat_old -lm
clean:
	rm -f measure dump_contract host_beat host_beat_old
.PHONY: all clean
