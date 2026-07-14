/*
 * 3GPP TR 38.901 CDL channel model core (CDL-A/B/C, NLOS).
 *
 * Self-contained (libm only) so the same code compiles in random_channel.c and in a
 * standalone unit test. Cluster tables transcribed from TR 38.901 Tables 7.7.1-1..3
 * (values cross-checked against NVlabs/sionna tr38901 model files).
 *
 * Model per 38.901 §7.7.1: each cluster n splits into M=20 rays at fixed offset
 * angles (Table 7.5-3, unit RMS) scaled by the per-model cluster angular spreads
 * (c_ASD/c_ASA/c_ZSD/c_ZSA); ray offsets are randomly coupled between angle
 * dimensions (§7.5 step 8); each (cluster, ray) gets a random initial phase.
 * Antenna arrays are ULAs; the element phase for ray (n,m) at RX element a is
 * 2*pi*d_lambda*a*sin(ZoA_nm)*sin(AoA_nm)  (array along y, azimuth from x-axis).
 *
 * Deliberate v1 simplifications (documented, not silent):
 *  - single polarization (no XPR term): lab antennas are single-pol
 *  - static channel: ray phases frozen after init (per-ray Doppler is phase 2;
 *    ray angles are kept in the state so phase rotation can be added)
 *  - per-UE placement = azimuth rotation of all cluster AoA/AoD (38.901 §7.7.5.1
 *    angle translation preserves the spreads)
 */
#ifndef CDL_MODEL_H
#define CDL_MODEL_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CDL_NUM_RAYS 20

/* TR 38.901 Table 7.5-3: ray offset angles within a cluster, unit RMS. */
static const double cdl_ray_offset[CDL_NUM_RAYS] = {
    0.0447,  -0.0447, 0.1413,  -0.1413, 0.2492,  -0.2492, 0.3715,  -0.3715, 0.5129,  -0.5129,
    0.6797,  -0.6797, 0.8844,  -0.8844, 1.1481,  -1.1481, 1.5195,  -1.5195, 2.1551,  -2.1551};

typedef struct {
  int n_clusters;
  double c_asd, c_asa, c_zsd, c_zsa; /* per-cluster rms angle spreads, degrees */
  const double *delays;              /* normalized (unit rms delay spread) */
  const double *powers_db;
  const double *aod, *aoa, *zod, *zoa; /* cluster angles, degrees */
} cdl_table_t;

/* ---- TR 38.901 Table 7.7.1-1: CDL-A (23 clusters, NLOS) ---- */
static const double cdl_a_delays[] = {0.0,    0.3819, 0.4025, 0.5868, 0.4610, 0.5375, 0.6708, 0.5750,
                                      0.7618, 1.5375, 1.8978, 2.2242, 2.1718, 2.4942, 2.5119, 3.0582,
                                      4.0810, 4.4579, 4.5695, 4.7966, 5.0066, 5.3043, 9.6586};
static const double cdl_a_powers[] = {-13.4, 0.0,   -2.2,  -4.0,  -6.0,  -8.2,  -9.9,  -10.5,
                                      -7.5,  -15.9, -6.6,  -16.7, -12.4, -15.2, -10.8, -11.3,
                                      -12.7, -16.2, -18.3, -18.9, -16.6, -19.9, -29.7};
static const double cdl_a_aod[] = {-178.1, -4.2,   -4.2,   -4.2,   90.2,   90.2,  90.2,  121.5,
                                   -81.7,  158.4,  -83.0,  134.8,  -153.0, -172.0, -129.9, -136.0,
                                   165.4,  148.4,  132.7,  -118.6, -154.1, 126.5,  -56.2};
static const double cdl_a_aoa[] = {51.3,  -152.7, -152.7, -152.7, 76.6,  76.6,   76.6,  -1.8,
                                   -41.9, 94.2,   51.9,   -115.9, 26.6,  76.6,   -7.0,  -23.0,
                                   -47.2, 110.4,  144.5,  155.3,  102.0, -151.8, 55.2};
