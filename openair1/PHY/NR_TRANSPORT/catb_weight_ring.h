/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */
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

/* WHAT THE RU ACTUALLY APPLIED, per air slot.
 *
 * The RU combines slot N's data with the weights carried by slot N's C-plane section, which the
 * DU built some slots earlier. The receive path must un-combine with THAT vector, not with the
 * newest one in the ring: y = w_old^H x equalised by h_eff = w_new^H H leaves a residual complex
 * scalar (mostly a phase rotation) that destroys high-order QAM. Reading "the latest record" was
 * a shortcut that discarded the association O-RAN already gives us — a C-plane section is scoped
 * to a slot, so which weights applied to which slot is never ambiguous on the wire.
 *
 * The DU writes this at C-plane BUILD time (oaioran.c catb_bfw_attach) and reads it back in the
 * receive path (nr_ulsch_demodulation.c), so both ends agree by construction.
 * valid==0 means no BFW went out for that slot: the RU then sends antenna 0 alone and the DU
 * must use the same degenerate weight (unit on antenna 0). */
#define CATB_SLOTS_PER_FRAME 20
typedef struct {
  volatile uint32_t seq; /* seqlock: odd = write in progress */
  uint16_t src_frame;    /* vintage: which estimate this vector was derived from */
  uint16_t src_slot;
  uint8_t n_ant;
  uint8_t valid;
  int16_t w[CATB_MAX_ANT * 2]; /* the wideband layer-0 vector actually placed in the ext-1 */
} catb_applied_rec_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  volatile uint32_t write_idx;
  /* PUSCH DMRS symbol bitmap (rel15_ul->ul_dmrs_symb_pos), published by PHY.
   * The C-plane build path lives in the fronthaul layer, which has no PUSCH PDU and therefore
   * cannot know which symbols carry pilots. Hardcoding 2,7,11 was WRONG here: measured dmrs=0 at
   * symbols 2 and 7, i.e. the RU was forwarding 16 antennas on pilot-free symbols while COMBINING
   * the ones that actually carry DMRS — destroying the DU's channel estimate, which is why the
   * published weights came out all zero. */
  volatile uint16_t dmrs_mask;
  uint16_t _pad;
  catb_weight_rec_t rec[CATB_RING_DEPTH];
  catb_applied_rec_t applied[CATB_SLOTS_PER_FRAME];
} catb_weight_ring_t;

/* Record the vector put on the wire for `slot`. Seqlock so the reader never sees a torn vector. */
static inline void catb_applied_write(catb_weight_ring_t *r, int slot, int n_ant,
                                      const int16_t *w, uint16_t src_frame, uint16_t src_slot)
{
  if (r == NULL || slot < 0 || slot >= CATB_SLOTS_PER_FRAME || n_ant <= 0 || n_ant > CATB_MAX_ANT)
    return;
  catb_applied_rec_t *a = &r->applied[slot];
  a->seq++;
  __sync_synchronize();
  a->n_ant = (uint8_t)n_ant;
  a->src_frame = src_frame;
  a->src_slot = src_slot;
  for (int i = 0; i < n_ant * 2; i++)
    a->w[i] = w[i];
  a->valid = 1;
  __sync_synchronize();
  a->seq++;
}

/* Read back what was applied to `slot`. Returns antenna count, 0 if no BFW went out for it. */
static inline int catb_applied_read(const catb_weight_ring_t *r, int slot, int16_t *out, int max_ant)
{
  if (r == NULL || slot < 0 || slot >= CATB_SLOTS_PER_FRAME)
    return 0;
  const catb_applied_rec_t *a = &r->applied[slot];
  for (int attempt = 0; attempt < 4; attempt++) {
    const uint32_t s0 = a->seq;
    if (s0 & 1u)
      continue;
    __sync_synchronize();
    if (!a->valid || a->n_ant == 0)
      return 0;
    const int n = (a->n_ant < max_ant) ? a->n_ant : max_ant;
    for (int i = 0; i < n * 2; i++)
      out[i] = a->w[i];
    __sync_synchronize();
    if (a->seq == s0)
      return n;
  }
  return 0;
}

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
