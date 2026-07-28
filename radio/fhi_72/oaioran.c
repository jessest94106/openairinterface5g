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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
#include "xran_fh_o_du.h"
#include "xran_compression.h"
#include "armral_bfp_compression.h"
#ifndef RU_RX_SLOT_DEPTH
#define RU_RX_SLOT_DEPTH 8 // MUST match openair1/PHY/defs_RU.h (L1 rxdataF ring depth)
#endif
#include "oai_bfp_compression.h"

#if defined(__arm__) || defined(__aarch64__)
#else
// xran_cp_api.h uses SIMD, but does not include it
#include <immintrin.h>
#endif
#include "xran_cp_api.h"
#include "xran_sync_api.h"
#include "oran_isolate.h"
#include "oran-init.h"
#include "oaioran.h"
#include <rte_ethdev.h>

#include "oran-config.h" // for g_kbar

#include "common/utils/threadPool/notified_fifo.h"

#define N_SC_PER_PRB 12

#if OAI_FHI72_USE_POLLING
#define USE_POLLING
#endif

// Declare variable useful for the send buffer function
volatile bool first_call_set = false;

int xran_is_prach_slot(uint8_t PortId, uint32_t subframe_id, uint32_t slot_id);
#include "common/utils/LOG/log.h"

#include "openair1/PHY/NR_TRANSPORT/catb_weight_ring.h"

// ---- Cat-B STEP 3a.1: UL C-plane beamforming weights (emission only) --------------------
// Weights are computed in PHY (nr_ulsch_demodulation.c) and handed here through the shm ring,
// which is an INTRA-DU handoff between layers — the DU->RU transport is the C-plane below.
// Wideband for 3a: numSetBFWs=1 => one section, which is all XRAN_MAX_SET_BFWS(=1) allows and
// all the latency experiment needs (staleness is the variable; granularity is not).
#define CATB_BFW_ANT 16
#define CATB_BFW_EXTBUF 512 // 16 ant x 4 B + ext-1 header; static, never allocate in this path
static void catb_bfw_attach(struct xran_prb_elm *pRbElm)
{
  static int en = -1;
  static catb_weight_ring_t *ring = NULL;
  if (en < 0) {
    const char *e = getenv("OAI_CATB_BFW");
    en = (e && e[0] && e[0] != '0') ? 1 : 0;
  }
  if (!en || pRbElm == NULL)
    return;
  // RETRY the mapping instead of caching failure. This path first runs while the C-plane is
  // being built for the very first UL slot — long before any UE has attached, and therefore
  // before PHY has created the ring (PHY creates it on its first MU-IRC decode). A one-shot
  // init here found no ring, latched OFF, and the feature silently never ran.
  if (ring == NULL) {
    static long tries = 0;
    if ((tries++ % 2000) != 0)
      return;
    ring = catb_ring_open(0);
    if (ring == NULL)
      return;
    LOG_A(HW, "[CATB] UL C-plane BFW emission ON (ring mapped after %ld attempts)\n", tries);
  }
  static catb_weight_rec_t rec;
  if (!catb_ring_read(ring, 0, &rec) || rec.n_ant == 0)
    return;
  // Wideband: one weight vector across antennas, taken from the mid-band PRB. 3b refines this
  // to per-PRB/bundle; Step 2 measured adjacent-PRB weight correlation at 0.91-0.96, so this
  // gives up real but bounded accuracy.
  static int16_t iq[CATB_BFW_ANT * 2];
  const int nant = (rec.n_ant > CATB_BFW_ANT) ? CATB_BFW_ANT : rec.n_ant;
  const int prb = rec.n_prb / 2;
  for (int a = 0; a < nant; a++) {
    const size_t k = catb_w_index(prb, 0 /*layer 0*/, a, rec.n_layers, rec.n_ant);
    iq[2 * a] = rec.w[2 * k];
    iq[2 * a + 1] = rec.w[2 * k + 1];
  }
  // The ext buffer MUST come from rte_malloc: xran_attach_cp_ext_buf() calls
  // rte_malloc_virt2iova() on p_ext_start and rte_panic()s on a bad IOVA, so a static array
  // would abort the DU rather than fail quietly. p_ext_section must also sit PAST a headroom
  // gap, because xran back-steps by RTE_PKTMBUF_HEADROOM + ecpri + section1 headers to find
  // the mbuf start. Rotating pool: each buffer is attached to an in-flight mbuf and released
  // by xran's free callback, so reusing one buffer for every section would corrupt packets
  // still on the wire.
  // Headroom and total size both generous. xran back-steps from p_ext_section by
  // RTE_PKTMBUF_HEADROOM + ecpri_hdr + section1_header AND extends the claimed mbuf length by
  // the same plus 18 — so the attachment reads and DMAs OUTSIDE the region we sized for the
  // payload alone. Undersizing produced a segfault in libxran at a sign-extended (negative)
  // address from L1_tx_thread. 1 KB head + 4 KB body leaves room for both directions.
  enum { CATB_HEAD = 1024, CATB_BODY = 4096, CATB_POOL = 128 };
  static int8_t *pool[CATB_POOL];
  static int pool_idx = 0;
  static int pool_failed = 0;
  if (pool_failed)
    return;
  if (pool[0] == NULL) {
    for (int i = 0; i < CATB_POOL; i++) {
      pool[i] = rte_malloc(NULL, CATB_HEAD + CATB_BODY, 64);
      if (pool[i]) memset(pool[i], 0, CATB_HEAD + CATB_BODY);
      if (pool[i] == NULL) {
        LOG_E(HW, "[CATB] rte_malloc failed for BFW ext buffer %d — BFW disabled\n", i);
        pool_failed = 1;
        return;
      }
    }
    LOG_A(HW, "[CATB] BFW ext pool: %d x %d B from rte_malloc\n", CATB_POOL, (int)(CATB_HEAD + CATB_BODY));
  }
  // atomic: the C-plane loop runs per antenna per symbol and may be threaded; two sections
  // sharing a buffer while both mbufs are in flight would corrupt packets.
  const int slot_i = __atomic_fetch_add(&pool_idx, 1, __ATOMIC_RELAXED) % CATB_POOL;
  int8_t *const base = pool[slot_i];
  // LAYOUT (xran_cp_proc.c:526-528 + ONE_EXT_LEN/ONE_CPSEC_EXT_LEN in xran_cp_proc.h):
  //   ext1.p_bfwIQ  = p_ext_section + sizeof(section1)
  //   ext1.bfwIQ_sz = ext_section_sz - sizeof(section1)
  // so the buffer must be [ sizeof(section1) bytes reserved ][ populate() output ], and
  // ext_section_sz must COUNT that reserved header. Writing populate's output at offset 0
  // and declaring only its own length made xran read from +8 with size-8 — i.e. from the
  // middle of the ext1 header — which is what killed the DU.
  int8_t *const extbuf = base + CATB_HEAD + sizeof(struct xran_cp_radioapp_section1);
  pRbElm->bf_weight.nAntElmTRx = nant;
  pRbElm->bf_weight.bfwIqWidth = 16; // uncompressed to start; this is the BFW compression knob
  pRbElm->bf_weight.bfwCompMeth = XRAN_BFWCOMPMETHOD_NONE; // BLKSCALE/ULAW/BEAMSPACE rte_panic()
  pRbElm->bf_weight.numSetBFWs = 1;
  pRbElm->bf_weight.numBundPrb = 0; // 0 => ext-1 rather than ext-11
  pRbElm->bf_weight.extType = 1;
  // Required for xran to emit weights rather than a beam index — without it the section stays
  // index-based (XRAN_BEAM_ID_BASED=0) and the extension is never attached.
  pRbElm->BeamFormingType = XRAN_BEAM_WEIGHT;
  // MANDATORY and easy to miss: xran gates ext-1 SECTION PREPARATION on this
  // (xran_cp_proc.c:513 "if((category == XRAN_CATEGORY_B) && (pPrbMapElem->bf_weight_update))")
  // while the ext-buffer ATTACH at :570 is gated only on extType==1. Setting extType without
  // this makes xran attach a buffer whose section content was never prepared — a malformed
  // C-plane packet handed to the NIC, which killed the DU outright in three earlier attempts.
  pRbElm->bf_weight_update = 1;
  pRbElm->bf_weight.maxExtBufSize = CATB_BFW_EXTBUF;
  const int32_t len = xran_cp_populate_section_ext_1(extbuf, CATB_BFW_EXTBUF, iq, pRbElm);
  if (len <= 0) {
    static int warned = 0;
    if (!warned++)
      LOG_E(HW, "[CATB] xran_cp_populate_section_ext_1 returned %d — BFW not attached\n", len);
    return;
  }
  pRbElm->bf_weight.p_ext_start = base; // rte_malloc base — xran takes its IOVA from this
  pRbElm->bf_weight.p_ext_section = base + CATB_HEAD; // section1 slot first, IQ after it
  pRbElm->bf_weight.ext_section_sz = (int16_t)(len + sizeof(struct xran_cp_radioapp_section1));
  // The TX path reads the width/compression from the PRB ELEMENT, not from bf_weight
  // (xran_cp_proc.c:522-523), so setting only the bf_weight copies has no effect.
  pRbElm->iqWidth = 16;
  pRbElm->compMethod = XRAN_BFWCOMPMETHOD_NONE;
  { static long n = 0; if ((n++ % 20000) == 0) LOG_I(HW, "[CATB] BFW attached, ext len %d, %d ant\n", len, nant); }
}
// ----------------------------------------------------------------------------------------

#ifndef USE_POLLING
extern notifiedFIFO_t oran_sync_fifo;
atomic_int xran_queue_length = 0;
#else
volatile oran_sync_info_t oran_sync_info = {0};
#endif

/** @details xran-specific callback, called when all packets for given CC and
 * 1/4, 1/2, 3/4, all symbols of a slot arrived. Currently, only used to get
 * timing information and unblock another thread in xran_fh_rx_read_slot()
 * through either a message queue, or writing in global memory with polling, on
 * a full slot boundary. */
