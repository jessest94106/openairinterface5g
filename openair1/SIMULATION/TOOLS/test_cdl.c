/* Standalone unit test for cdl_model.h — gcc test_cdl.c -lm -o test_cdl && ./test_cdl */
#include <stdio.h>
#include <assert.h>
#include "cdl_model.h"

static double urand(void) { return drand48(); }

static const char *names[3] = {"CDL-A", "CDL-B", "CDL-C"};

int main(void) {
  srand48(4242);
  int fails = 0;

  /* 1. Table 7.5-3 ray offsets are unit-RMS by construction */
  double s2 = 0;
  for (int m = 0; m < CDL_NUM_RAYS; m++)
    s2 += cdl_ray_offset[m] * cdl_ray_offset[m];
  double rms = sqrt(s2 / CDL_NUM_RAYS);
  printf("[1] ray-offset RMS = %.5f (expect 1.0)\n", rms);
  if (fabs(rms - 1.0) > 1e-3) { puts("    FAIL"); fails++; }

  /* 2. Normalized RMS delay spread of each table == 1.0 (spec construction) */
  for (int mod = 0; mod < 3; mod++) {
    const cdl_table_t *t = cdl_get_table(mod);
    double psum = 0, mean = 0, m2 = 0;
    for (int n = 0; n < t->n_clusters; n++) {
      double p = pow(10.0, t->powers_db[n] / 10.0);
      psum += p;
      mean += p * t->delays[n];
      m2 += p * t->delays[n] * t->delays[n];
    }
    mean /= psum;
    m2 /= psum;
    double ds = sqrt(m2 - mean * mean);
    printf("[2] %s: %d clusters, normalized RMS DS = %.4f (expect ~1.0)\n", names[mod], t->n_clusters, ds);
    if (fabs(ds - 1.0) > 0.02) { puts("    FAIL (table transcription suspect)"); fails++; }
  }

  /* 3. E[|tap|^2] == cluster linear power (1/sqrt(M) scaling correct) */
  {
    const int TRIALS = 4000;
    const cdl_table_t *t = cdl_get_table(0);
    double amp = pow(10.0, t->powers_db[1] / 10.0); /* strongest CDL-A cluster */
    double acc = 0;
    for (int k = 0; k < TRIALS; k++) {
      cdl_state_t st;
      cdl_init(&st, 0, 8, 1, 0.5, 0.5, 0.0, NULL, 0, urand);
      double h[2];
      cdl_gen_tap(&st, 1, 3, 0, amp, h);
      acc += h[0] * h[0] + h[1] * h[1];
    }
    double ratio = (acc / TRIALS) / amp;
    printf("[3] E[|tap|^2]/P_cluster = %.3f (expect 1.0 +/- 0.05)\n", ratio);
    if (fabs(ratio - 1.0) > 0.05) { puts("    FAIL"); fails++; }
  }

  /* 4. Spatial correlation across antennas matches the closed form
        E[h_a h_b*] = (P/M) * sum_m exp(j*2pi*d*(a-b)*sin(zoa_m)*sin(aoa_m))
        for one frozen angle-coupling (expectation over ray phases only). */
  {
    const int TRIALS = 20000;
    cdl_state_t st;
    srand48(7);
    cdl_init(&st, 0, 8, 1, 0.5, 0.5, 0.0, NULL, 0, urand);
    cdl_state_t frozen = st; /* keep angles; re-randomize phases each trial */
    int n = 1, a = 0, b = 3;
    double amp = pow(10.0, cdl_get_table(0)->powers_db[n] / 10.0);
    double er = 0, ei = 0;
    for (int k = 0; k < TRIALS; k++) {
      for (int m = 0; m < CDL_NUM_RAYS; m++)
        frozen.ray_phase[n][m] = 2.0 * M_PI * urand();
      double ha[2], hb[2];
      cdl_gen_tap(&frozen, n, a, 0, amp, ha);
      cdl_gen_tap(&frozen, n, b, 0, amp, hb);
      er += ha[0] * hb[0] + ha[1] * hb[1]; /* Re(ha * conj(hb)) */
      ei += ha[1] * hb[0] - ha[0] * hb[1]; /* Im */
    }
    er /= TRIALS; ei /= TRIALS;
    double tr = 0, ti = 0;
    const double d2r = M_PI / 180.0;
    for (int m = 0; m < CDL_NUM_RAYS; m++) {
      double ph = 2.0 * M_PI * 0.5 * (a - b) * sin(frozen.ray_zoa[n][m] * d2r) * sin(frozen.ray_aoa[n][m] * d2r);
      tr += cos(ph); ti += sin(ph);
    }
    tr *= amp / CDL_NUM_RAYS; ti *= amp / CDL_NUM_RAYS;
    printf("[4] E[h0 h3*] empirical (%.4f,%.4f) vs theory (%.4f,%.4f)\n", er, ei, tr, ti);
    if (hypot(er - tr, ei - ti) > 0.05 * hypot(tr, ti) + 0.01 * amp) { puts("    FAIL"); fails++; }
  }

  /* 5. Inter-UE correlation falls with azimuth separation (geometry-controlled) */
  {
    const int TRIALS = 300;
    double seps[4] = {0.0, 10.0, 30.0, 90.0};
    printf("[5] inter-UE |corr|^2 vs azimuth separation (CDL-A, 8 rx, f=0 wideband):\n");
    double first = -1, last = -1;
    for (int s = 0; s < 4; s++) {
      double acc = 0;
      for (int k = 0; k < TRIALS; k++) {
        cdl_state_t u1, u2;
        cdl_init(&u1, 0, 8, 1, 0.5, 0.5, 0.0, NULL, 0, urand);
        cdl_init(&u2, 0, 8, 1, 0.5, 0.5, seps[s], NULL, 0, urand);
        const cdl_table_t *t = cdl_get_table(0);
        double psum = 0;
        for (int n = 0; n < t->n_clusters; n++)
          psum += pow(10.0, t->powers_db[n] / 10.0);
        double h1[8][2] = {{0}}, h2[8][2] = {{0}};
        for (int n = 0; n < t->n_clusters; n++) {
          double amp = pow(10.0, t->powers_db[n] / 10.0) / psum;
          for (int a = 0; a < 8; a++) {
            double x[2];
            cdl_gen_tap(&u1, n, a, 0, amp, x); h1[a][0] += x[0]; h1[a][1] += x[1];
            cdl_gen_tap(&u2, n, a, 0, amp, x); h2[a][0] += x[0]; h2[a][1] += x[1];
          }
        }
        double dr = 0, di = 0, p1 = 0, p2 = 0;
        for (int a = 0; a < 8; a++) {
          dr += h1[a][0] * h2[a][0] + h1[a][1] * h2[a][1];
          di += h1[a][1] * h2[a][0] - h1[a][0] * h2[a][1];
          p1 += h1[a][0] * h1[a][0] + h1[a][1] * h1[a][1];
          p2 += h2[a][0] * h2[a][0] + h2[a][1] * h2[a][1];
        }
        acc += (dr * dr + di * di) / (p1 * p2);
      }
      double c = acc / TRIALS;
      printf("    dAz=%5.1f deg  |corr|^2 = %.3f\n", seps[s], c);
      if (s == 0) first = c;
      if (s == 3) last = c;
    }
    /* i.i.d. reference for 8 antennas is 1/8 = 0.125; co-located same-geometry
       UEs must exceed it and 90-degree-separated UEs must fall back toward it. */
    printf("    (i.i.d. reference = 1/8 = 0.125)\n");
    if (!(first > last)) { puts("    FAIL: correlation did not fall with separation"); fails++; }
  }

  printf(fails ? "\n== %d FAILURE(S) ==\n" : "\n== ALL PASS ==\n", fails);
  return fails ? 1 : 0;
}