static const double cdl_a_zod[] = {50.2,  93.2,  93.2,  93.2,  122.0, 122.0, 122.0, 150.2,
                                   55.2,  26.4,  126.4, 171.6, 151.4, 157.2, 47.2,  40.4,
                                   43.3,  161.8, 10.8,  16.7,  171.7, 22.7,  144.9};
static const double cdl_a_zoa[] = {125.4, 91.3,  91.3,  91.3,  94.0,  94.0,  94.0,  47.1,
                                   56.0,  30.1,  58.8,  26.0,  49.2,  143.1, 117.4, 122.7,
                                   123.2, 32.6,  27.2,  15.2,  146.0, 150.7, 156.1};

/* ---- TR 38.901 Table 7.7.1-2: CDL-B (23 clusters, NLOS) ---- */
static const double cdl_b_delays[] = {0.0000, 0.1072, 0.2155, 0.2095, 0.2870, 0.2986, 0.3752, 0.5055,
                                      0.3681, 0.3697, 0.5700, 0.5283, 1.1021, 1.2756, 1.5474, 1.7842,
                                      2.0169, 2.8294, 3.0219, 3.6187, 4.1067, 4.2790, 4.7834};
static const double cdl_b_powers[] = {0.0,  -2.2, -4.0, -3.2, -9.8,  -1.2, -3.4,  -5.2,
                                      -7.6, -3.0, -8.9, -9.0, -4.8,  -5.7, -7.5,  -1.9,
                                      -7.6, -12.2, -9.8, -11.4, -14.9, -9.2, -11.3};
static const double cdl_b_aod[] = {9.3,   9.3,   9.3,   -34.1, -65.4, -11.4, -11.4, -11.4,
                                   -67.2, 52.5,  -72.0, 74.3,  -52.2, -50.5, 61.4,  30.6,
                                   -72.5, -90.6, -77.6, -82.6, -103.6, 75.6, -77.6};
static const double cdl_b_aoa[] = {-173.3, -173.3, -173.3, 125.5, -88.0, 155.1, 155.1, 155.1,
                                   -89.8,  132.1,  -83.6,  95.3,  103.7, -87.8, -92.5, -139.1,
                                   -90.6,  58.6,   -79.0,  65.8,  52.7,  88.7,  -60.4};
static const double cdl_b_zod[] = {105.8, 105.8, 105.8, 115.3, 119.3, 103.2, 103.2, 103.2,
                                   118.2, 102.0, 100.4, 98.3,  103.4, 102.5, 101.4, 103.0,
                                   100.0, 115.2, 100.5, 119.6, 118.7, 117.8, 115.7};
static const double cdl_b_zoa[] = {78.9, 78.9, 78.9, 63.3, 59.9, 67.5, 67.5, 67.5,
                                   82.6, 66.3, 61.6, 58.0, 78.2, 82.0, 62.4, 78.0,
                                   60.9, 82.9, 60.8, 57.3, 59.9, 60.1, 62.3};

/* ---- TR 38.901 Table 7.7.1-3: CDL-C (24 clusters, NLOS) ---- */
static const double cdl_c_delays[] = {0.0,    0.2099, 0.2219, 0.2329, 0.2176, 0.6366, 0.6448, 0.6560,
                                      0.6584, 0.7935, 0.8213, 0.9336, 1.2285, 1.3083, 2.1704, 2.7105,
                                      4.2589, 4.6003, 5.4902, 5.6077, 6.3065, 6.6374, 7.0427, 8.6523};
static const double cdl_c_powers[] = {-4.4,  -1.2,  -3.5,  -5.2,  -2.5,  0.0,   -2.2,  -3.9,
                                      -7.4,  -7.1,  -10.7, -11.1, -5.1,  -6.8,  -8.7,  -13.2,
                                      -13.9, -13.9, -15.8, -17.1, -16.0, -15.7, -21.6, -22.8};
static const double cdl_c_aod[] = {-46.6, -22.8, -22.8, -22.8, -40.7, 0.3,   0.3,   0.3,
                                   73.1,  -64.5, 80.2,  -97.1, -55.3, -64.3, -78.5, 102.7,
                                   99.2,  88.8,  -101.9, 92.2, 93.3,  106.6, 119.5, -123.8};
