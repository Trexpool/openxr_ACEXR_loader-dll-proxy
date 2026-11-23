/* Offline test of the shared trigger curve.
   Build: gcc -std=c11 -Isrc tools/trigger_curve_test.c src/xr_core.c src/Quaternion.c -o trigger_curve_test.exe -lm */
#include "xr_core.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok)
	failures++;
}

static int near(float a, float b)
{
  return fabsf(a - b) < 1e-4f;
}

int main(void)
{
  CoreTriggerCfg c;
  int ok;

  core_trigger_cfg_defaults(&c);
  check("0.0 -> 0.0", near(core_trigger_curve(0.0f, &c), 0.0f));
  check("1.0 -> 1.0", near(core_trigger_curve(1.0f, &c), 1.0f));

  ok = 1;
  for (int i = 0; i <= 1000; ++i)
  {
	float v = i / 1000.0f;
	if (!near(core_trigger_curve(v, &c), v) || !near(core_trigger_output(v, &c), v))
	  ok = 0;
  }
  check("exponent 1.0 is identity on [0,1]", ok);

  core_trigger_cfg_defaults(&c);
  c.deadzone = 0.2f;
  ok = near(core_trigger_curve(0.0f, &c), 0.0f) && near(core_trigger_curve(0.19f, &c), 0.0f) &&
	   near(core_trigger_curve(0.2f, &c), 0.0f) && near(core_trigger_curve(0.6f, &c), 0.5f) &&
	   near(core_trigger_curve(1.0f, &c), 1.0f);
  check("deadzone 0.2 removes [0,0.2) and remaps [0.2,1] linearly", ok);

  core_trigger_cfg_defaults(&c);
  c.exponent = 2.0f;
  check("exponent 2.0: 0.25 -> 0.5 (easier)", near(core_trigger_curve(0.25f, &c), 0.5f));
  check("exponent 2.0: endpoints preserved",
		near(core_trigger_curve(0.0f, &c), 0.0f) && near(core_trigger_curve(1.0f, &c), 1.0f));
  c.exponent = 0.5f;
  check("exponent 0.5: 0.5 -> 0.25 (deeper)", near(core_trigger_curve(0.5f, &c), 0.25f));

  core_trigger_cfg_defaults(&c);
  c.gain = 2.0f;
  check("gain 2.0 analog: 0.25 -> 0.5", near(core_trigger_output(0.25f, &c), 0.5f));
  check("gain 2.0 analog: 0.8 clamps to 1.0", near(core_trigger_output(0.8f, &c), 1.0f));
  check("gain does not affect boolean", core_trigger_pressed(0.4f, &c) == 0);

  core_trigger_cfg_defaults(&c);
  c.exponent = 2.0f;
  c.press_threshold = 0.5f;
  check("boolean uses CURVED value (raw 0.3 -> curved 0.548 >= 0.5)", core_trigger_pressed(0.3f, &c) == 1);
  check("boolean uses CURVED value (raw 0.2 -> curved 0.447 < 0.5)", core_trigger_pressed(0.2f, &c) == 0);
  c.exponent = 1.0f;
  check("boolean raw 0.3 < 0.5 at exponent 1", core_trigger_pressed(0.3f, &c) == 0);

  core_trigger_cfg_defaults(&c);
  c.press_threshold = 0.0f;
  check("threshold 0.0 -> always pressed (documented hazard)", core_trigger_pressed(0.0f, &c) == 1);

  ok = 1;
  for (int d = 0; d <= 9; ++d)
	for (int e = 0; e < 20; ++e)
	  for (int g = 0; g < 10; ++g)
		for (int i = 0; i < 50; ++i)
		{
		  CoreTriggerCfg s;
		  float v = (i - 5) / 40.0f; /* includes out-of-range inputs */
		  float curved, out;
		  s.deadzone = d / 10.0f;
		  s.exponent = 0.01f + e * 1.05f;
		  s.press_threshold = 0.5f;
		  s.gain = g * 1.5f;
		  core_trigger_cfg_sanitize(&s);
		  curved = core_trigger_curve(v, &s);
		  out = core_trigger_output(v, &s);
		  if (!(curved >= 0.0f && curved <= 1.0f && out >= 0.0f && out <= 1.0f))
			ok = 0;
		}
  check("sweep of 100000 inputs stays within [0,1]", ok);

  {
    CoreTriggerCfg z;
    float dz = 0.2f;
    float raws[5];
    float zero = 0.0f;
    unsigned int zbits, obits;
    int zok = 1;
    int exps = 0;
    static const float expv[3] = {1.0f, 2.0f, 0.5f};
    memcpy(&zbits, &zero, sizeof zbits);
    core_trigger_cfg_defaults(&z);
    z.deadzone = dz;
    raws[0] = 0.0f;
    raws[1] = 0.000001f;
    raws[2] = 0.001f;
    raws[3] = dz * 0.5f;
    raws[4] = dz * 0.999f;
    for (int e = 0; e < 3; ++e)
      for (int g = 0; g < 2; ++g)
      {
        z.exponent = expv[e];
        z.gain = g ? 50.0f : 1.0f;
        for (int i = 0; i < 5; ++i)
        {
          float c = core_trigger_curve(raws[i], &z);
          float o = core_trigger_output(raws[i], &z);
          memcpy(&obits, &c, sizeof obits);
          if (obits != zbits) zok = 0;
          memcpy(&obits, &o, sizeof obits);
          if (obits != zbits) zok = 0;
        }
      }
    check("below deadzone output is bit-exactly 0.0f (curve and output, exps 1/2/0.5, gain 1/50)", zok);
    (void)exps;

    {
      CoreTriggerCfg n;
      float neg = -0.5f, o;
      core_trigger_cfg_defaults(&n);
      o = core_trigger_output(neg, &n);
      memcpy(&obits, &o, sizeof obits);
      check("negative raw yields bit-exact +0.0f", obits == zbits);
      n.gain = 100.0f;
      check("gain 100 still clamps to 1.0", core_trigger_output(0.9f, &n) == 1.0f);
    }
  }

  {
    /* Reference copy of the pre-change algorithm (commit 8abeb52) for bit-identity checks. */
    CoreTriggerCfg a, b;
    int id_force = 1, id_range = 1, id_click = 1, id_bool = 1;
    unsigned int seed = 12345u;
    core_trigger_cfg_defaults(&a);
    a.deadzone = 0.15f;
    a.exponent = 1.7f;
    a.gain = 1.3f;
    a.press_threshold = 0.4f;
    b = a;
    for (int i = 0; i < 10000; ++i)
    {
      float v;
      float ref_curve, ref_out;
      float v1;
      seed = seed * 1664525u + 1013904223u;
      v = ((seed >> 8) & 0xFFFF) / 65535.0f * 1.2f - 0.1f;
      v1 = v >= 0.0f ? (v > 1.0f ? 1.0f : v) : 0.0f;
      v1 = (v1 - a.deadzone) / (1.0f - a.deadzone);
      v1 = v1 >= 0.0f ? (v1 > 1.0f ? 1.0f : v1) : 0.0f;
      ref_curve = powf(v1, 1.0f / a.exponent);
      ref_out = ref_curve * a.gain;
      ref_out = ref_out >= 0.0f ? (ref_out > 1.0f ? 1.0f : ref_out) : 0.0f;
      if (memcmp(&ref_out, &(float){core_trigger_output(v, &a)}, sizeof(float)) != 0)
        id_force = 0;
      b.force_value = -1.0f;
      b.range_max = 1.0f;
      if (core_trigger_output(v, &b) != core_trigger_output(v, &a))
        id_range = 0;
      if (core_trigger_pressed(v, &b) != (ref_curve >= a.press_threshold))
        id_bool = 0;
      (void)id_click;
    }
    check("force_value=-1 bit-identical to legacy output over 10000 samples", id_force);
    check("range_max=1.0 identical over 10000 samples", id_range);
    check("force/range disabled: Boolean identical to legacy over 10000 samples", id_bool);
  }

  {
    CoreTriggerCfg f;
    core_trigger_cfg_defaults(&f);
    f.force_value = 0.10f;
    ok = 1;
    for (int i = 0; i <= 1000; ++i)
      if (core_trigger_output(i / 1000.0f, &f) != 0.10f)
        ok = 0;
    check("force_value 0.10 ignores raw (analog constant)", ok);
    check("force_value Boolean derived from forced value (0.10 vs thr 0.5 -> 0)", core_trigger_pressed(1.0f, &f) == 0);
    f.force_value = 7.0f;
    core_trigger_cfg_sanitize(&f);
    check("force_value clamps to 1.0", core_trigger_output(0.0f, &f) == 1.0f);

    core_trigger_cfg_defaults(&f);
    f.range_max = 0.5f;
    check("range_max 0.5: raw 0.5 -> 1.0", near(core_trigger_output(0.5f, &f), 1.0f));
    check("range_max 0.5: raw 0.25 -> 0.5", near(core_trigger_output(0.25f, &f), 0.5f));

    core_trigger_cfg_defaults(&f);
    check("click threshold default is passthrough (-1, disabled)", !core_trigger_click_enabled(&f));
    check("click passthrough: runtime 0 -> 0, 1 -> 1 for any raw",
          core_trigger_click(0.9f, 0, &f) == 0 && core_trigger_click(0.0f, 1, &f) == 1);
    check("click defaults: mode replace, compare rawpull",
          f.click_mode == CORE_CLICK_REPLACE && f.click_compare == CORE_COMPARE_RAWPULL);
    f.click_threshold = 0.0f;
    check("click synth threshold 0.0: raw 0.0 -> click 1", core_trigger_click(0.0f, 0, &f) == 1);
    f.click_threshold = 0.5f;
    check("click synth threshold 0.5: raw 0.49 -> 0, raw 0.5 -> 1",
          core_trigger_click(0.49f, 0, &f) == 0 && core_trigger_click(0.5f, 0, &f) == 1);
    f.click_threshold = 1.0f;
    check("click synth threshold 1.0: raw 0.99 -> 0, raw 1.0 -> 1",
          core_trigger_click(0.99f, 0, &f) == 0 && core_trigger_click(1.0f, 0, &f) == 1);

    /* click_mode replace/or/and at thresholds 0.0, 0.5, 1.0, runtime click 0 and 1 */
    {
      static const float th[3] = {0.0f, 0.5f, 1.0f};
      static const float rawv[2] = {0.25f, 1.0f};
      int ok_replace = 1, ok_or = 1, ok_and = 1;
      for (int t = 0; t < 3; ++t)
        for (int r = 0; r < 2; ++r)
          for (int rt = 0; rt < 2; ++rt)
          {
            int hit;
            core_trigger_cfg_defaults(&f);
            f.click_threshold = th[t];
            hit = rawv[r] >= th[t];
            f.click_mode = CORE_CLICK_REPLACE;
            if (core_trigger_click(rawv[r], rt, &f) != hit) ok_replace = 0;
            f.click_mode = CORE_CLICK_OR;
            if (core_trigger_click(rawv[r], rt, &f) != (rt || hit)) ok_or = 0;
            f.click_mode = CORE_CLICK_AND;
            if (core_trigger_click(rawv[r], rt, &f) != (rt && hit)) ok_and = 0;
          }
      check("click_mode replace ignores runtime (thresholds 0/0.5/1, runtime 0/1)", ok_replace);
      check("click_mode or = runtime || hit (thresholds 0/0.5/1, runtime 0/1)", ok_or);
      check("click_mode and = runtime && hit (thresholds 0/0.5/1, runtime 0/1)", ok_and);
    }

    /* click_compare: rawpull vs curved with exponent 2.0 */
    core_trigger_cfg_defaults(&f);
    f.exponent = 2.0f;
    f.click_threshold = 0.25f;
    f.click_compare = CORE_COMPARE_RAWPULL;
    check("rawpull exp 2 thr 0.25: fires at raw 0.25", core_trigger_click(0.25f, 0, &f) == 1);
    check("rawpull exp 2 thr 0.25: does NOT fire at raw 0.0625", core_trigger_click(0.0625f, 0, &f) == 0);
    f.click_compare = CORE_COMPARE_CURVED;
    check("curved exp 2 thr 0.25: fires at raw 0.0625", core_trigger_click(0.0625f, 0, &f) == 1);
    check("curved exp 2 thr 0.25: raw 0.05 does not fire", core_trigger_click(0.05f, 0, &f) == 0);
    core_trigger_cfg_defaults(&f);
    f.deadzone = 0.2f;
    f.exponent = 2.0f;
    f.gain = 3.0f;
    f.click_threshold = 0.5f;
    check("rawpull uses deadzone but not exponent/gain: raw 0.6 -> pull 0.5",
          near(core_trigger_click_compare(0.6f, &f), 0.5f) && core_trigger_click(0.6f, 0, &f) == 1 &&
          core_trigger_click(0.59f, 0, &f) == 0);
    check("click mode/compare name parsing",
          core_click_mode_parse("or") == CORE_CLICK_OR && core_click_mode_parse("bogus") < 0 &&
          core_click_compare_parse("curved") == CORE_COMPARE_CURVED && core_click_compare_parse("x") < 0);
  }

  /* path matching: COMPLETE paths, never a loose suffix */
  {
    const char *R = "/user/hand/right", *Lh = "/user/hand/left";
    char p[96];
    int ok;
    snprintf(p, sizeof(p), "%s/input/trigger", R);
    check("path .../right/input/trigger matches as CLICK", core_trigger_path_matches(p, CORE_ROLE_CLICK, CORE_HAND_RIGHT));
    check("path .../right/input/trigger is not AXIS", !core_trigger_path_matches(p, CORE_ROLE_AXIS, CORE_HAND_RIGHT));
    snprintf(p, sizeof(p), "%s/input/trigger/click", R);
    check("path .../right/input/trigger/click matches as CLICK", core_trigger_path_matches(p, CORE_ROLE_CLICK, CORE_HAND_RIGHT));
    snprintf(p, sizeof(p), "%s/input/trigger/value", R);
    check("path .../right/input/trigger/value matches as AXIS", core_trigger_path_matches(p, CORE_ROLE_AXIS, CORE_HAND_RIGHT));
    check("path .../right/input/trigger/value NEVER matches as CLICK", !core_trigger_path_matches(p, CORE_ROLE_CLICK, CORE_HAND_RIGHT));
    snprintf(p, sizeof(p), "%s/input/trigger/touch", R);
    check("path .../right/input/trigger/touch does NOT match as CLICK", !core_trigger_path_matches(p, CORE_ROLE_CLICK, CORE_HAND_RIGHT));
    check("path .../right/input/trigger/touch is TOUCH", core_trigger_path_matches(p, CORE_ROLE_TOUCH, CORE_HAND_RIGHT));
    ok = 1;
    {
      static const char *suffixes[] = {"/input/trigger", "/input/trigger/click", "/input/trigger/value", "/input/trigger/touch"};
      static const int roles[] = {CORE_ROLE_CLICK, CORE_ROLE_CLICK, CORE_ROLE_AXIS, CORE_ROLE_TOUCH};
      for (int i = 0; i < 4; ++i)
      {
        snprintf(p, sizeof(p), "%s%s", Lh, suffixes[i]);
        if (core_trigger_path_matches(p, roles[i], CORE_HAND_RIGHT)) ok = 0;
        if (!core_trigger_path_matches(p, roles[i], CORE_HAND_LEFT)) ok = 0;
      }
    }
    check("LEFT-hand variants never match a right-hand action (and match left)", ok);
    check("loose paths do not match: .../trigger/valuex, .../triggerx, .../trigger/click/x",
          core_trigger_classify_path("/user/hand/right/input/trigger/valuex", NULL) == CORE_ROLE_OTHER &&
          core_trigger_classify_path("/user/hand/right/input/triggerx", NULL) == CORE_ROLE_OTHER &&
          core_trigger_classify_path("/user/hand/right/input/trigger/click/x", NULL) == CORE_ROLE_OTHER &&
          core_trigger_classify_path("/user/hand/rightx/input/trigger", NULL) == CORE_ROLE_OTHER);
  }

  /* the three actions from the in-game log; axis is never the click; runtime-confirmed wins */
  {
    XrAction cl = (XrAction)(uintptr_t)101, ax = (XrAction)(uintptr_t)102, tc = (XrAction)(uintptr_t)103;
    core_trigger_reset_sources();
    core_trigger_note_action(cl, XR_ACTION_TYPE_BOOLEAN_INPUT, "oculustouch_right_trigger_click");
    core_trigger_note_action(ax, XR_ACTION_TYPE_FLOAT_INPUT, "oculustouch_right_trigger_axis");
    core_trigger_note_action(tc, XR_ACTION_TYPE_BOOLEAN_INPUT, "oculustouch_right_trigger_touch");
    core_trigger_mark_source(cl, "/user/hand/right/input/trigger");
    core_trigger_mark_source(ax, "/user/hand/right/input/trigger/value");
    core_trigger_mark_source(tc, "/user/hand/right/input/trigger/touch");
    check("UE4 legacy bool action is click for the right hand", core_trigger_click_flags(cl) == CORE_HAND_RIGHT);
    check("analog axis action is never treated as the click", core_trigger_click_flags(ax) == 0);
    check("touch action is neither click nor axis", core_trigger_click_flags(tc) == 0 && core_trigger_hand_flags(tc) == 0);
    check("axis action is found as the float trigger", core_trigger_find_float_action(CORE_HAND_RIGHT) == ax);
    check("click action is not the axis", core_trigger_hand_flags(cl) == 0);
    core_trigger_mark_source_confirmed(cl, "/user/hand/right/input/squeeze/click");
    check("runtime-confirmed binding overrides the suggested one (no longer a click)", core_trigger_click_flags(cl) == 0);
    core_trigger_mark_source_confirmed(cl, "/user/hand/right/input/trigger");
    check("runtime-confirmed trigger path is a click again", core_trigger_click_flags(cl) == CORE_HAND_RIGHT && core_click_source_confirmed(cl));
    {
      char line[512];
      int okfmt = core_action_table_format(0, line, sizeof(line));
      printf("  %s\n", line);
      check("action table shows type/bound/role", okfmt && strstr(line, "type=bool") && strstr(line, "role=click") && strstr(line, "bound=["));
      core_trigger_note_action((XrAction)(uintptr_t)104, XR_ACTION_TYPE_BOOLEAN_INPUT, "orphan");
      okfmt = core_action_table_format(3, line, sizeof(line));
      printf("  %s\n", line);
      check("unresolved action says UNRESOLVED", okfmt && strstr(line, "UNRESOLVED"));
    }
    core_trigger_reset_sources();
  }

  printf("%s (%d failure(s))\n", failures ? "OVERALL FAIL" : "OVERALL PASS", failures);
  return failures ? 1 : 0;
}
