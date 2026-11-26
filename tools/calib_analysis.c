/* Offline calibration math analysis. Uses the project's own Quaternion.c.
   Build: gcc -std=c11 -Isrc tools/calib_analysis.c src/Quaternion.c -o obj/calib_analysis.exe -lm */
#include <stdio.h>
#include <math.h>
#include "Quaternion.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define D2R(a) ((a) * M_PI / 180.0)
#define R2D(a) ((a) * 180.0 / M_PI)

/* exactly what xrLocateSpace (main.c:2179-2182) does with config keys x,y,z (degrees) */
static Quaternion from_cfg(double x, double y, double z)
{
  float e[3];
  Quaternion q;
  e[Z_AXIS] = (float)D2R(z);
  e[Y_AXIS] = (float)D2R(y);
  e[X_AXIS] = (float)D2R(x);
  Quaternion_fromEulerZYX(e, &q);
  return q;
}

static void inv(Quaternion *q, Quaternion *o) { Quaternion_conjugate(q, o); }

/* returns angle in degrees of rotation q, shortest path; axis in out (OpenXR x,y,z) */
static double angle_axis(Quaternion *q, double axis[3])
{
  double w = q->w, x = q->v[0], y = q->v[1], z = q->v[2];
  double n = sqrt(x * x + y * y + z * z);
  if (w < 0) { w = -w; x = -x; y = -y; z = -z; }
  /* Quaternion.v[] is stored [OpenXR z, y, x] (main.c:2170-2173); emit in OpenXR x,y,z order */
  axis[0] = n > 1e-9 ? z / n : 0;
  axis[1] = n > 1e-9 ? y / n : 0;
  axis[2] = n > 1e-9 ? x / n : 0;
  return R2D(2.0 * atan2(n, w));
}

static void rotate_axis(const char *name, double ax, double ay, double az, double deg)
{
  /* which OpenXR axis does each config key rotate about? */
  (void)name; (void)ax; (void)ay; (void)az; (void)deg;
}

int main(void)
{
  double ax[3];
  Quaternion qm = from_cfg(153.86, -22.93, 167.73); /* manual: x=Pitch? y=Yaw z=Roll (GUI labels) */
  Quaternion qc = from_cfg(170.782747, 0.987922, 165.001415); /* capture */
  Quaternion qh = from_cfg(156.4, -19.5, 170.1);   /* scripts/gunman-known-good-offsets.txt */
  Quaternion rel, qi;

  printf("== per-key axis mapping (what rotation each key makes alone, 30 deg) ==\n");
  {
	const char *names[3] = {"x", "y", "z"};
	for (int k = 0; k < 3; k++)
	{
	  double v[3] = {0, 0, 0};
	  Quaternion q;
	  double a;
	  v[k] = 30.0;
	  q = from_cfg(v[0], v[1], v[2]);
	  a = angle_axis(&q, ax);
	  printf("key rotation_%s=30 -> rotates %.2f deg about OpenXR axis (%.2f, %.2f, %.2f)\n",
			 names[k], a, ax[0], ax[1], ax[2]);
	}
  }

  printf("\n== quaternions (OpenXR x,y,z,w) ==\n");
  printf("manual  : %.6f %.6f %.6f %.6f\n", qm.v[2], qm.v[1], qm.v[0], qm.w);
  printf("capture : %.6f %.6f %.6f %.6f\n", qc.v[2], qc.v[1], qc.v[0], qc.w);
  printf("script  : %.6f %.6f %.6f %.6f\n", qh.v[2], qh.v[1], qh.v[0], qh.w);

  printf("\n== relative rotation ==\n");
  inv(&qm, &qi); Quaternion_multiply(&qi, &qc, &rel);
  printf("manual^-1 * capture : %.3f deg axis (%.3f %.3f %.3f)\n", angle_axis(&rel, ax), ax[0], ax[1], ax[2]);
  inv(&qc, &qi); Quaternion_multiply(&qi, &qm, &rel);
  printf("capture^-1 * manual : %.3f deg axis (%.3f %.3f %.3f)\n", angle_axis(&rel, ax), ax[0], ax[1], ax[2]);
  inv(&qm, &qi); Quaternion_multiply(&qc, &qi, &rel);
  printf("capture * manual^-1 : %.3f deg axis (%.3f %.3f %.3f)  [world-frame error]\n", angle_axis(&rel, ax), ax[0], ax[1], ax[2]);
  inv(&qm, &qi); Quaternion_multiply(&qh, &qi, &rel);
  printf("script * manual^-1  : %.3f deg axis (%.3f %.3f %.3f)\n", angle_axis(&rel, ax), ax[0], ax[1], ax[2]);

  printf("\n== Euler conditioning (middle angle is key y; gimbal lock at y=+-90) ==\n");
  {
	double ys[2] = {-22.93, 0.987922};
	for (int i = 0; i < 2; i++)
	  printf("y=%.3f  sin=%.4f  cos=%.4f  worst-case amplification 1/cos = %.4f\n",
			 ys[i], sin(D2R(ys[i])), cos(D2R(ys[i])), 1.0 / cos(D2R(ys[i])));
  }

  printf("\n== round trip fromEuler->toEuler sanity ==\n");
  {
	Quaternion q = from_cfg(153.86, -22.93, 167.73);
	float e[3];
	Quaternion_toEulerZYX(&q, e);
	printf("in x=153.86 y=-22.93 z=167.73 -> out x=%.3f y=%.3f z=%.3f\n",
		   R2D(e[X_AXIS]), R2D(e[Y_AXIS]), R2D(e[Z_AXIS]));
  }
  rotate_axis("", 0, 0, 0, 0);
  return 0;
}
