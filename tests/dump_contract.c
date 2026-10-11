/* Dumps what the plugin SERVES, for the off-device checks:
 *   dump_contract contract  -> {"ui_hierarchy":...,"chain_params":...}
 *   dump_contract curves    -> shaper_eval samples, for the canvas.js mirror test
 * It includes the plugin source so the strings are the ones that ship. */
#include "../src/dsp/pushnpull.c"

int main(int argc, char **argv){
    static char buf[131072];
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(NULL);
    void *I = api->create_instance(".", NULL);
    if (!I) return 1;
    if (argc > 1 && !strcmp(argv[1], "curves")){
        static const float SL[] = {0.1f, 0.55f, 1.0f}, SH[] = {0.5f, 0.2f}, AT[] = {0.0f, 0.3f};
        shaper_t s; shaper_init(&s);
        printf("[");
        int first = 1;
        for (int c = 0; c < PNP_CURVE_COUNT; c++)
        for (int a = 0; a < 3; a++) for (int b = 0; b < 2; b++) for (int d = 0; d < 2; d++){
            s.curve = c; s.slope = SL[a]; s.shift = SH[b]; s.attack = AT[d];
            printf("%s{\"cv\":%d,\"slope\":%.2f,\"shift\":%.2f,\"attack\":%.2f,\"m\":[", first?"":",", c, SL[a], SH[b], AT[d]);
            first = 0;
            for (int i = 0; i < 64; i++) printf("%s%.5f", i?",":"", shaper_eval(&s, (i + 0.37) / 64.0));
            printf("]}");
        }
        printf("]\n");
        return 0;
    }
    printf("{\"ui_hierarchy\":");
    if (api->get_param(I, "ui_hierarchy", buf, sizeof(buf)) < 0) return 2;
    printf("%s,\"chain_params\":", buf);
    if (api->get_param(I, "chain_params", buf, sizeof(buf)) < 0) return 3;
    printf("%s,\"ui_phase\":", buf);
    if (api->get_param(I, "ui_phase", buf, sizeof(buf)) < 0) return 4;
    printf("\"%s\",\"band_freq\":", buf);
    if (api->get_param(I, "band_freq", buf, sizeof(buf)) < 0) return 5;
    printf("\"%s\"}\n", buf);
    return 0;
}