static const double cdl_c_aoa[] = {-101.0, 120.0, 120.0, 120.0, -127.5, 170.4, 170.4, 170.4,
                                   55.4,   66.5,  -48.1, 46.9,  68.1,   -68.7, 81.5,  30.7,
                                   -16.4,  3.8,   -13.7, 9.7,   5.6,    0.7,   -21.9, 33.6};
static const double cdl_c_zod[] = {97.2,  98.6,  98.6,  98.6,  100.6, 99.2,  99.2,  99.2,
                                   105.2, 95.3,  106.1, 93.5,  103.7, 104.2, 93.0,  104.2,
                                   94.9,  93.1,  92.2,  106.7, 93.0,  92.9,  105.2, 107.8};
static const double cdl_c_zoa[] = {87.6,  72.1,  72.1,  72.1,  70.1, 75.3,  75.3,  75.3,
                                   67.4,  63.8,  71.4,  60.5,  90.6, 60.1,  61.0,  100.7,
                                   62.3,  66.7,  52.9,  61.8,  51.9, 61.7,  58.0,  57.0};

static const cdl_table_t cdl_tables[3] = {
    {23, 5.0, 11.0, 3.0, 3.0, cdl_a_delays, cdl_a_powers, cdl_a_aod, cdl_a_aoa, cdl_a_zod, cdl_a_zoa},
    {23, 10.0, 22.0, 3.0, 7.0, cdl_b_delays, cdl_b_powers, cdl_b_aod, cdl_b_aoa, cdl_b_zod, cdl_b_zoa},
    {24, 2.0, 15.0, 3.0, 7.0, cdl_c_delays, cdl_c_powers, cdl_c_aod, cdl_c_aoa, cdl_c_zod, cdl_c_zoa},
};

#define CDL_MAX_CLUSTERS 24

/* Per-descriptor CDL state: everything needed to (re)generate tap coefficients. */
typedef struct {
  int model;      /* 0=A 1=B 2=C */
  int n_clusters;
  int nb_rx, nb_tx;
  double rx_spacing_lambda, tx_spacing_lambda;
  double az_rot_deg; /* per-UE azimuth rotation applied to all AoA/AoD */
  /* per (cluster, ray): coupled angles in degrees and random initial phase */
  double ray_aoa[CDL_MAX_CLUSTERS][CDL_NUM_RAYS];
  double ray_aod[CDL_MAX_CLUSTERS][CDL_NUM_RAYS];
  double ray_zoa[CDL_MAX_CLUSTERS][CDL_NUM_RAYS];
  double ray_zod[CDL_MAX_CLUSTERS][CDL_NUM_RAYS];
  double ray_phase[CDL_MAX_CLUSTERS][CDL_NUM_RAYS]; /* radians */
} cdl_state_t;

static inline const cdl_table_t *cdl_get_table(int model) {
  return &cdl_tables[model];
}

/* Power-ranked cluster selection: fill keep_idx with the indices of the max_n strongest
 * clusters (all if max_n >= n_clusters). Rationale: the vrtsim sparse conv budget at
 * 122.88 Msps x 8 antennas was tuned on the 12-tap TS 38.104 TDL tables — 3GPP's own
 * power-pruned reduction of these same profiles. Returns the kept count. */
