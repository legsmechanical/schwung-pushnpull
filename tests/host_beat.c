/* The plugin end to end against a fake host, for the one thing clock.c's unit
 * checks cannot see: whether pushnpull.c ASKS the host for its transport.
 *   host_beat          host exports shadow_transport_beat_position
 *   host_beat_old      it does not: an old host, where the struct field must
 *                      not be read at all
 * It includes the plugin source so the binding under test is the one that ships. */
#include "../src/dsp/pushnpull.c"

static double g_beat = -1.0;
static int    g_field_calls = 0;
static double fake_beat(void){ g_field_calls++; return g_beat; }
static float  fake_bpm(void){ return 120.0f; }
#ifdef WITH_SHIM_SYMBOL
__attribute__((visibility("default"), used))
double shadow_transport_beat_position(void){ return g_beat; }
#endif

static int fails = 0;
#define CHECK(c, ...) do{ if(c) printf("ok: "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); }while(0)

/* Runs `blocks` blocks of a full-scale constant and returns the quietest sample. */
static int run(audio_fx_api_v2_t *api, void *I, int blocks, int advance){
    int16_t buf[256]; int lo = 32767;
    for (int b = 0; b < blocks; b++){
        for (int i = 0; i < 256; i++) buf[i] = 20000;
        api->process_block(I, buf, 128);
        for (int i = 0; i < 256; i++) if (buf[i] < lo) lo = buf[i];
        if (advance) g_beat += 128.0 * 120.0 / (60.0 * SAMPLE_RATE);
    }
    return lo;
}

int main(void){
    host_api_v1_t host; memset(&host, 0, sizeof(host));
    host.get_bpm = fake_bpm;
    host.get_beat_position = fake_beat;
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&host);
    void *I = api->create_instance(".", NULL);
    if (!I) return 1;
    char buf[64];

    g_beat = -1.0;
    CHECK(run(api, I, 200, 0) == 20000, "transport stopped, no MIDI clock: audio untouched");
    g_beat = 0.0;
    int lo = run(api, I, 400, 1);   /* ~1.16 s = over two 1/4-note cycles */
#ifdef WITH_SHIM_SYMBOL
    CHECK(lo < 2000, "host transport running, no MIDI clock: the curve ducks (min %d)", lo);
    api->get_param(I, "ui_phase", buf, sizeof(buf));
    CHECK(buf[strlen(buf)-1] == '1', "ui_phase reports active (%s)", buf);
    g_beat = -1.0;
    run(api, I, 4, 0);
    CHECK(run(api, I, 200, 0) == 20000, "transport stopped again: audio untouched");
#else
    CHECK(lo == 20000, "old host: stays bypassed without MIDI clock (min %d)", lo);
    CHECK(g_field_calls == 0, "old host: the struct field is never called (%d calls)", g_field_calls);
    api->get_param(I, "ui_phase", buf, sizeof(buf));
    CHECK(buf[strlen(buf)-1] == '0', "old host: ui_phase reports inactive (%s)", buf);
#endif
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
