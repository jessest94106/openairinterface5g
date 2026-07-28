/*
 * Cat-B UL beamforming-weight ring: DU (producer) -> RU (consumer).
 *
 * The DU computes MMSE combining weights from its channel estimates and publishes them
 * here; the RU applies them so that only N_layer streams cross the fronthaul instead of
 * N_ant. Emulates the O-RAN C-plane BFW path — the latency physics is faithful, the
 * protocol encoding is not (see CATB_SRS_RU_MMSE_SCOPE.md).
 *
 * Header-only so the DU (nr-softmodem) and RU (nr-oru) share one definition without a
 * build-system change. Single producer, single consumer, seqlock per record: readers
 * retry while seq is odd or changed, so a torn record is never consumed. No locks in the
 * hot path — this sits inside the DU's per-slot receive path.
 *
 * NOTE: OAI does not form an explicit weight matrix anywhere. nr_ulsch_mmse_2layers()
 * builds the 2x2 Gram H^H*H and applies its inverse to already-matched-filtered data, so
 * W is never materialised. The DU-side producer therefore COMPUTES W, it does not merely
 * forward something that already exists.
 */
#ifndef CATB_WEIGHT_RING_H
#define CATB_WEIGHT_RING_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define CATB_RING_NAME    "/catb_weights"
#define CATB_RING_MAGIC   0x43415442u /* "CATB" */
#define CATB_RING_VERSION 1u
#define CATB_MAX_PRB      273
#define CATB_MAX_ANT      16
#define CATB_MAX_LAYERS   2
#define CATB_RING_DEPTH   8 /* deep enough for the largest loop delay we sweep in slots */

typedef struct {
  volatile uint32_t seq; /* seqlock: odd = write in progress */
  uint16_t frame;
  uint16_t slot; /* the slot the ESTIMATE came from — the RU logs age against this */
  uint16_t rnti;
  uint16_t rb_start;
  uint16_t n_prb;
  uint8_t n_ant;
  uint8_t n_layers;
  /* w[prb][layer][ant] as interleaved int16 I,Q (Q15). Per-PRB, not wideband: MMSE weights
   * are frequency-selective and a wideband set would silently flatten that. */
  int16_t w[CATB_MAX_PRB * CATB_MAX_LAYERS * CATB_MAX_ANT * 2];
} catb_weight_rec_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  volatile uint32_t write_idx;
  uint32_t _pad;
  catb_weight_rec_t rec[CATB_RING_DEPTH];
} catb_weight_ring_t;

static inline size_t catb_ring_bytes(void)
{
  return sizeof(catb_weight_ring_t);
}

/* create=1 for the producer (DU), 0 for the consumer (RU). NULL on failure — callers must
 * degrade to today's behaviour rather than abort: this path is diagnostic/experimental. */
static inline catb_weight_ring_t *catb_ring_open(int create)
{
  const int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
  int fd = shm_open(CATB_RING_NAME, flags, 0666);
  if (fd < 0)
    return NULL;
  if (create && ftruncate(fd, (off_t)catb_ring_bytes()) != 0) {
    close(fd);
    return NULL;
  }
  void *p = mmap(NULL, catb_ring_bytes(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (p == MAP_FAILED)
    return NULL;
  catb_weight_ring_t *r = (catb_weight_ring_t *)p;
  if (create) {
    r->magic = CATB_RING_MAGIC;
    r->version = CATB_RING_VERSION;
    r->write_idx = 0;
  } else if (r->magic != CATB_RING_MAGIC || r->version != CATB_RING_VERSION) {
    munmap(p, catb_ring_bytes());
    return NULL;
  }
  return r;
}

/* Producer: claim the next slot, fill it, then commit. */
static inline catb_weight_rec_t *catb_ring_begin(catb_weight_ring_t *r)
{
  catb_weight_rec_t *rec = &r->rec[r->write_idx % CATB_RING_DEPTH];
  __atomic_add_fetch(&rec->seq, 1, __ATOMIC_ACQ_REL); /* -> odd, write in progress */
  return rec;
}

static inline void catb_ring_commit(catb_weight_ring_t *r, catb_weight_rec_t *rec)
{
  __atomic_add_fetch(&rec->seq, 1, __ATOMIC_ACQ_REL); /* -> even, stable */
  __atomic_add_fetch(&r->write_idx, 1, __ATOMIC_ACQ_REL);
}

/* Consumer: copy the record `age_back` publications behind the newest (age_back=0 = newest).
 * That is how VRTSIM_BFW_DELAY_SLOTS is realised — the RU deliberately reads a stale set.
 * Returns 1 on success, 0 if nothing valid is available. */
static inline int catb_ring_read(const catb_weight_ring_t *r, unsigned age_back, catb_weight_rec_t *out)
{
  const uint32_t widx = __atomic_load_n(&r->write_idx, __ATOMIC_ACQUIRE);
  if (widx == 0 || age_back >= CATB_RING_DEPTH || age_back >= widx)
    return 0;
  const catb_weight_rec_t *rec = &r->rec[(widx - 1 - age_back) % CATB_RING_DEPTH];
  for (int attempt = 0; attempt < 4; attempt++) {
    const uint32_t s0 = __atomic_load_n(&rec->seq, __ATOMIC_ACQUIRE);
    if (s0 & 1u)
      continue; /* being written */
    memcpy(out, rec, sizeof(*out));
    const uint32_t s1 = __atomic_load_n(&rec->seq, __ATOMIC_ACQUIRE);
    if (s0 == s1)
      return 1; /* stable across the copy */
  }
  return 0;
}

static inline size_t catb_w_index(int prb, int layer, int ant, int n_layers, int n_ant)
{
  return ((size_t)prb * n_layers + layer) * n_ant + ant;
}

#endif /* CATB_WEIGHT_RING_H */