static inline int cdl_select_clusters(int model, int max_n, int *keep_idx) {
  const cdl_table_t *t = cdl_get_table(model);
  int n = t->n_clusters;
  int order[CDL_MAX_CLUSTERS];
  for (int i = 0; i < n; i++)
    order[i] = i;
  for (int i = 0; i < n; i++) /* selection sort by power desc — n<=24, cost irrelevant */
    for (int j = i + 1; j < n; j++)
      if (t->powers_db[order[j]] > t->powers_db[order[i]]) {
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
  int keep = (max_n > 0 && max_n < n) ? max_n : n;
  for (int i = 0; i < keep; i++)
    keep_idx[i] = order[i];
  return keep;
}

/* Fisher-Yates shuffle of ray indices using caller-supplied uniform(0,1) RNG. */
static inline void cdl_shuffle(int *idx, int n, double (*urand)(void)) {
  for (int i = 0; i < n; i++)
    idx[i] = i;
  for (int i = n - 1; i > 0; i--) {
    int j = (int)(urand() * (i + 1));
    if (j > i)
      j = i;
    int t = idx[i];
    idx[i] = idx[j];
    idx[j] = t;
  }
}

/* Initialize CDL state: couple ray offsets randomly per 38.901 §7.5 step 8 and
 * draw the per-ray initial phases. urand must return uniform (0,1); the caller
 * seeds it per UE so different UEs get independent realizations. */
static inline void cdl_init(cdl_state_t *st,
                            int model,
                            int nb_rx,
                            int nb_tx,
                            double rx_spacing_lambda,
                            double tx_spacing_lambda,
                            double az_rot_deg,
                            const int *keep_idx, /* NULL = all clusters in table order */
                            int keep_n,
                            double (*urand)(void)) {
  memset(st, 0, sizeof(*st));
  const cdl_table_t *t = cdl_get_table(model);
  st->model = model;
  st->n_clusters = (keep_idx != NULL) ? keep_n : t->n_clusters;
  st->nb_rx = nb_rx;
  st->nb_tx = nb_tx;
  st->rx_spacing_lambda = rx_spacing_lambda;
  st->tx_spacing_lambda = tx_spacing_lambda;
  st->az_rot_deg = az_rot_deg;
  for (int n = 0; n < st->n_clusters; n++) {
    int src = (keep_idx != NULL) ? keep_idx[n] : n;
    int perm_aoa[CDL_NUM_RAYS], perm_aod[CDL_NUM_RAYS], perm_zoa[CDL_NUM_RAYS], perm_zod[CDL_NUM_RAYS];
    cdl_shuffle(perm_aoa, CDL_NUM_RAYS, urand);
    cdl_shuffle(perm_aod, CDL_NUM_RAYS, urand);
    cdl_shuffle(perm_zoa, CDL_NUM_RAYS, urand);
    cdl_shuffle(perm_zod, CDL_NUM_RAYS, urand);
    for (int m = 0; m < CDL_NUM_RAYS; m++) {
      st->ray_aoa[n][m] = t->aoa[src] + t->c_asa * cdl_ray_offset[perm_aoa[m]] + az_rot_deg;
      st->ray_aod[n][m] = t->aod[src] + t->c_asd * cdl_ray_offset[perm_aod[m]] + az_rot_deg;
      st->ray_zoa[n][m] = t->zoa[src] + t->c_zsa * cdl_ray_offset[perm_zoa[m]];
      st->ray_zod[n][m] = t->zod[src] + t->c_zsd * cdl_ray_offset[perm_zod[m]];
      st->ray_phase[n][m] = 2.0 * M_PI * urand();
    }
  }
}

/* Tap coefficient for cluster n between TX element aatx and RX element aarx.
 * amp_lin is the cluster's normalized LINEAR power (sum over clusters = 1).
 * Output: out[0]=re, out[1]=im. E[|h|^2] over phase draws = amp_lin. */
static inline void cdl_gen_tap(const cdl_state_t *st, int n, int aarx, int aatx, double amp_lin, double out[2]) {
  const double d2r = M_PI / 180.0;
  double re = 0.0, im = 0.0;
  for (int m = 0; m < CDL_NUM_RAYS; m++) {
    double ph = st->ray_phase[n][m];
    ph += 2.0 * M_PI * st->rx_spacing_lambda * aarx * sin(st->ray_zoa[n][m] * d2r) * sin(st->ray_aoa[n][m] * d2r);
    ph += 2.0 * M_PI * st->tx_spacing_lambda * aatx * sin(st->ray_zod[n][m] * d2r) * sin(st->ray_aod[n][m] * d2r);
    re += cos(ph);
    im += sin(ph);
  }
  const double scale = sqrt(amp_lin / CDL_NUM_RAYS);
  out[0] = re * scale;
  out[1] = im * scale;
}

#endif /* CDL_MODEL_H */