void oai_xran_fh_rx_callback(void *pCallbackTag, xran_status_t status)
{
  struct xran_cb_tag *callback_tag = (struct xran_cb_tag *)pCallbackTag;

  static int32_t last_slot = -1;
  static int32_t last_frame = -1;

  const struct xran_fh_init *fh_init = get_xran_fh_init();
  int num_ports = fh_init->xran_ports;

  /* assuming all RUs have the same numerology */
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  const int slots_in_sf = 1 << fh_cfg->frame_conf.nNumerology;
  const int sf_in_frame = 10;

  static int rx_RU[XRAN_PORTS_NUM][160] = {0};
  uint32_t tti = callback_tag->slotiId;
  uint32_t frame = XranGetFrameNum(tti, 0, sf_in_frame, slots_in_sf);
  uint32_t subframe = XranGetSubFrameNum(tti, slots_in_sf, sf_in_frame);
  uint32_t slot = XranGetSlotNum(tti, slots_in_sf);

  uint32_t rx_sym = callback_tag->symbol & 0xFF;
  uint32_t ru_id = callback_tag->oXuId;

  // LOG_D(HW, "rx_callback at %4d.%3d (subframe %d), rx_sym %d ru_id %d\n", frame, slot, subframe, rx_sym, ru_id);

  if (rx_sym == 7) { // in F release this value is defined as XRAN_FULL_CB_SYM (full slot (offset + 7))
#if defined F_RELEASE
    for (int ru_idx = 0; ru_idx < num_ports; ru_idx++) {
      struct xran_fh_config *fh_config = get_xran_fh_config(ru_idx);
      oran_buf_list_t *bufs = get_xran_buffers(ru_idx);
      for (uint16_t cc_id = 0; cc_id < 1 /* fh_config->nCC */; cc_id++) { // OAI does not support multiple CC yet.
        // dstcp = the UL PRB maps: bound by the UL antenna count (neAxcUl under asymmetric eAxC).
        // Bounding by neAxc (the DL count) left antennas 2..7 un-reset -> their nSecDesc climbed to
        // XRAN_MAX_FRAGMENT -> xran dropped their packets -> DU death. Latent upstream (DL==UL there).
        uint32_t ul_eaxc = fh_config->neAxcUl > 0 ? fh_config->neAxcUl : fh_config->neAxc;
        for(uint32_t ant_id = 0; ant_id < ul_eaxc; ant_id++) {
          struct xran_prb_map *pRbMap = (struct xran_prb_map *)bufs->dstcp[ant_id][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
          AssertFatal(pRbMap != NULL, "(%d:%d:%d)pRbMap == NULL. Aborting.\n", cc_id, tti % XRAN_N_FE_BUF_LEN, ant_id);

          // Reset a buffer HALF A RING ahead, far from both the buffer being
          // filled now (tti) and the one about to be read. Resetting (tti+1) was
          // wiping the next slot's EARLY symbols: the RU streams UL symbol-by-
          // symbol and the next slot's first packets arrive before this mid-slot
          // callback, so (tti+1)'s syms 0-8 were cleared after landing — leaving
          // only the last ~5 symbols (mask=...011111) and forcing every UL TB to
          // need 3 HARQ rounds. With N=20, (tti+10) is clear long before its slot.
          // Reset offset: must land AFTER the buffer's last read (slot B-20, read ~3 slots
          // after its slot) and BEFORE its next fill. The non-realtime vrtsim RU streams UL
          // up to ~10 slots AHEAD of this callback clock (lead is width-dependent: the slow
          // BFP-compress path at iq<=10 delayed emission below the boundary; raw/wide paths
          // don't), so the old N/2 (+10) offset swept buffers WHILE their 2-fragment deposit
          // trains were landing: pair-complete-then-cleared -> whole-symbol loss, mid-pair ->
          // one fragment randomly wiped (50/50) -> DMRS chest collapse (log2h 4) -> LDPC
          // abort chains -> the iq_width>=12 throughput crater. +14 keeps 3-4 slots of read
          // margin and tolerates a streaming lead up to 14 slots. [RSTLIVE] counts residual
          // collisions (expect 0). Env-tunable for experiments.
          static int rst_off = -1;
          if (rst_off < 0) {
            const char *e_ro = getenv("OAI_FH_RESET_OFFSET");
            rst_off = (e_ro && e_ro[0]) ? atoi(e_ro) : (XRAN_N_FE_BUF_LEN - 6);
          }
          uint32_t next_buf = (tti + rst_off) % XRAN_N_FE_BUF_LEN;
          struct xran_prb_map *pNextRbMap = (struct xran_prb_map *)bufs->dstcp[ant_id][next_buf].pBuffers->pData;
          if (pNextRbMap != NULL) {
            for (uint32_t sym_id = 0; sym_id < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_id++) {
              for (uint32_t idxElm = 0; idxElm < pNextRbMap->nPrbElm; idxElm++) {
                // Investigation probe: nSecDesc==1 here = we are clearing MID-PAIR (a
                // fragment train is depositing into this buffer RIGHT NOW) = collision.
                uint16_t ns_at_reset = pNextRbMap->prbMap[idxElm].nSecDesc[sym_id];
                if (ns_at_reset == 1) {
                  static _Atomic long rst_midpair = 0;
                  long c = ++rst_midpair;
                  if (c <= 8 || (c % 500) == 0)
                    printf("[RSTLIVE] mid-pair clear #%ld cb_tti %u target_buf %u ant %u sym %u\n",
                           c, tti, next_buf, ant_id, sym_id);
                }
                pNextRbMap->prbMap[idxElm].nSecDesc[sym_id] = 0;
              }
            }
          }
        }
      }
    }
#endif
    // if xran did not call xran_physide_dl_tti callback, it's not ready yet.
    // wait till first callback to advance counters, because otherwise users
    // would see periodic output with only "0" in stats counters
    if (!first_call_set)
      return;
    uint32_t slot2 = slot + (subframe * slots_in_sf);
    rx_RU[ru_id][slot2] = 1;
    if (last_frame > 0 && frame > 0
        && ((slot2 > 0 && last_frame != frame) || (slot2 == 0 && last_frame != ((1024 + frame - 1) & 1023))))
      LOG_E(HW, "Jump in frame counter last_frame %d => %d, slot %d\n", last_frame, frame, slot2);
    for (int i = 0; i < num_ports; i++) {
      if (rx_RU[i][slot2] == 0)
        return;
    }
    for (int i = 0; i < num_ports; i++)
      rx_RU[i][slot2] = 0;

    if (last_slot == -1 || slot2 != last_slot) {
#ifndef USE_POLLING
      notifiedFIFO_elt_t *req = newNotifiedFIFO_elt(sizeof(oran_sync_info_t), 0, &oran_sync_fifo, NULL);
      oran_sync_info_t *info = NotifiedFifoData(req);
      info->tti = tti;
      info->sl = slot2;
      info->f = frame;
      // LOG_D(HW, "Push %d.%d.%d (slot %d, subframe %d,last_slot %d)\n", frame, info->sl, slot, ru_id, subframe, last_slot);
      atomic_fetch_add(&xran_queue_length, 1);
      pushNotifiedFIFO(&oran_sync_fifo, req);
#else
      LOG_D(HW, "Writing %d.%d.%d (slot %d, subframe %d,last_slot %d)\n", frame, slot2, ru_id, slot, subframe, last_slot);
      oran_sync_info.tti = tti;
      oran_sync_info.sl = slot2;
      oran_sync_info.f = frame;
#endif
    } else
      LOG_E(HW, "Cannot Push %d.%d.%d (slot %d, subframe %d,last_slot %d)\n", frame, slot2, ru_id, slot, subframe, last_slot);
    last_slot = slot2;
    last_frame = frame;
  } // rx_sym == 7
}

/** @details Only used to unblock timing in oai_xran_fh_rx_callback() on first
 * call. */
int oai_physide_dl_tti_call_back(void *param)
{
#ifdef GNB_FHI_TIMING_DEBUG
  if (!first_call_set)
    LOG_I(HW, "first_call set from phy cb\n");
#endif
  first_call_set = true;
  return 0;
}

/** @brief Reads PRACH data from xran buffers.
 *
 * @details Reads PRACH data from xran-specific buffers and, if I/Q compression
 * (bitwidth < 16 bits) is configured, uncompresses the data. Places PRACH data
 * in OAI buffer. */
#define GNB_PRACH_UPLANE_DEBUG 1
static int read_prach_data(ru_info_t *ru, int frame, int slot)
{
  /* calculate tti and subframe_id from frame, slot num */
  int sym_idx = 0;

  struct xran_fh_init *fh_init = get_xran_fh_init();
  struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  nr_prach_info_t prach_info = get_prach_info(0);

  int prach_start_sym = prach_info.start_symbol;
  int prach_end_sym = prach_info.N_dur + prach_start_sym;
  struct xran_ru_config *ru_conf = &fh_cfg->ru_conf;
  int slots_per_frame = 10 << fh_cfg->frame_conf.nNumerology;
  int slots_per_subframe = 1 << fh_cfg->frame_conf.nNumerology;

  int tti = slots_per_frame * (frame) + (slot);
  uint32_t subframe = slot / slots_per_subframe;
  // PRACH occasion in a frame if and only if SFN % x == y, TS 38.211 Table 6.3.3.2-2/3/4
  uint32_t is_prach_frame = (frame % prach_info.x == prach_info.y);
  uint32_t is_prach_slot = is_prach_frame && xran_is_prach_slot(0, subframe, (slot % slots_per_subframe));

  int nb_rx_per_ru = ru->nb_rx / fh_init->xran_ports;
  /* If it is PRACH slot, copy prach IQ from XRAN PRACH buffer to OAI PRACH buffer */
  if (is_prach_slot) {
    printf("[PRACHPAIR FILL] f=%d s=%d buf=%p\n", frame, slot, (void *)ru->prach_buf);
    if (!ru->prach_buf) {
      LOG_W(HW, "we get rach data from ru, but it is not scheduled %d.%d\n", frame, slot);
      return -1;
    }
    for (sym_idx = prach_start_sym; sym_idx < prach_end_sym; sym_idx++) {
      for (int aa = 0; aa < ru->nb_rx; aa++) {
        int16_t *dst, *src;
        int idx = 0;
        oran_buf_list_t *bufs = get_xran_buffers(aa / nb_rx_per_ru);
        // hardcoded to use only first prach occasion
        dst = (int16_t *)ru->prach_buf[0][aa];
        src = (int16_t *)bufs->prachdstdecomp[aa % nb_rx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers[sym_idx].pData;
        /* convert Network order to host order */
        if (ru_conf->compMeth_PRACH == XRAN_COMPMETHOD_NONE) {
          int src_nonzero_full = 0;
          int src_nonzero_extract = 0;
          int src_min = 0;
          int src_max = 0;
          for (idx = 0; idx < 12 * 2 * N_SC_PER_PRB; idx++) {
            int16_t val = (int16_t)ntohs(src[idx]);
            if (val != 0) {
              src_nonzero_full++;
              if (val < src_min)
                src_min = val;
              if (val > src_max)
                src_max = val;
            }
          }
          for (idx = 0; idx < 139 * 2; idx++) {
            if ((int16_t)ntohs(src[idx + g_kbar]) != 0)
              src_nonzero_extract++;
          }
          if (sym_idx == prach_start_sym) {
            for (idx = 0; idx < 139 * 2; idx++) {
              dst[idx] = ((int16_t)ntohs(src[idx + g_kbar]));
            }
          } else {
            for (idx = 0; idx < 139 * 2; idx++) {
              dst[idx] += ((int16_t)ntohs(src[idx + g_kbar]));
            }
          }
          int dst_nonzero = 0;
          int dst_min = 0;
          int dst_max = 0;
          for (idx = 0; idx < 139 * 2; idx++) {
            if (dst[idx] != 0) {
              dst_nonzero++;
              if (dst[idx] < dst_min)
                dst_min = dst[idx];
              if (dst[idx] > dst_max)
                dst_max = dst[idx];
            }
          }
          static int prach_uncomp_log_count = 0;
#ifdef GNB_PRACH_UPLANE_DEBUG
          bool should_log_uncomp = prach_uncomp_log_count < 32 || src_nonzero_full > 0 || dst_nonzero > 0;
#else
          bool should_log_uncomp = false;
#endif
          if (should_log_uncomp) {
            LOG_A(HW,
                  "[gNB PRACH RX UNCOMP] frame=%d slot=%d sym=%d aa=%d tti=%d g_kbar=%d src_nonzero_full=%d/288 "
                  "src_nonzero_extract=%d/278 src_min=%d src_max=%d dst_nonzero=%d/278 dst_min=%d dst_max=%d\n",
                  frame,
                  slot,
                  sym_idx,
                  aa,
                  tti,
                  g_kbar,
                  src_nonzero_full,
                  src_nonzero_extract,
                  src_min,
                  src_max,
                  dst_nonzero,
                  dst_min,
                  dst_max);
            LOG_A(HW,
                  "[gNB PRACH RX UNCOMP] src[0]=%d src[g]=%d src[g+1]=%d src[g+100]=%d src[g+277]=%d "
                  "dst[0]=%d dst[1]=%d dst[100]=%d dst[277]=%d\n",
                  (int16_t)ntohs(src[0]),
                  (int16_t)ntohs(src[g_kbar]),
                  (int16_t)ntohs(src[g_kbar + 1]),
                  (int16_t)ntohs(src[g_kbar + 100]),
                  (int16_t)ntohs(src[g_kbar + 277]),
                  dst[0],
                  dst[1],
                  dst[100],
                  dst[277]);
            if (prach_uncomp_log_count < 16)
              prach_uncomp_log_count++;
          }
        } else if (ru_conf->compMeth_PRACH == XRAN_COMPMETHOD_BLKFLOAT) {

          int16_t local_dst[12 * 2 * N_SC_PER_PRB] __attribute__((aligned(64)));

#if defined(__i386__) || defined(__x86_64__)
          struct xranlib_decompress_request bfp_decom_req = {};
          struct xranlib_decompress_response bfp_decom_rsp = {};
          int payload_len = (3 * ru_conf->iqWidth_PRACH + 1) * 12; // 12 = closest number of PRBs to 139 REs

          // Check compressed data first to see if we have actual PRACH transmission
          int non_zero_compressed = 0;
          for (int i = 0; i < payload_len; i++) {
            if (((uint8_t*)src)[i] != 0) non_zero_compressed++;
          }

          static int prach_decomp_log_count = 0;
          // Log when we have non-zero compressed data OR for first 10 occurrences
#ifdef GNB_PRACH_UPLANE_DEBUG
          bool should_log = (prach_decomp_log_count < 10 || non_zero_compressed > 0);
#else
          bool should_log = false;
#endif
          if (should_log) {
            printf("[gNB PRACH RX] ENTER: frame=%d, slot=%d, sym=%d, aa=%d, iqWidth=%d, payload_len=%d, non_zero_compressed=%d/%d, g_kbar=%d prach_buf=%p dst=%p\n",
                  frame, slot, sym_idx, aa, ru_conf->iqWidth_PRACH, payload_len, non_zero_compressed, payload_len, g_kbar,
                  (void *)ru->prach_buf, (void *)dst);
            LOG_I(HW, "[gNB PRACH RX] Compressed input: [0]=0x%02x [1]=0x%02x [27]=0x%02x [28]=0x%02x [29]=0x%02x [55]=0x%02x [56]=0x%02x\n",
                  ((uint8_t*)src)[0], ((uint8_t*)src)[1], ((uint8_t*)src)[27], ((uint8_t*)src)[28],
                  ((uint8_t*)src)[29], ((uint8_t*)src)[55], ((uint8_t*)src)[56]);
          }

          bfp_decom_req.data_in = (int8_t *)src;
          bfp_decom_req.numRBs = 12; // closest number of PRBs to 139 REs
          bfp_decom_req.len = payload_len;
          bfp_decom_req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
          bfp_decom_req.iqWidth = ru_conf->iqWidth_PRACH;

          bfp_decom_rsp.data_out = (int16_t *)local_dst;
          bfp_decom_rsp.len = 0;

          oai_bfp_decompression(bfp_decom_req.iqWidth, bfp_decom_req.numRBs, bfp_decom_req.data_in, bfp_decom_rsp.data_out);
          bfp_decom_rsp.len = bfp_decom_req.numRBs * 24 * sizeof(int16_t);

          if (should_log) {
            LOG_I(HW, "[gNB PRACH RX] Decompression returned, rsp.len=%d\n", bfp_decom_rsp.len);
            LOG_I(HW, "[gNB PRACH RX] local_dst: [0]=%d [4]=%d [100]=%d [281]=%d\n",
                  local_dst[0], local_dst[4], local_dst[100], local_dst[281]);
            int max_val = 0, min_val = 0;
            int non_zero_count = 0;
            for (int i = 0; i < 12 * 2 * N_SC_PER_PRB; i++) {
              if (local_dst[i] > max_val) max_val = local_dst[i];
              if (local_dst[i] < min_val) min_val = local_dst[i];
              if (local_dst[i] != 0) non_zero_count++;
            }
            LOG_I(HW, "[gNB PRACH RX] Decompressed range: min=%d, max=%d, non_zero=%d/288\n", min_val, max_val, non_zero_count);
            if (non_zero_compressed > 0 || prach_decomp_log_count < 10)
              prach_decomp_log_count++;
          }
#elif defined(__arm__) || defined(__aarch64__)
          armral_bfp_decompression(ru_conf->iqWidth_PRACH, 12, (int8_t *)src, (int16_t *)local_dst);
#else
          AssertFatal(1 == 0, "BFP decompression not supported on this architecture");
#endif
          // note: this is hardwired for 139 point PRACH sequence, kbar=2
          if (sym_idx == prach_start_sym)
            for (idx = 0; idx < (139 * 2); idx++)
              dst[idx] = local_dst[idx + g_kbar];
          else
            for (idx = 0; idx < (139 * 2); idx++)
              dst[idx] += (local_dst[idx + g_kbar]);

#ifdef GNB_PRACH_UPLANE_DEBUG
          // Log extracted PRACH data for first and last symbol
          if (prach_decomp_log_count <= 5) {
            if (sym_idx == prach_start_sym) {
              LOG_I(HW, "[gNB PRACH RX] First symbol dst after g_kbar: [0]=%d [1]=%d [100]=%d [138]=%d\n",
                    dst[0], dst[1], dst[100], dst[138]);
            } else if (sym_idx == prach_start_sym + 11) {
              LOG_I(HW, "[gNB PRACH RX] Final accumulated dst (all 12 syms): [0]=%d [1]=%d [100]=%d [138]=%d\n",
                    dst[0], dst[1], dst[100], dst[138]);
            }
          }
#endif
        } // COMPMETHOD_BLKFLOAT


        else if (ru_conf->compMeth_PRACH == XRAN_COMPMETHOD_BLKSCALE) {
          /* 12 PRBs cover 139 REs */
          int16_t local_dst[12 * 2 * N_SC_PER_PRB] __attribute__((aligned(64)));

#if defined(__i386__) || defined(__x86_64__)
          struct xranlib_decompress_request bs_decom_req = {};
          struct xranlib_decompress_response bs_decom_rsp = {};
          int payload_len = (3 * ru_conf->iqWidth_PRACH + 1) * 12; // 12 = closest number of PRBs to 139 REs

          bs_decom_req.data_in = (int8_t *)src;
          bs_decom_req.numRBs = 12; // closest number of PRBs to 139 REs
          bs_decom_req.len = payload_len;
          bs_decom_req.compMethod = XRAN_COMPMETHOD_BLKSCALE;
          bs_decom_req.iqWidth = ru_conf->iqWidth_PRACH;

          bs_decom_rsp.data_out = (int16_t *)local_dst;
          bs_decom_rsp.len = 0;
          xranlib_decompress_blkscale_avx512(&bs_decom_req, &bs_decom_rsp);
#else
          AssertFatal(1 == 0, "BFP decompression not supported on this architecture");
#endif
          // note: this is hardwired for 139 point PRACH sequence, kbar=2
          if (sym_idx == prach_start_sym) //
            for (idx = 0; idx < (139 * 2); idx++)
              dst[idx] = local_dst[idx + g_kbar];
          else
            for (idx = 0; idx < (139 * 2); idx++)
              dst[idx] += (local_dst[idx + g_kbar]);
        } //COMPMETHOD_BLKSCALE 
        else if (ru_conf->compMeth_PRACH == XRAN_COMPMETHOD_ULAW) {
          /* 12 PRBs cover 139 REs */
          int16_t local_dst[12 * 2 * N_SC_PER_PRB] __attribute__((aligned(64)));

#if defined(__i386__) || defined(__x86_64__)
          struct xranlib_decompress_request ulaw_decom_req = {};
          struct xranlib_decompress_response ulaw_decom_rsp = {};
          int payload_len = (3 * ru_conf->iqWidth_PRACH + 1) * 12; // 12 = closest number of PRBs to 139 REs

          ulaw_decom_req.data_in = (int8_t *)src;
          ulaw_decom_req.numRBs = 12; // closest number of PRBs to 139 REs
          ulaw_decom_req.len = payload_len;
          ulaw_decom_req.compMethod = XRAN_COMPMETHOD_ULAW;
          ulaw_decom_req.iqWidth = ru_conf->iqWidth_PRACH;

          ulaw_decom_rsp.data_out = (int16_t *)local_dst;
          ulaw_decom_rsp.len = 0;
          xranlib_decompress_ulaw_avx512(&ulaw_decom_req, &ulaw_decom_rsp);
#else
          AssertFatal(1 == 0, "BFP decompression not supported on this architecture");
#endif
          // note: this is hardwired for 139 point PRACH sequence, kbar=2
          if (sym_idx == prach_start_sym) //
            for (idx = 0; idx < (139 * 2); idx++)
              dst[idx] = local_dst[idx + g_kbar];
          else
            for (idx = 0; idx < (139 * 2); idx++)
              dst[idx] += (local_dst[idx + g_kbar]);
        } //COMPMETHOD_ULAW
      } // aa
    } // symb_indx
  } // is_prach_slot
  return (0);
}

/** @brief Check if symbol in slot is UL.
 *
 * @param frame_conf xran frame configuration
 * @param slot the current (absolute) slot (number)
 * @param sym_idx the current symbol index */
static bool is_tdd_ul_symbol(const struct xran_frame_config *frame_conf, int slot, int sym_idx)
{
  /* in FDD, every symbol is also UL */
  if (frame_conf->nFrameDuplexType == XRAN_FDD)
    return true;
  int tdd_period = frame_conf->nTddPeriod;
  int slot_in_period = slot % tdd_period;
  /* check if symbol is UL */
  return frame_conf->sSlotConfig[slot_in_period].nSymbolType[sym_idx] == 1 /* UL */;
}

/** @brief Check if symbol in slot is DL.
 *
 * @param frame_conf xran frame configuration
 * @param slot the current (absolute) slot (number)
 * @param sym_idx the current symbol index */
static bool is_tdd_dl_symbol(const struct xran_frame_config *frame_conf, int slot, int sym_idx)
{
  /* in FDD, every symbol is also UL */
  if (frame_conf->nFrameDuplexType == XRAN_FDD)
    return true;
  int tdd_period = frame_conf->nTddPeriod;
  int slot_in_period = slot % tdd_period;
  /* check if symbol is UL */
  return frame_conf->sSlotConfig[slot_in_period].nSymbolType[sym_idx] == 0 /* DL */;
}

/** @brief Check if current slot is guard/mixed */
static bool is_tdd_guard_slot(const struct xran_frame_config *frame_conf, int slot)
{
  return (is_tdd_dl_symbol(frame_conf, slot, 0) && is_tdd_ul_symbol(frame_conf, slot,  XRAN_NUM_OF_SYMBOL_PER_SLOT - 1));
}

/** @brief Check if current slot is DL or guard/mixed without UL (i.e., current
 * slot is not UL). */
static bool is_tdd_dl_guard_slot(const struct xran_frame_config *frame_conf, int slot)
{
  return !is_tdd_ul_symbol(frame_conf, slot, 0);
}

/** @brief Check if current slot is UL or guard/mixed without UL (i.e., current
 * slot is not UL). */
static bool is_tdd_ul_guard_slot(const struct xran_frame_config *frame_conf, int slot)
{
  return is_tdd_ul_symbol(frame_conf, slot, XRAN_NUM_OF_SYMBOL_PER_SLOT - 1);
}

/** @details Read PRACH and PUSCH data from xran buffers.  If
 * I/Q compression (bitwidth < 16 bits) is configured, deccompresses the data
 * before writing. Prints ON TIME counters every 128 frames.
 *
 * Function is blocking and waits for next frame/slot combination. It is unblocked
 * by oai_xran_fh_rx_callback(). It writes the current slot into parameters
 * frame/slot. */
/* UL FH-latency metric: fraction of expected UL symbols whose U-plane data arrived by DU read time.
   Tightening Ta4 (the O-DU UL receive deadline) makes the xran lib drop late UL symbols -> present rate falls. */
static uint64_t g_ul_sym_present = 0;
static uint64_t g_ul_sym_expected = 0;

int xran_fh_rx_read_slot(ru_info_t *ru, int *frame, int *slot)
{
  void *ptr = NULL;
  int32_t *pos = NULL;
  int idx = 0;

  static int64_t old_rx_counter[XRAN_PORTS_NUM] = {0};
  static int64_t old_tx_counter[XRAN_PORTS_NUM] = {0};
  struct xran_common_counters x_counters[XRAN_PORTS_NUM];
  static int outcnt = 0;
#ifndef USE_POLLING
  // pull next event from oran_sync_fifo
  notifiedFIFO_elt_t *res = pullNotifiedFIFO(&oran_sync_fifo);
  atomic_fetch_sub(&xran_queue_length, 1);
  oran_sync_info_t *info = NotifiedFifoData(res);

  // Skip threshold: jump-to-latest when the DU L1 falls this many slots behind the FH
  // sync callbacks. Default 3 (real-time HW). Under non-real-time dilation on a bursty
  // fabric (e.g. 2-physical-port wire over a PCIe gen3 x1 link), U-plane arrives in bursts
  // and a deeper queue rides them out without a desyncing jump. Bounded by XRAN_N_FE_BUF_LEN/2=10
  // (the half-ring buffer-reset horizon) -> keep <10. Tunable via OAI_FH_MAX_QUEUE_NO_JUMP.
  static int MAX_QUEUE_LENGTH_NO_JUMP = 0;
  if (MAX_QUEUE_LENGTH_NO_JUMP == 0) {
    const char *e_mq = getenv("OAI_FH_MAX_QUEUE_NO_JUMP");
    MAX_QUEUE_LENGTH_NO_JUMP = (e_mq && atoi(e_mq) > 0) ? atoi(e_mq) : 3;
    LOG_I(HW, "FH TTI-skip threshold MAX_QUEUE_LENGTH_NO_JUMP=%d\n", MAX_QUEUE_LENGTH_NO_JUMP);
  }
  if (xran_queue_length > 0 && xran_queue_length < MAX_QUEUE_LENGTH_NO_JUMP) {
    LOG_D(HW, "%4d.%2d TTI processing delay detected\n", info->f, info->sl);
  } else if (xran_queue_length >= MAX_QUEUE_LENGTH_NO_JUMP) {
    uint32_t old_f = info->f;
    uint32_t old_sl = info->sl;
    // set the frame/slot info to what is in the last message
    notifiedFIFO_elt_t *f;
    while ((f = pollNotifiedFIFO(&oran_sync_fifo)) != NULL) {
      atomic_fetch_sub(&xran_queue_length, 1);
      delNotifiedFIFO_elt(res);
      res = f;
    }
    info = NotifiedFifoData(res);
    LOG_W(HW, "TTI processing delay detected, skipping %4d.%2d => %4d.%2d\n", old_f, old_sl, info->f, info->sl);
    DevAssert(xran_queue_length == 0);
  }

  *slot = info->sl;
  *frame = info->f;
  // UL slot delay (2026-07-05): under vrtsim the RU's south read trails the UE's ring
  // writes by one slot (ORU_UL_LABEL_SHIFT companion), so UL U-plane for slot N finishes
  // arriving ~2 slots after N. Process an OLDER slot per sync callback so the buffer is
  // complete when read. Safe up to <10 (half-ring reset horizon). Default 0 = stock.
  static int ul_slot_delay = -1;
  if (ul_slot_delay < 0) {
    const char *e_d = getenv("OAI_FH_UL_SLOT_DELAY");
    ul_slot_delay = (e_d && atoi(e_d) > 0) ? atoi(e_d) : 0;
    if (ul_slot_delay)
      LOG_I(HW, "FH UL slot delay = %d slots\n", ul_slot_delay);
  }
  if (ul_slot_delay > 0) {
    struct xran_fh_config *fh_cfg_d = get_xran_fh_config(0);
    int spf_d = 10 << fh_cfg_d->frame_conf.nNumerology;
    int t_d = spf_d * (*frame) + (*slot) - ul_slot_delay;
    if (t_d < 0)
      t_d += 1024 * spf_d;
    *frame = (t_d / spf_d) & 1023;
    *slot = t_d % spf_d;
  }
  delNotifiedFIFO_elt(res);
#else
  *slot = oran_sync_info.sl;
  *frame = oran_sync_info.f;
  uint32_t tti_in = oran_sync_info.tti;

  static int last_slot = -1;
  LOG_D(HW, "oran slot %d, last_slot %d\n", *slot, last_slot);
  int cnt = 0;
  // while (*slot == last_slot)  {
  while (tti_in == oran_sync_info.tti) {
    //*slot = oran_sync_info.sl;
    cnt++;
  }
  LOG_D(HW, "cnt %d, Reading %d.%d\n", cnt, *frame, *slot);
  last_slot = *slot;
#endif
  // return(0);

  struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  int slots_per_frame = 10 << fh_cfg->frame_conf.nNumerology;

  int tti = slots_per_frame * (*frame) + (*slot);

  read_prach_data(ru, *frame, *slot);

  const struct xran_fh_init *fh_init = get_xran_fh_init();
  int fftsize = 1 << fh_cfg->nULFftSize;

  int slot_offset_rxdata = (*slot) % RU_RX_SLOT_DEPTH; // MUST match L1's rxdataF ring (defs_RU.h); was hardcoded 3&slot
  uint32_t slot_size = 4 * 14 * fftsize;
  uint8_t *rx_data = (uint8_t *)ru->rxdataF[0];
  uint8_t *start_ptr = NULL;
  int nb_rx_per_ru = ru->nb_rx / fh_init->xran_ports;
  for (uint16_t cc_id = 0; cc_id < 1 /*nSectorNum*/; cc_id++) { // OAI does not support multiple CC yet.
    for (uint8_t ant_id = 0; ant_id < ru->nb_rx; ant_id++) {
      rx_data = (uint8_t *)ru->rxdataF[ant_id];
      start_ptr = rx_data + (slot_size * slot_offset_rxdata);
      const struct xran_frame_config *frame_conf = &get_xran_fh_config(ant_id / nb_rx_per_ru)->frame_conf;
      // skip processing this slot is TX (no RX in this slot)
      if (!is_tdd_ul_guard_slot(frame_conf, *slot))
        continue;
      // This loop would better be more inner to avoid confusion and maybe also errors.
      for (int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
        /* the callback is for mixed and UL slots. In mixed, we have to
         * skip DL and guard symbols. */
        if (!is_tdd_ul_symbol(frame_conf, *slot, sym_idx))
          continue;

        oran_buf_list_t *bufs = get_xran_buffers(ant_id / nb_rx_per_ru);
        uint8_t *pPrbMapData = bufs->dstcp[ant_id % nb_rx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
        struct xran_prb_map *pRbMap = (struct xran_prb_map *)pPrbMapData;

        uint8_t *src = (uint8_t *)ptr;

        // even when the fragmentation occurs, nRBSize & nRBStart carry the same values in each prbMap
        // therefore, I took the liberty to just extract these values from the first prbMap
        int num_totalRB = pRbMap->prbMap[0].nRBSize;
        int start_totalRB = pRbMap->prbMap[0].nRBStart;
        /* UL FH-latency: was this UL symbol's U-plane data present (arrived) by read time? */
        g_ul_sym_expected++;
        if (pRbMap->prbMap[0].nSecDesc[sym_idx] > 0)
          g_ul_sym_present++;
        else {
          // Misses are rare (~0.02%) but at 4 antennas they compound into a per-TB BLER
          // floor that pins OLLA/MCS low. Log each one to correlate with TB failures.
          static int miss_log_count = 0;
          if (miss_log_count++ < 2000)
            LOG_W(HW, "[UL SYM MISS] f=%d s=%d sym=%d ant=%d\n", *frame, *slot, sym_idx, ant_id);
        }
        if (g_ul_sym_expected % 5000 == 0) {
          LOG_W(HW, "UL sym present: %lu/%lu (%.1f%%)\n", (unsigned long)g_ul_sym_present,
                (unsigned long)g_ul_sym_expected, 100.0 * g_ul_sym_present / g_ul_sym_expected);
          g_ul_sym_present = 0;
          g_ul_sym_expected = 0;
        }
        int32_t local_dst[num_totalRB * N_SC_PER_PRB] __attribute__((aligned(64)));
        // Zero the scratch buffer: with a fragmented symbol (payload > MTU, e.g. 273 PRB at
        // iqWidth >= 12) each fragment fills only its own PRB slice, and the copy-out below is
        // triggered by whichever section completes the range. If a fragment is missing, the
        // untouched slice is uninitialised stack memory and gets written into the RX grid as
        // full-scale garbage -> corrupt REs with healthy reported energy. Zeroing degrades a
        // lost fragment to "no energy" (recoverable by HARQ) instead of catastrophic noise.
        memset(local_dst, 0, sizeof(local_dst));

        // LOG_D(HW, "[%d.%d] pRbMap->nPrbElm %d\n", *frame, *slot, pRbMap->nPrbElm);
        for (uint32_t idxElm = 0; idxElm < pRbMap->nPrbElm; idxElm++) {
          int numRB, startRB;
          uint8_t *pData;
          struct xran_section_desc *p_sec_desc = NULL;
          struct xran_prb_elm *pRbElm = &pRbMap->prbMap[idxElm];
#if defined F_RELEASE
          // UP_nRBSize & UP_nRBStart are for DL U-plane only
          // LOG_D(HW, "[%d.%d] idxElm[%d] startSym[%d]:numSym[%d] UP_startRB[%d]:UP_numRB[%d] sym_idx[%d] ant_id[%d] pRbElm->nRBStart[%d]:pRbElm->nRBSize[%d]\n", *frame, *slot, idxElm, pRbElm->nStartSymb, pRbElm->numSymb, pRbElm->UP_nRBStart, pRbElm->UP_nRBSize, sym_idx, ant_id, pRbElm->nRBStart, pRbElm->nRBSize);
          // Fragmented-symbol deposit (payload > MTU => 2+ sections per symbol, e.g. 273 PRB at
          // iqWidth >= 12). sec_desc[] is indexed in ARRIVAL order, not PRB order, so the old
          // in-loop copy-out fired as soon as the section ENDING the PRB range was reached in
          // array order — if that section arrived first, the symbol was copied to the RX grid
          // before the other fragment had been decompressed into local_dst (half-empty symbol),
          // and the later fragment was then decompressed and silently discarded. Deposit once,
          // after every section of this symbol has been decompressed into the (zeroed) scratch
          // buffer, so fragment arrival order no longer matters and a missing fragment yields
          // zeros (HARQ-recoverable) instead of stale IQ from a previous slot.
          int sym_have_data = 0;
          int32_t *sym_pos = NULL;
          for (int idxDesc = 0; idxDesc < XRAN_MAX_FRAGMENT; idxDesc++) {
            p_sec_desc = &pRbElm->sec_desc[sym_idx][idxDesc];
            if (p_sec_desc == NULL)
              continue;
            if (sym_idx >= pRbElm->nStartSymb && sym_idx < pRbElm->nStartSymb + pRbElm->numSymb) {
              // nSecDesc is reset for the NEXT slot's buffer at the start of each
              // full-slot callback, then incremented by xran_process_rx_sym as
              // each fragment arrives.  Use it to distinguish fresh writes (for
              // this frame) from stale pCtrl/pData left over from two frames ago.
              //
              // Sym 0-9 arrive in the first half of the slot, well before the
              // end-of-slot callback; spin-wait is unnecessary and would race
              // with the reset of the buffer at the slot boundary.  Only spin on
              // sym >= 10 (the last four symbols, which may arrive fractionally
              // after the deadline timer fires).
              // NOTE: a spin-wait on ALL allocated symbols was tried and did NOT
              // recover the missing UL REs — ~71% of a multi-symbol PUSCH's REs
              // are absent from the read buffer per slot (random subset), so the
              // loss is upstream of this read (xran RX buffer rotation/timing),
              // not merely late arrival. See investigation notes.
              if (idxDesc == 0 && sym_idx >= 10) {
                if (pRbElm->nSecDesc[sym_idx] == 0) {
                  // Spin-wait cap for a late/missing tail-symbol fragment. Default 300000 (real-time HW).
                  // At iq>=10 a rare missing fragment makes the FULL spin (~10 ms wall) blow the dilated
                  // slot and cascade into a 20k-skip collapse. Lowering the cap = give up fast on a
                  // genuinely-missing symbol so one slot's loss doesn't snowball. iq9 (~100% present at
                  // read) essentially never reaches it, so this is safe for working widths. Env-tunable.
                  static int spin_cap = 0;
                  if (spin_cap == 0) { const char *e_sc = getenv("OAI_FH_SPIN_CAP"); spin_cap = (e_sc && atoi(e_sc) > 0) ? atoi(e_sc) : 300000; LOG_I(HW, "FH spin cap = %d\n", spin_cap); }
                  volatile uint16_t *ns = (volatile uint16_t *)&pRbElm->nSecDesc[sym_idx];
                  for (int w = 0; w < spin_cap && *ns == 0; w++)
                    __asm__ volatile("pause" ::: "memory");
                }
                if (pRbElm->nSecDesc[sym_idx] == 0)
                  continue;
              }
              // Hardware-RU emulation (OAI_FH_EXPECT_FRAGS=N, default off): a real O-RU emits a
              // symbol's fragments back-to-back with deterministic timing, so the consumer only
              // ever sees complete symbols. Emulate that by waiting for ALL N expected sections
              // (not just the first) before reading — affordable because wall-time budgets are
              // 1/TS dilated relative to the sim-time deadline the spec actually defines.
              // Verdict counters distinguish late-but-arrives (recoverable) from upstream-dropped.
              // PRB-COVERAGE WAIT (always on, self-limiting). A symbol whose payload exceeds the
              // MTU is split into several U-plane sections; the DU used to read as soon as the
              // FIRST section registered, so at 273 PRB with iqWidth >= 12 it deposited a
              // half-covered symbol (the [CONSRMS] probe shows absmean256 = 0 on the fragment-1
              // half). Counting sections is not enough (a duplicate section satisfies a count
              // but leaves a PRB hole), so wait until the registered sections actually COVER
              // num_totalRB. Symbols that fit in one packet cover the range with their first
              // section and never spin — that is what makes this affordable, unlike the
              // count-based OAI_FH_EXPECT_FRAGS gate, which spun on every symbol and broke
              // attach. A genuinely lost fragment costs one bounded spin, then proceeds with
              // zeros for the missing PRBs.
              if (idxDesc == 0 && pRbElm->nSecDesc[sym_idx] > 0) {
                static int cov_cap = 0;
                if (cov_cap == 0) { const char *e_cc = getenv("OAI_FH_COVER_CAP"); cov_cap = (e_cc && atoi(e_cc) > 0) ? atoi(e_cc) : 20000; }
                volatile uint16_t *nsv = (volatile uint16_t *)&pRbElm->nSecDesc[sym_idx];
                static long cov_ok = 0, cov_to = 0;
                int spun = 0;
                for (int w = 0; w < cov_cap; w++) {
                  int cov = 0;
                  int nsec = (int)*nsv;
                  if (nsec > XRAN_MAX_FRAGMENT) nsec = XRAN_MAX_FRAGMENT;
                  for (int d = 0; d < nsec; d++)
                    cov += pRbElm->sec_desc[sym_idx][d].num_prbu;
                  if (cov >= num_totalRB)
                    break;
                  spun = 1;
                  __asm__ volatile("pause" ::: "memory");
                }
                if (spun) {
                  int cov = 0, nsec = (int)*nsv;
                  if (nsec > XRAN_MAX_FRAGMENT) nsec = XRAN_MAX_FRAGMENT;
                  for (int d = 0; d < nsec; d++) cov += pRbElm->sec_desc[sym_idx][d].num_prbu;
                  if (cov >= num_totalRB) cov_ok++; else cov_to++;
                  long tot = cov_ok + cov_to;
                  if (tot <= 8 || (tot % 5000) == 0)
                    printf("[FRAGCOV] completed %ld incomplete %ld (need %d) f=%d s=%d sym=%d ant=%d\n",
                           cov_ok, cov_to, num_totalRB, *frame, *slot, sym_idx, ant_id);
                }
              }
              {
                static int exp_frags = -1;
                if (exp_frags < 0) { const char *e_ef = getenv("OAI_FH_EXPECT_FRAGS"); exp_frags = (e_ef && e_ef[0]) ? atoi(e_ef) : 0; }
                if (exp_frags > 1 && idxDesc == 0 && pRbElm->nSecDesc[sym_idx] > 0
                    && pRbElm->nSecDesc[sym_idx] < exp_frags) {
                  static int fw_cap = 0;
                  if (fw_cap == 0) { const char *e_sc = getenv("OAI_FH_SPIN_CAP"); fw_cap = (e_sc && atoi(e_sc) > 0) ? atoi(e_sc) : 300000; }
                  volatile uint16_t *ns2 = (volatile uint16_t *)&pRbElm->nSecDesc[sym_idx];
                  int w2 = 0;
                  for (; w2 < fw_cap && *ns2 < exp_frags; w2++)
                    __asm__ volatile("pause" ::: "memory");
                  static long fw_ok = 0, fw_to = 0, fw_have_f1 = 0, fw_have_f2 = 0;
                  if (*ns2 >= exp_frags) {
                    fw_ok++;
                  } else {
                    fw_to++;
                    // Which fragment survived? start_prbu 0 = frag1 (late-arrival story),
                    // nonzero = frag2 (stale-generation story).
                    if (pRbElm->sec_desc[sym_idx][0].start_prbu == 0) fw_have_f1++; else fw_have_f2++;
                  }
                  long fw_tot = fw_ok + fw_to;
                  if (fw_tot <= 8 || (fw_tot % 2000) == 0)
                    printf("[FRAGWAIT] complete %ld timeout %ld (have_f1 %ld have_f2 %ld) f=%d s=%d sym=%d ant=%d\n",
                           fw_ok, fw_to, fw_have_f1, fw_have_f2, *frame, *slot, sym_idx, ant_id);
                }
              }
              if (idxDesc >= (int)pRbElm->nSecDesc[sym_idx]) {
                // Fragment not received this frame or sym arrived early and is
                // already counted — skip stale sec_desc entries.
                continue;
              }
              if (!p_sec_desc->pCtrl)
                continue;
              pData = p_sec_desc->pData;
              numRB = p_sec_desc->num_prbu;
              startRB = p_sec_desc->start_prbu;
              // Fragment-descriptor probe: what does each section actually claim? Needed to
              // tell "second fragment never registered" from "registered but mis-described".
              { static long fd_n = 0;
                if (fd_n++ < 24)
                  printf("[FRAGDESC] f=%d s=%d sym=%d ant=%d idxDesc=%d nSec=%u startRB=%d numRB=%d total=%d/%d comp=%d iqw=%d\n",
                         *frame, *slot, sym_idx, ant_id, idxDesc, (unsigned)pRbElm->nSecDesc[sym_idx],
                         startRB, numRB, start_totalRB, num_totalRB, pRbElm->compMethod, pRbElm->iqWidth); }
              // num_prbu & start_prbu are for UL U-plane only
              // LOG_D(HW, "p_sec_desc[%d] startRB[%d]:numRB[%d]\n", idxDesc, startRB, numRB);
#endif
              ptr = pData;
              pos = (int32_t *)(start_ptr + (4 * sym_idx * fftsize));
              if (ptr == NULL || pos == NULL)
                continue;
              src = pData;
              int pusch_src_nonzero = 0;
              int pusch_dst_nonzero = 0;
              int16_t pusch_src_min = 32767;
              int16_t pusch_src_max = -32768;
              int16_t pusch_dst_min = 32767;
              int16_t pusch_dst_max = -32768;
              if (pRbElm->compMethod == XRAN_COMPMETHOD_NONE) {
                // NOTE: gcc 11 knows how to generate AVX2 for this!
                for (idx = 0; idx < (numRB * N_SC_PER_PRB) * 2; idx++) {
                  int16_t sample = (int16_t)ntohs(((uint16_t *)src)[idx]);
                  int16_t shifted = sample >> 2;
                  if (sample != 0)
                    pusch_src_nonzero++;
                  if (shifted != 0)
                    pusch_dst_nonzero++;
                  if (sample < pusch_src_min)
                    pusch_src_min = sample;
                  if (sample > pusch_src_max)
                    pusch_src_max = sample;
                  if (shifted < pusch_dst_min)
                    pusch_dst_min = shifted;
                  if (shifted > pusch_dst_max)
                    pusch_dst_max = shifted;
                  ((int16_t *)local_dst)[idx + startRB * N_SC_PER_PRB * 2] = shifted;
                }
                static int pusch_rx_uncomp_dbg_count = 0;
#ifdef GNB_PUSCH_UPLANE_DEBUG
                const int pusch_rx_msg3_debug = 1;
#else
                const int pusch_rx_msg3_debug = 0;
#endif
                if (pusch_rx_msg3_debug && pusch_rx_uncomp_dbg_count < 4096) {
                  LOG_A(HW,
                        "[gNB PUSCH RX UNCOMP] frame=%d slot=%d sym=%d ant=%d idxElm=%u idxDesc=%d map_startRB=%d map_numRB=%d desc_startRB=%d desc_numRB=%d elm_startSym=%d elm_numSym=%d comp=%d iqWidth=%d src_nonzero=%d/%d src_min=%d src_max=%d dst_nonzero=%d/%d dst_min=%d dst_max=%d\n",
                        *frame,
                        *slot,
                        sym_idx,
                        ant_id,
                        idxElm,
                        idxDesc,
                        start_totalRB,
                        num_totalRB,
                        startRB,
                        numRB,
                        pRbElm->nStartSymb,
                        pRbElm->numSymb,
                        pRbElm->compMethod,
                        pRbElm->iqWidth,
                        pusch_src_nonzero,
                        (numRB * N_SC_PER_PRB) * 2,
                        pusch_src_nonzero ? pusch_src_min : 0,
                        pusch_src_nonzero ? pusch_src_max : 0,
                        pusch_dst_nonzero,
                        (numRB * N_SC_PER_PRB) * 2,
                        pusch_dst_nonzero ? pusch_dst_min : 0,
                        pusch_dst_nonzero ? pusch_dst_max : 0);
                  pusch_rx_uncomp_dbg_count++;
                }
              } else if (pRbElm->compMethod == XRAN_COMPMETHOD_BLKFLOAT) {
#if defined(__i386__) || defined(__x86_64__)
                struct xranlib_decompress_request bfp_decom_req = {};
                struct xranlib_decompress_response bfp_decom_rsp = {};

                int16_t payload_len = (3 * pRbElm->iqWidth + 1) * numRB;

                static int pusch_decomp_log_count = 0;
                if (pusch_decomp_log_count < 5) {
                  LOG_I(HW, "[PUSCH BFP] ENTER: frame=%d, slot=%d, sym=%d, ant=%d, startRB=%d, numRB=%d, iqWidth=%d, payload_len=%d\n",
                        *frame, *slot, sym_idx, ant_id, startRB, numRB, pRbElm->iqWidth, payload_len);
                }

                bfp_decom_req.data_in = (int8_t *)src;
                bfp_decom_req.numRBs = numRB;
                bfp_decom_req.len = payload_len;
                bfp_decom_req.compMethod = pRbElm->compMethod;
                bfp_decom_req.iqWidth = pRbElm->iqWidth;

                bfp_decom_rsp.data_out = (int16_t *) (local_dst + startRB * N_SC_PER_PRB);
                bfp_decom_rsp.len = 0;

                oai_bfp_decompression(bfp_decom_req.iqWidth, bfp_decom_req.numRBs, bfp_decom_req.data_in, bfp_decom_rsp.data_out);
                bfp_decom_rsp.len = bfp_decom_req.numRBs * 24 * sizeof(int16_t);

                // BFP output is ±(2^(iqWidth-1)-1)*2^exponent; scale down by 2 bits
                // to match the IQ16 NONE path which applies >> 2 (see above).
                int16_t *bfp_out = bfp_decom_rsp.data_out;
                for (int s = 0; s < numRB * N_SC_PER_PRB * 2; s++)
                  bfp_out[s] >>= 2;
                // Per-antenna divergence probe (2026-07-06): the RU sends 4 byte-identical
                // streams; log a per-(slot,sym,ant) signature post-decompress to find where
                // they diverge (per-eAxC staleness/reorder at the DU). OAI_PUSCH_ANT_DEBUG=1.
                {
                  static int ant_dbg = -1;
                  static int ant_dbg_count = 0;
                  if (ant_dbg < 0) {
                    const char *e_ad = getenv("OAI_PUSCH_ANT_DEBUG");
                    ant_dbg = (e_ad && e_ad[0]) ? 1 : 0;
                  }
                  if (ant_dbg && ant_dbg_count < 8192) {
                    int nz_s = 0;
                    long asum = 0;
                    const int n16 = numRB * N_SC_PER_PRB * 2;
                    for (int s = 0; s < n16; s++) {
                      if (bfp_out[s]) nz_s++;
                      asum += bfp_out[s] < 0 ? -bfp_out[s] : bfp_out[s];
                    }
                    if (nz_s > 0) {
                      LOG_A(HW, "[PUSCH ANT] f=%d s=%d sym=%d ant=%d elm=%u nz=%d/%d asum=%ld mid=(%d,%d)\n",
                            *frame, *slot, sym_idx, ant_id, idxElm, nz_s, n16, asum,
                            bfp_out[n16 / 2], bfp_out[n16 / 2 + 1]);
                      ant_dbg_count++;
                    }
                  }
                }

                if (pusch_decomp_log_count < 5) {
                  LOG_I(HW, "[PUSCH BFP] Decompression returned, rsp.len=%d\n", bfp_decom_rsp.len);
                  pusch_decomp_log_count++;
                }
#elif defined(__arm__) || defined(__aarch64__)
                armral_bfp_decompression(pRbElm->iqWidth, numRB, (int8_t *)src, (int16_t *)local_dst);
#else
                AssertFatal(1 == 0, "BFP compression not supported on this architecture");
#endif
                outcnt++;
              } else if (pRbElm->compMethod == XRAN_COMPMETHOD_BLKSCALE) {
#if defined(__i386__) || defined(__x86_64__)
            struct xranlib_decompress_request bs_decom_req = {};
            struct xranlib_decompress_response bs_decom_rsp = {};

            int16_t payload_len = (3 * pRbElm->iqWidth + 1) * pRbElm->nRBSize;

            bs_decom_req.data_in = (int8_t *)src;
            bs_decom_req.numRBs = numRB;
            bs_decom_req.len = payload_len;
            bs_decom_req.compMethod = pRbElm->compMethod;
            bs_decom_req.iqWidth = pRbElm->iqWidth;

            bs_decom_rsp.data_out = (int16_t *)(local_dst + startRB * N_SC_PER_PRB);
            bs_decom_rsp.len = 0;

            xranlib_decompress_blkscale_avx512(&bs_decom_req, &bs_decom_rsp);

#else
            AssertFatal(1 == 0, "BLKSCALE compression not supported on this architecture");
#endif
            outcnt++;
          } else if (pRbElm->compMethod == XRAN_COMPMETHOD_ULAW) {
#if defined(__i386__) || defined(__x86_64__)
            struct xranlib_decompress_request ulaw_decom_req = {};
            struct xranlib_decompress_response ulaw_decom_rsp = {};

            int16_t payload_len = (3 * pRbElm->iqWidth + 1) * pRbElm->nRBSize;

            ulaw_decom_req.data_in = (int8_t *)src;
            ulaw_decom_req.numRBs = numRB;
            ulaw_decom_req.len = payload_len;
            ulaw_decom_req.compMethod = pRbElm->compMethod;
            ulaw_decom_req.iqWidth = pRbElm->iqWidth;

            ulaw_decom_rsp.data_out = (int16_t *)(local_dst + startRB * N_SC_PER_PRB);
            ulaw_decom_rsp.len = 0;

            xranlib_decompress_ulaw_avx512(&ulaw_decom_req, &ulaw_decom_rsp);

#else
            AssertFatal(1 == 0, "ULAW compression not supported on this architecture");
#endif
            outcnt++;
          } 
               else {
                printf("pRbElm->compMethod == %d is not supported\n", pRbElm->compMethod);
                exit(-1);
              }
              // This section's IQ is now in local_dst at its own PRB offset; defer the deposit
              // until every section of the symbol has been decompressed (see note above).
              sym_have_data = 1;
              sym_pos = pos;
            }
          } // idxDesc
          if (sym_have_data && sym_pos != NULL) {
            // Scale probe: delivered amplitude of the decoded symbol (first 256 int16
            // = low PRBs). Compares raw-vs-BFP path output scale across iq widths.
            {
              static _Atomic long crms_n = 0;
              if (ant_id == 0 && (++crms_n % 2048) == 1) {
                long acc = 0;
                const int16_t *p16 = (const int16_t *)local_dst;
                for (int s = 0; s < 256; s++)
                  acc += p16[s] < 0 ? -p16[s] : p16[s];
                printf("[CONSRMS] f=%d s=%d sym=%d absmean256 %ld\n", *frame, *slot, sym_idx, acc / 256);
              }
            }
            int pos_len = 0;
            int neg_len = 0;

            if (start_totalRB < (num_totalRB >> 1)) // there are PRBs left of DC
              neg_len = min((num_totalRB * 6) - (start_totalRB * 12), num_totalRB * N_SC_PER_PRB);
            pos_len = (num_totalRB * N_SC_PER_PRB) - neg_len;
            // Calculation of the pointer for the section in the buffer.
            // positive half
            uint8_t *dst1 = (uint8_t *)(sym_pos + (neg_len == 0 ? ((start_totalRB * N_SC_PER_PRB) - (num_totalRB * 6)) : 0));
            // negative half
            uint8_t *dst2 = (uint8_t *)(sym_pos + (start_totalRB * N_SC_PER_PRB) + fftsize - (num_totalRB * 6));
            memcpy((void *)dst2, (void *)local_dst, neg_len * 4);
            memcpy((void *)dst1, (void *)&local_dst[neg_len], pos_len * 4);
          }
        } // idxElm

      } // sym_ind
    } // ant_ind
  } // vv_inf
  static FILE *stats_file = NULL;
  static int stats_file_initialized = 0;
  if ((*frame & 0x7f) == 0 && *slot == 0 && xran_get_common_counters(gxran_handle, &x_counters[0]) == XRAN_STATUS_SUCCESS) {
    // Initialize stats file on first run
    if (!stats_file_initialized) {
      stats_file = fopen("fh_load_stats.csv", "w");
      if (stats_file != NULL) {
        fprintf(stats_file, "timestamp,o_xu_id,rx_kbps,tx_kbps,total_kbps,rx_pps,tx_pps,total_pps,total_msgs_rcvd\n");
        fflush(stats_file);
      }
      stats_file_initialized = 1;
    }
    for (int o_xu_id = 0; o_xu_id < fh_init->xran_ports; o_xu_id++) {
      const long rx_pps = x_counters[o_xu_id].rx_counter - old_rx_counter[o_xu_id];
      const long tx_pps = x_counters[o_xu_id].tx_counter - old_tx_counter[o_xu_id];
      const long rx_kbps = x_counters[o_xu_id].rx_bytes_per_sec * 8 / 1000L;
      const long tx_kbps = x_counters[o_xu_id].tx_bytes_per_sec * 8 / 1000L;
      const long total_kbps = rx_kbps + tx_kbps;
      LOG_I(HW,
            "[%s%d][rx %7ld pps %7ld kbps %7ld][tx %7ld pps %7ld kbps %7ld][Total Msgs_Rcvd %ld]\n",
            "o-du ",
            o_xu_id,
            x_counters[o_xu_id].rx_counter,
            rx_pps,
            rx_kbps,
            x_counters[o_xu_id].tx_counter,
            tx_pps,
            tx_kbps,
            x_counters[o_xu_id].Total_msgs_rcvd);
      LOG_I(HW,
            "[FH RXCLS][o-du %d] dupl %ld srs %ld ontime %ld early %ld late %ld corrupt %ld\n",
            o_xu_id,
            (long)x_counters[o_xu_id].Rx_pkt_dupl,
            (long)x_counters[o_xu_id].rx_srs_packets,
            (long)x_counters[o_xu_id].Rx_on_time,
            (long)x_counters[o_xu_id].Rx_early,
            (long)x_counters[o_xu_id].Rx_late,
            (long)x_counters[o_xu_id].Rx_corrupt);
      LOG_I(HW,
            "[FH LOAD][o-du %d] rx=%ld.%03ld Mbps tx=%ld.%03ld Mbps total=%ld.%03ld Mbps rx_pps=%ld tx_pps=%ld total_pps=%ld\n",
            o_xu_id,
            rx_kbps / 1000L,
            rx_kbps % 1000L,
            tx_kbps / 1000L,
            tx_kbps % 1000L,
            total_kbps / 1000L,
            total_kbps % 1000L,
            rx_pps,
            tx_pps,
            rx_pps + tx_pps);
      // Log to file
      if (stats_file != NULL) {
        time_t now = time(NULL);
        fprintf(stats_file, "%ld,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
                now,
                o_xu_id,
                rx_kbps,
                tx_kbps,
                total_kbps,
                rx_pps,
                tx_pps,
                rx_pps + tx_pps,
                x_counters[o_xu_id].Total_msgs_rcvd);
        fflush(stats_file);  // Ensure data is written immediately
      }
      for (int rxant = 0; rxant < ru->nb_rx / fh_init->xran_ports; rxant++)
        LOG_I(HW,
              "[%s%d][pusch%d %7ld prach%d %7ld]\n",
              "o_du",
              o_xu_id,
              rxant,
              x_counters[o_xu_id].rx_pusch_packets[rxant],
              rxant,
              x_counters[o_xu_id].rx_prach_packets[rxant]);
      if (x_counters[o_xu_id].rx_counter > old_rx_counter[o_xu_id])
        old_rx_counter[o_xu_id] = x_counters[o_xu_id].rx_counter;
      if (x_counters[o_xu_id].tx_counter > old_tx_counter[o_xu_id])
        old_tx_counter[o_xu_id] = x_counters[o_xu_id].tx_counter;
    }
  }
  return (0);
}

/** @details Write PDSCH IQ-data from OAI txdataF_BF buffer to xran buffers. If
 * I/Q compression (bitwidth < 16 bits) is configured, compresses the data
 * before writing. */
int xran_fh_tx_send_slot(ru_info_t *ru, int frame, int slot, uint64_t timestamp)
{
  int tti = /*frame*SUBFRAMES_PER_SYSTEMFRAME*SLOTNUM_PER_SUBFRAME+*/ 20 * frame
            + slot; // commented out temporarily to check that compilation of oran 5g is working.

  void *ptr = NULL;
  int32_t *pos = NULL;
  int idx = 0;

  const struct xran_fh_init *fh_init = get_xran_fh_init();
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
   int nPRBs = fh_cfg->nDLRBs;
  int fftsize = 1 << fh_cfg->nDLFftSize;
  int nb_tx_per_ru = ru->nb_tx / fh_init->xran_ports;
  int nb_rx_per_ru = ru->nb_rx / fh_init->xran_ports;

  // Handle CP UL packet here instead of at xran_fh_rx_read_slot() as oran_fh_if4p5_south_in() lags behind
  // oran_fh_if4p5_south_out() (which is invoked at the right time slot) by 4 slots.
  // Need to use --continuous-tx so that this routine will be triggered in RX slot.
  for (uint16_t cc_id = 0; cc_id < 1 /*nSectorNum*/; cc_id++) { // OAI does not support multiple CC yet.
    for (uint8_t ant_id = 0; ant_id < ru->nb_rx; ant_id++) {
      int first = 1; // The first UL symbol
      const struct xran_frame_config *frame_conf = &get_xran_fh_config(ant_id / nb_rx_per_ru)->frame_conf;
      // skip processing this slot is TX (no RX in this slot)
      if (!is_tdd_ul_guard_slot(frame_conf, slot)) {
        continue;
      }
      // This loop would better be more inner to avoid confusion and maybe also errors.
      for (int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
        /* skip DL and guard symbols. */
        if (!is_tdd_ul_symbol(frame_conf, slot, sym_idx)) {
          continue;
        }
        oran_buf_list_t *bufs = get_xran_buffers(ant_id / nb_rx_per_ru);
        uint8_t *pPrbMapData = bufs->dstcp[ant_id % nb_rx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
        struct xran_prb_map *pPrbMap = (struct xran_prb_map *)pPrbMapData;

        // LOG_D(HW, "pPrbMap->nPrbElm %d\n", pPrbMap->nPrbElm);
        for (uint32_t idxElm = 0; idxElm < pPrbMap->nPrbElm; idxElm++) {
          struct xran_prb_elm *pRbElm = &pPrbMap->prbMap[idxElm];
          // Cat-B STEP 3a.1 (OAI_CATB_BFW=1): attach beamforming weights to this UL C-plane
          // section. EMISSION ONLY — the RU has no consumer yet, so throughput must not move;
          // the point is to prove the bytes reach the wire before anything depends on them.
          // xran_cp_populate_section_ext_1() is an API the APPLICATION must call: it is declared
          // in xran_cp_api.h and never invoked inside xran, which is why nothing emits BFW today.
          catb_bfw_attach(pRbElm);
          int numRB, startRB;
#if defined F_RELEASE
          numRB = pRbElm->UP_nRBSize;
          startRB = pRbElm->UP_nRBStart;
          struct xran_section_desc *p_sec_desc = &pRbElm->sec_desc[sym_idx][0];
#endif
          // LOG_D(HW, "pPrbMap[%d] : PRBstart %d nPRBs %d\n", idxElm, startRB, numRB);
          // For Liteon FR2 with RunSlotPrbMapBySymbolEnable xran_prb_map will have xran_prb_elm prbMap[14], each idxElm matches to sym_idx.
          if (fh_cfg->RunSlotPrbMapBySymbolEnable) {
            if (sym_idx >= pRbElm->nStartSymb && sym_idx < pRbElm->nStartSymb + pRbElm->numSymb) {
              if (!p_sec_desc->pCtrl)
                continue;
              // ant_id / no of antenna per beam gives the beam_nb
              pRbElm->nBeamIndex = ru->beam_id[ant_id / (ru->nb_rx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT + sym_idx];
              // In phy-f-1.0/fhi_lib/lib/api/xran_pkt_cp.h, beamId:15 is of 15bit. -1 set extension bit ef:1 to 1 mistakenly.
              if (pRbElm->nBeamIndex == -1)
                pRbElm->nBeamIndex = 0;
            }
          } else {
            if (first) {
              // ant_id / no of antenna per beam gives the beam_nb
              pRbElm->nBeamIndex =
                  ru->beam_id[ant_id / (ru->nb_rx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT + sym_idx];
              // In phy-f-1.0/fhi_lib/lib/api/xran_pkt_cp.h, beamId:15 is of 15bit. -1 set extension bit ef:1 to 1 mistakenly.
              if (pRbElm->nBeamIndex == -1) {
                pRbElm->nBeamIndex = 0;
              } else {
                first = 0;
              }
            }
          }
        }
      }
    }
  }

  for (uint16_t cc_id = 0; cc_id < 1 /*nSectorNum*/; cc_id++) { // OAI does not support multiple CC yet.
    for (uint8_t ant_id = 0; ant_id < ru->nb_tx; ant_id++) {
      oran_buf_list_t *bufs = get_xran_buffers(ant_id / nb_tx_per_ru);
      const struct xran_frame_config *frame_conf = &get_xran_fh_config(ant_id / nb_tx_per_ru)->frame_conf;
      // skip processing this slot is TX (no TX in this slot)
      if (!is_tdd_dl_guard_slot(frame_conf, slot)) {
        if (slot == 0 && frame % 2 == 0)
          LOG_D(HW, "[xran_tx] Frame %d Slot %d ant_id %d: SKIPPED (not DL/guard slot)\n", frame, slot, ant_id);
        continue;
      }
      // if (slot == 0 && frame % 2 == 0 && ant_id == 0)
      //   LOG_D(HW, "[xran_tx] Frame %d Slot %d ant_id %d: Processing DL symbols\n", frame, slot, ant_id);

      // For Liteon FR2 with RunSlotPrbMapBySymbolEnable. Set nPrbElm if beam_id = -1 for all downlink symbols
      if (fh_cfg->RunSlotPrbMapBySymbolEnable) {
        bool beam_used = false;
        uint8_t *pPrbMapData = bufs->srccp[ant_id % nb_tx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
        struct xran_prb_map *pPrbMap = (struct xran_prb_map *)pPrbMapData;
        struct xran_prb_map *pRbMap = pPrbMap;
        int32_t dl_sym_end = 0;
        for (int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
          if (is_tdd_dl_symbol(frame_conf, slot, sym_idx)) {
            if (ru->beam_id[ant_id / (ru->nb_tx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT+ sym_idx] != -1)
              beam_used |= true;
          }
          else {
              dl_sym_end = sym_idx;
              break;
          }
        }
        if (is_tdd_guard_slot(frame_conf, slot))
          pRbMap->nPrbElm = dl_sym_end;
        else
          pRbMap->nPrbElm = XRAN_NUM_OF_SYMBOL_PER_SLOT;
        if (!beam_used) {
          pRbMap->nPrbElm = 0;
          continue;
        }
      }

      // This loop would better be more inner to avoid confusion and maybe also errors.
      for (int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
        /* skip UL and guard symbols. */
        if (!is_tdd_dl_symbol(frame_conf, slot, sym_idx)) {
          continue;
        }
        uint8_t *pData =
            bufs->src[ant_id % nb_tx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers[sym_idx % XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
        uint8_t *pPrbMapData = bufs->srccp[ant_id % nb_tx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
        struct xran_prb_map *pPrbMap = (struct xran_prb_map *)pPrbMapData;
        ptr = pData;
        pos = &ru->txdataF_BF[ant_id][sym_idx * fftsize];
        // if (slot == 0 && frame % 2 == 0 && ant_id == 0 && sym_idx >= 2 && sym_idx <= 5)
        //   LOG_D(HW, "[xran_tx] Frame %d Slot %d ant_id %d sym %d: ptr=%p pos=%p\n", frame, slot, ant_id, sym_idx, ptr, pos);

        uint8_t *u8dptr;
        // even when the fragmentation occurs, nRBSize & nRBStart carry the same values in each prbMap
        // therefore, I took the liberty to just extract these values from the first prbMap
        struct xran_prb_elm *p_prbMapElm = &pPrbMap->prbMap[0];
        int num_totalRB = p_prbMapElm->nRBSize;
        int start_totalRB = p_prbMapElm->nRBStart;

        if (ptr && pos) {
          u8dptr = (uint8_t *)ptr;
          int16_t payload_len = 0;

          uint8_t *dst = (uint8_t *)u8dptr;

          for (uint32_t idxElm = 0; idxElm < pPrbMap->nPrbElm; idxElm++) {
            struct xran_section_desc *p_sec_desc = NULL;
            struct xran_prb_elm *p_prbMapElm = &pPrbMap->prbMap[idxElm];
            if (sym_idx == 0) {
              // ant_id / no of antenna per beam gives the beam_nb
              p_prbMapElm->nBeamIndex = ru->beam_id[ant_id / (ru->nb_tx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT];
              // In phy-f-1.0/fhi_lib/lib/api/xran_pkt_cp.h, beamId:15 is of 15bit. -1 set extension bit ef:1 to 1 mistakenly.
              if (p_prbMapElm->nBeamIndex == -1)
                p_prbMapElm->nBeamIndex = 0;
            }

            // radio-transport fragmentation is not supported in xran F release;
            // E-bit = 1 => each ethernet frame is considered as the last fragment;
            // a group of PRBs per each symbol is encapsulated in one ethernet frame.
            // => seems that the RUs don't check for E-bit
#if defined F_RELEASE
            p_sec_desc = &p_prbMapElm->sec_desc[sym_idx][0];
            int16_t startRB = p_prbMapElm->UP_nRBStart;
            int16_t numRB = p_prbMapElm->UP_nRBSize;
#endif

            if (p_sec_desc == NULL) {
              printf("p_sec_desc == NULL\n");
              exit(-1);
            }

            // For Liteon FR2 with RunSlotPrbMapBySymbolEnable xran_prb_map will have xran_prb_elm prbMap[14], each idxElm matches to sym_idx.
            if (fh_cfg->RunSlotPrbMapBySymbolEnable) {
              /* skip, if not scheduled */
              if(sym_idx < p_prbMapElm->nStartSymb || sym_idx >= p_prbMapElm->nStartSymb + p_prbMapElm->numSymb){
                  p_sec_desc->iq_buffer_offset = 0;
                  p_sec_desc->iq_buffer_len    = 0;
                  continue;
              }
              // ant_id / no of antenna per beam gives the beam_nb
              p_prbMapElm->nBeamIndex = ru->beam_id[ant_id / (ru->nb_tx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT+ sym_idx];
              // In phy-f-1.0/fhi_lib/lib/api/xran_pkt_cp.h, beamId:15 is of 15bit. -1 set extension bit ef:1 to 1 mistakenly.
              if (p_prbMapElm->nBeamIndex == -1)
                p_prbMapElm->nBeamIndex = 0;
            } else {
              if (sym_idx == 0) {
                // ant_id / no of antenna per beam gives the beam_nb
                p_prbMapElm->nBeamIndex = ru->beam_id[ant_id / (ru->nb_tx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT];
                // In phy-f-1.0/fhi_lib/lib/api/xran_pkt_cp.h, beamId:15 is of 15bit. -1 set extension bit ef:1 to 1 mistakenly.
                if (p_prbMapElm->nBeamIndex == -1)
                  p_prbMapElm->nBeamIndex = 0;
              }
            }

#ifdef GNB_FHI_PRB_DEBUG
            // Log PRB map element compression settings before packet header construction
            static int prb_elm_log_count = 0;
            if (prb_elm_log_count < 5 && sym_idx >= 2 && sym_idx <= 5 && slot == 0) {
              LOG_I(HW, "[TX PRB ELM] frame=%d, slot=%d, sym=%d, ant=%d, idxElm=%d: compMethod=%d, iqWidth=%d, nRBStart=%d, nRBSize=%d\n",
                    frame, slot, sym_idx, ant_id, idxElm, p_prbMapElm->compMethod, p_prbMapElm->iqWidth,
                    p_prbMapElm->nRBStart, p_prbMapElm->nRBSize);
              prb_elm_log_count++;
            }
#endif

            dst = xran_add_hdr_offset(dst, p_prbMapElm->compMethod);

            uint16_t *dst16 = (uint16_t *)dst;

            // Start of this section
            int32_t *pos_start = pos + (start_totalRB + startRB) * N_SC_PER_PRB;

            if (p_prbMapElm->compMethod == XRAN_COMPMETHOD_NONE) {
              payload_len = numRB * N_SC_PER_PRB * 4L;

              /* convert to Network order */
              // NOTE: ggc 11 knows how to generate AVX2 for this!
              for (idx = 0; idx < (numRB * N_SC_PER_PRB) * 2; idx++)
                ((uint16_t *)dst16)[idx] = htons(((uint16_t *)pos_start)[idx]);
            } else if (p_prbMapElm->compMethod == XRAN_COMPMETHOD_BLKFLOAT) {
              payload_len = (3 * p_prbMapElm->iqWidth + 1) * numRB;

#if defined(__i386__) || defined(__x86_64__)
              struct xranlib_compress_request bfp_com_req = {};
              struct xranlib_compress_response bfp_com_rsp = {};

              // For RAU configuration (no fft_shift), txdataF_BF is already in [neg|pos] format
              // which is what the BFP compression library expects. Use pos_start directly.
              bfp_com_req.data_in = (int16_t *)pos_start;
              bfp_com_req.numRBs = numRB;
              bfp_com_req.len = payload_len;
              bfp_com_req.compMethod = p_prbMapElm->compMethod;
              bfp_com_req.iqWidth = p_prbMapElm->iqWidth;

              bfp_com_rsp.data_out = (int8_t *)dst;
              bfp_com_rsp.len = 0;

              oai_bfp_compression(bfp_com_req.iqWidth, bfp_com_req.numRBs, bfp_com_req.data_in, bfp_com_rsp.data_out);
              bfp_com_rsp.len = (3 * bfp_com_req.iqWidth + 1) * bfp_com_req.numRBs;
#elif defined(__arm__) || defined(__aarch64__)
              armral_bfp_compression(p_prbMapElm->iqWidth, numRB, (int16_t *)pos_start, (int8_t *)dst);
#else
              AssertFatal(1 == 0, "BFP compression not supported on this architecture");
#endif
            } else if (p_prbMapElm->compMethod == XRAN_COMPMETHOD_BLKSCALE) {

              payload_len = (3 * p_prbMapElm->iqWidth + 1) * numRB;

#if defined(__i386__) || defined(__x86_64__)

              struct xranlib_compress_request  req = {};
              struct xranlib_compress_response rsp = {};

              // For RAU configuration (no fft_shift), txdataF_BF is already in [neg|pos] format
              req.data_in    = (int16_t *)pos_start;
              req.numRBs     = numRB;
              req.len        = payload_len;
              req.compMethod = XRAN_COMPMETHOD_BLKSCALE;
              req.iqWidth    = p_prbMapElm->iqWidth;

              rsp.data_out = (int8_t *)dst;
              rsp.len = 0;
              xranlib_compress_blkscale_avx512(&req, &rsp);

#else
              AssertFatal(0, "BLKSCALE compression not supported on this architecture");
#endif

            } else if (p_prbMapElm->compMethod == XRAN_COMPMETHOD_ULAW) {

              payload_len = (3 * p_prbMapElm->iqWidth + 1) *numRB;

#if defined(__i386__) || defined(__x86_64__)

              struct xranlib_compress_request  req = {};
              struct xranlib_compress_response rsp = {};

              // For RAU configuration (no fft_shift), txdataF_BF is already in [neg|pos] format
              req.data_in    = (int16_t *)pos_start;
              req.numRBs     = numRB;
              req.len        = payload_len;
              req.compMethod = XRAN_COMPMETHOD_ULAW;
              req.iqWidth    = p_prbMapElm->iqWidth;

              rsp.data_out = (int8_t *)dst;
              rsp.len = 0;
              xranlib_compress_ulaw_avx512(&req, &rsp);

#else
              AssertFatal(0, "ULAW compression not supported on this architecture");
#endif

            }
            
            
            else {
              printf("p_prbMapElm->compMethod == %d is not supported\n", p_prbMapElm->compMethod);
              exit(-1);
            }

            p_sec_desc->iq_buffer_offset = RTE_PTR_DIFF(dst, u8dptr);
            p_sec_desc->iq_buffer_len = payload_len;

            dst += payload_len;
            dst = xran_add_hdr_offset(dst, p_prbMapElm->compMethod);
          }

          // The tti should be updated as it increased.
          pPrbMap->tti_id = tti;

        } else {
          printf("ptr ==NULL\n");
          exit(-1); // fails here??
        }
      }
    }
  }
  return (0);
}
