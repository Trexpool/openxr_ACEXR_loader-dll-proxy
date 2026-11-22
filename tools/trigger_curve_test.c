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

  printf("%s (%d failure(s))\n", failures ? "OVERALL FAIL" : "OVERALL PASS", failures);
  return failures ? 1 : 0;
}
