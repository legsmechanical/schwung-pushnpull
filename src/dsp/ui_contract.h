/*
 * ui_contract.h — the UI contract PushNPull serves from get_param.
 *
 * Three knob-grid pages, one level each (Curve / Filter / Output); root only
 * navigates. Param KEYS are the saved-state keys and must not change ("length"
 * is displayed as "Rate").
 *
 * Layout notes, because the knob order looks arbitrary otherwise:
 *   - A graphic must sit contiguously on ONE row of four. The curve picture
 *     (custom:pnpcurve, drawn by canvas.js) spans curve/slope/shift/attack on
 *     the Curve page's bottom row; the filter response spans cutoff/resonance/
 *     mode on the Filter page's bottom row.
 *   - "" in a knobs array is a deliberate blank slot. Do not tidy it away:
 *     every later knob would move up a slot and break the row above.
 *   - ui_phase is read-only and sits on no level: it has no cell, and exists
 *     to carry the playhead to the curve picture (viz.extra_keys).
 */
#ifndef PNP_UI_CONTRACT_H
#define PNP_UI_CONTRACT_H

static const char PNP_UI_HIERARCHY[] =
  "{\"modes\":null,\"levels\":{"
    "\"root\":{\"name\":\"PushNPull\",\"children\":null,\"knobs\":[],"
      "\"params\":[{\"level\":\"curve\",\"label\":\"Curve\"},"
                  "{\"level\":\"filter\",\"label\":\"Filter\"},"
                  "{\"level\":\"output\",\"label\":\"Output\"}]},"
    "\"curve\":{\"name\":\"Curve\",\"label\":\"Curve\",\"children\":null,"
      "\"knobs\":[\"length\",\"volume\",\"filter\",\"\",\"curve\",\"slope\",\"shift\",\"attack\"],"
      "\"params\":[\"curve\",\"length\",\"slope\",\"shift\",\"attack\",\"volume\",\"filter\"]},"
    "\"filter\":{\"name\":\"Filter\",\"label\":\"Filter\",\"children\":null,"
      "\"knobs\":[\"model\",\"drive\",\"\",\"\",\"cutoff\",\"resonance\",\"mode\",\"\"],"
      "\"params\":[\"model\",\"drive\",\"cutoff\",\"resonance\","
                  "{\"key\":\"mode\",\"visible_if\":{\"param\":\"model\",\"equals\":\"SVF\"}}]},"
    "\"output\":{\"name\":\"Output\",\"label\":\"Output\",\"children\":null,"
      "\"knobs\":[\"band_on\",\"band_freq\",\"output\",\"\"],"
      "\"params\":[\"band_on\",\"band_freq\",\"output\"]}}}";

#define PNP_CARD "\"card_script\":\"canvas.js#pnp_curve_card\",\"card_w\":120,\"card_h\":44"

static const char PNP_CHAIN_PARAMS[] =
  "["
  "{\"key\":\"curve\",\"name\":\"Curve\",\"type\":\"enum\","
    "\"options\":[\"Sidechain 1\",\"Sidechain 2\",\"Punch\",\"Sub Bass\",\"Gate\",\"Reverse\",\"Pulse\",\"Push\",\"Trim\",\"Swell\",\"Stutter\",\"Pump\"],"
    "\"default\":\"Sidechain 1\","
    "\"viz\":{\"group\":\"shape\",\"role\":\"curve\",\"kind\":\"custom:pnpcurve\",\"extra_keys\":[\"ui_phase\"]}},"
  "{\"key\":\"slope\",\"name\":\"Slope\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.55,\"step\":0.02,\"unit\":\"%\","
    "\"viz\":{\"group\":\"shape\",\"role\":\"slope\"}," PNP_CARD "},"
  "{\"key\":\"shift\",\"name\":\"Shift\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.5,\"step\":0.02,\"unit\":\"%\","
    "\"viz\":{\"group\":\"shape\",\"role\":\"shift\"}," PNP_CARD "},"
  "{\"key\":\"attack\",\"name\":\"Attack\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0,\"step\":0.02,\"unit\":\"%\","
    "\"viz\":{\"group\":\"shape\",\"role\":\"attack\"}," PNP_CARD "},"
  "{\"key\":\"length\",\"name\":\"Rate\",\"type\":\"enum\",\"options\":[\"1/8\",\"1/4\",\"1/2\",\"1/1\"],\"default\":\"1/4\"},"
  "{\"key\":\"volume\",\"name\":\"Vol Depth\",\"short_name\":\"Vol\",\"type\":\"float\",\"min\":-1,\"max\":1,\"default\":-1,\"step\":0.02,\"unit\":\"%\",\"viz\":false},"
  "{\"key\":\"filter\",\"name\":\"Flt Depth\",\"short_name\":\"Filter\",\"type\":\"float\",\"min\":-1,\"max\":1,\"default\":0,\"step\":0.02,\"unit\":\"%\",\"viz\":false},"
  "{\"key\":\"model\",\"name\":\"Model\",\"type\":\"enum\",\"options\":[\"SVF\",\"Schwoog\"],\"short_options\":[\"SVF\",\"LADDER\"],\"default\":\"SVF\"},"
  "{\"key\":\"drive\",\"name\":\"Drive\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0,\"step\":0.02,\"unit\":\"%\"},"
  "{\"key\":\"cutoff\",\"name\":\"Cutoff\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.5,\"step\":0.02,\"unit\":\"%\","
    "\"viz\":{\"group\":\"flt\",\"role\":\"cutoff\",\"kind\":\"filter\"}},"
  "{\"key\":\"resonance\",\"name\":\"Resonance\",\"short_name\":\"Reso\",\"type\":\"float\",\"min\":0,\"max\":1,\"default\":0.2,\"step\":0.02,\"unit\":\"%\","
    "\"viz\":{\"group\":\"flt\",\"role\":\"resonance\"}},"
  "{\"key\":\"mode\",\"name\":\"Mode\",\"type\":\"enum\",\"options\":[\"LP\",\"HP\",\"BP\",\"Notch\",\"Peak\",\"AP\"],\"default\":\"LP\","
    "\"viz\":{\"group\":\"flt\",\"role\":\"mode\"}},"
  "{\"key\":\"band_on\",\"name\":\"Band Split\",\"short_name\":\"Split\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"],\"default\":\"Off\",\"viz\":{\"kind\":\"switch\"}},"
  "{\"key\":\"band_freq\",\"name\":\"Band Freq\",\"short_name\":\"Xover\",\"type\":\"float\",\"min\":20,\"max\":5120,\"default\":320,\"step\":10,\"unit\":\"Hz\"},"
  "{\"key\":\"output\",\"name\":\"Output\",\"type\":\"float\",\"min\":-24,\"max\":12,\"default\":0,\"step\":0.5,\"unit\":\"dB\",\"viz\":{\"kind\":\"fader\"}},"
  "{\"key\":\"ui_phase\",\"name\":\"Phase\",\"type\":\"string\",\"access\":\"read\"}"
  "]";

#endif
