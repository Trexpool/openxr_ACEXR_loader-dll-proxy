/* Offline test of the shared trigger curve.
   Build: gcc -std=c11 -Isrc tools/trigger_curve_test.c src/xr_core.c src/Quaternion.c -o trigger_curve_test.exe -lm */
#include "xr_core.h"

#include <math.h>
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
    f.click_threshold = 0.0f;
    check("click synth threshold 0.0: raw 0.0 -> click 1", core_trigger_click(0.0f, &f) == 1);
    f.click_threshold = 0.5f;
    check("click synth threshold 0.5: raw 0.49 -> 0, raw 0.5 -> 1",
          core_trigger_click(0.49f, &f) == 0 && core_trigger_click(0.5f, &f) == 1);
    f.click_threshold = 1.0f;
    check("click synth threshold 1.0: raw 0.99 -> 0, raw 1.0 -> 1",
          core_trigger_click(0.99f, &f) == 0 && core_trigger_click(1.0f, &f) == 1);
    f.click_threshold = 0.5f;
    f.exponent = 2.0f;
    check("click uses curved value: raw 0.25 exp 2 -> curved 0.5 -> click 1", core_trigger_click(0.25f, &f) == 1);
  }

  printf("%s (%d failure(s))\n", failures ? "OVERALL FAIL" : "OVERALL PASS", failures);
  return failures ? 1 : 0;
}
