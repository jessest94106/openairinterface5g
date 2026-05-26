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

#include "assertions.h"
#include "nr/nr_common.h"
#include "utils.h"
#include <asm-generic/errno.h>
#include <netinet/in.h>
#include <rte_ring.h>
#include <rte_ring_core.h>
#include <rte_ring_elem.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "xran_fh_o_du.h"
#include "xran_fh_o_ru.h"
#include "xran_compression.h"
#include "armral_bfp_compression.h"
#include "oai_bfp_compression.h"
#include <xran_pkt.h>
#include <xran_pkt_cp.h>
#include <xran_pkt_up.h>
#include <xran_transport.h>

#if defined(__arm__) || defined(__aarch64__)
#else
// xran_cp_api.h uses SIMD, but does not include it
#include <immintrin.h>
#endif
#include "xran_cp_api.h"
#include "xran_sync_api.h"
#include "oran_isolate.h"
#include "xran_up_api.h"
#include "oran-init.h"
#include "oaioran.h"
#include <rte_ethdev.h>

#include "oran-config.h" // for g_kbar

#include "common/utils/threadPool/notified_fifo.h"

#define RATE_LIMIT(n) if (({ static int _counter = 0; _counter++ % (n) == 0; }))
#define ETHER_TYPE_ECPRI 0xAEFE
#define MAX_NUM_ANTENNAS 4
#define XRAN_GET_MU_FROM_SECT_ID(sectId) (sectId/XRAN_MAX_SECTIONS_PER_SLOT)

notifiedFIFO_t ru_dl_sync_fifo;
extern volatile bool first_call_set;

#define UPLANE_DATA_RING_SIZE 1024
#define NUM_UPLANE_DATA_ELEMENTS (UPLANE_DATA_RING_SIZE - 1)
#define UPLANE_SYMBOL_RING_SIZE 16
#define MAX_UPLANE_PACKETS_PER_SYMBOL (UPLANE_SYMBOL_RING_SIZE - 1)

typedef struct {
  int frame;
  int slot;
  int symbol;
  int num_symbols;
  struct timespec ts;
} ru_dl_sync_info_t;

typedef struct {
  uint16_t cb_symbol_mask;
  int num_symbols[NR_SYMBOLS_PER_SLOT];
  int start_symbol[NR_SYMBOLS_PER_SLOT];
  int symbol_diff;
  int numerology;
} oran_symbol_callback_args_t;

typedef struct {
  void *iq_data;
  void *mbuf_to_free;
  int start_prb;
  int num_prb;
  int iq_width;
  int comp_meth;
  int aatx;
} uplane_data_t;

typedef struct {
  uplane_data_t uplane_data_pool[NUM_UPLANE_DATA_ELEMENTS];
  struct rte_ring *uplane_data_ring;
  struct rte_ring **uplane_symbol_rings;
  _Atomic(uint64_t) up_not_processed;
  _Atomic(uint64_t) up_symbol_pool_exhausted;
  _Atomic(uint64_t) up_received;
  _Atomic(uint64_t) up_processed;
  _Atomic(uint64_t) up_pool_exhausted;
  _Atomic(uint64_t) up_dropped;
  _Atomic(uint64_t) up_late;
  _Atomic(uint64_t) up_early;
  _Atomic(uint64_t) up_malformed;
} packet_processor_context_t;

static packet_processor_context_t packet_processor_context;

static void init_packet_processor_context(packet_processor_context_t *context, int mu)
{
  memset(context, 0, sizeof(packet_processor_context_t));
  context->uplane_data_ring = rte_ring_create("uplane_data_ring", UPLANE_DATA_RING_SIZE, rte_socket_id(), 0);
  AssertFatal(context->uplane_data_ring != NULL, "Failed to create ring uplane_data_ring\n");
  for (int i = 0; i < NUM_UPLANE_DATA_ELEMENTS; i++) {
    int ret = rte_ring_enqueue(context->uplane_data_ring, &context->uplane_data_pool[i]);
    AssertFatal(ret == 0,
                "Failed to push element %d/%d to uplane_data_ring, ring_size %d entries %d free entries %d return value %d \n",
                i,
                NUM_UPLANE_DATA_ELEMENTS,
                UPLANE_DATA_RING_SIZE,
                rte_ring_count(context->uplane_data_ring),
                rte_ring_free_count(context->uplane_data_ring),
                ret);
  }
  LOG_I(HW, "Enqueued %d elements to uplane_data_ring\n", NUM_UPLANE_DATA_ELEMENTS);
  int num_slots_per_frame = 10 << mu;
  int num_symbol_rings = NR_SYMBOLS_PER_SLOT * num_slots_per_frame;
  context->uplane_symbol_rings = malloc(sizeof(struct rte_ring *) * num_symbol_rings);
  for (int i = 0; i < num_symbol_rings; i++) {
    char name[128];
    snprintf(name, sizeof(name), "up_symbol_%d", i);
    struct rte_ring *ring = rte_ring_create(name, UPLANE_SYMBOL_RING_SIZE, rte_socket_id(), 0);
    AssertFatal(ring != NULL, "Failed to create ring %s\n", name);
    context->uplane_symbol_rings[i] = ring;
  }
}

static void clear_old_uplane_packets(packet_processor_context_t *context, int slot_in_frame, int symbol)
{
  uplane_data_t *uplane_data[MAX_UPLANE_PACKETS_PER_SYMBOL];
  struct rte_ring *ring = context->uplane_symbol_rings[slot_in_frame * NR_SYMBOLS_PER_SLOT + symbol];
  int dequeued = rte_ring_dequeue_burst(ring, (void **)&uplane_data, MAX_UPLANE_PACKETS_PER_SYMBOL, NULL);
  if (dequeued > 0) {
    context->up_not_processed += dequeued;
    context->up_dropped += dequeued;
    for (int i = 0; i < dequeued; i++) {
      rte_pktmbuf_free(uplane_data[i]->mbuf_to_free);
    }
    int enqueued = rte_ring_enqueue_burst(context->uplane_data_ring, (void **)uplane_data, dequeued, NULL);
    AssertFatal(enqueued == dequeued, "Failed to push elements to uplane_data_ring\n");
  }
}

static void enqueue_uplane_packet(packet_processor_context_t *context,
                                  void *iq_data,
                                  void *mbuf_to_free,
                                  int start_prb,
                                  int num_prb,
                                  int iq_width,
                                  int comp_meth,
                                  int slot_in_frame,
                                  int symbol,
                                  int aatx)
{
  uplane_data_t *uplane_data;
  context->up_received++;
  int ret = rte_ring_dequeue(context->uplane_data_ring, (void **)&uplane_data);
  if (ret == 0) {
    uplane_data->iq_data = iq_data;
    uplane_data->mbuf_to_free = mbuf_to_free;
    uplane_data->start_prb = start_prb;
    uplane_data->num_prb = num_prb;
    uplane_data->iq_width = iq_width;
    uplane_data->comp_meth = comp_meth;
    uplane_data->aatx = aatx;
    struct rte_ring *ring = context->uplane_symbol_rings[slot_in_frame * NR_SYMBOLS_PER_SLOT + symbol];
    int ret = rte_ring_enqueue(ring, uplane_data);
    if (ret != 0) {
      context->up_symbol_pool_exhausted++;
      context->up_dropped++;
      rte_pktmbuf_free(mbuf_to_free);
      ret = rte_ring_enqueue(context->uplane_data_ring, uplane_data);
      AssertFatal(ret == 0, "Failed to push elements to uplane_data_ring\n");
    }
  } else {
    context->up_pool_exhausted++;
    context->up_dropped++;
    rte_pktmbuf_free(mbuf_to_free);
  }
}

static int32_t pull_uplane_packet_data(packet_processor_context_t *context,
                                       uplane_data_t **uplane_data,
                                       int num_data,
                                       int slot_in_frame,
                                       int symbol)
{
  struct rte_ring *ring = context->uplane_symbol_rings[slot_in_frame * NR_SYMBOLS_PER_SLOT + symbol];
  int ret = rte_ring_dequeue_burst(ring, (void **)uplane_data, num_data, NULL);
  if (ret > 0) {
    context->up_processed += ret;
  }
  return ret;
}

static void push_uplane_packet_data(packet_processor_context_t *context, uplane_data_t **uplane_data, int num_data)
{
  int enqueued = rte_ring_enqueue_burst(context->uplane_data_ring, (void **)uplane_data, num_data, NULL);
  AssertFatal(enqueued == num_data, "Failed to push elements to uplane_data_ring\n");
}

static void print_statistics(packet_processor_context_t *context)
{
  if (context->up_pool_exhausted > 0) {
    LOG_W(HW, "Packets lost due to pool exhaustion: %lu\n", context->up_pool_exhausted);
    context->up_pool_exhausted = 0;
  }
  if (context->up_symbol_pool_exhausted > 0) {
    LOG_W(HW, "Packets lost due to symbol pool exhaustion: %lu\n", context->up_symbol_pool_exhausted);
    context->up_symbol_pool_exhausted = 0;
  }
  if (context->up_not_processed > 0) {
    LOG_W(HW, "Packets not processed by the application layer (application layer too slow): %lu\n", context->up_not_processed);
    context->up_not_processed = 0;
  }
  if (context->up_late > 0) {
    LOG_W(HW, "Packets late: %lu\n", context->up_late);
    context->up_late = 0;
  }
  if (context->up_early > 0) {
    LOG_W(HW, "Packets early: %lu\n", context->up_early);
    context->up_early = 0;
  }
  if (context->up_malformed > 0) {
    LOG_W(HW, "Packets malformed (packet couldn't be processed): %lu\n", context->up_malformed);
    context->up_malformed = 0;
  }
  LOG_I(HW, "RU: packets received %lu\n", context->up_received);
  LOG_I(HW, "RU: packets processed %lu\n", context->up_processed);
  LOG_I(HW, "RU: packets dropped %lu\n", context->up_dropped);
}

typedef struct {
  int section_id;
  int num_prb;
  int start_prb;
  int slot;
  int frame;
  int mu;
  int filter_id;
} oran_prach_cplane_config_t;

typedef struct {
  int section_id;
  int num_prb;
  int start_prb;
  int frame;
  int slot;
  int start_symbol;
  int num_symbols;
  int mu;
  int filter_id;
} oran_pusch_cplane_config_t;

static oran_pusch_cplane_config_t pusch_config[MAX_NUM_ANTENNAS][20][14];

#define PRACH_FRAME_ID_MOD 256
#define PRACH_SLOTS_PER_FRAME 20

static oran_prach_cplane_config_t prach_config_by_frame_slot[MAX_NUM_ANTENNAS][PRACH_FRAME_ID_MOD][PRACH_SLOTS_PER_FRAME] = {0};
static uint8_t prach_seq_id[MAX_NUM_ANTENNAS] = {0};
static _Atomic(uint8_t) pusch_seq_id[MAX_NUM_ANTENNAS] = {0};

#ifdef ORU_PUSCH_CPLANE_DEBUG
static int oru_pusch_msg3_debug_window(int slot, int symbol, int start_prb, int num_prb, int nonzero_iq)
{
  return nonzero_iq > 0 || num_prb <= 8 || (slot == 18 && symbol >= 10 && symbol <= 13);
}
#endif

static int get_oru_prach_frame_adjust(void)
{
  static int initialized = 0;
  static int adjust = 0;
  if (!initialized) {
    const char *env = getenv("ORU_PRACH_FRAME_ADJUST");
    if (env != NULL)
      adjust = atoi(env);
    LOG_I(HW, "ORU PRACH frame adjust %d frame(s)\n", adjust);
    initialized = 1;
  }
  return adjust;
}

static int adjusted_prach_frame(int frame)
{
  int adjusted = (frame + get_oru_prach_frame_adjust()) % 1024;
  if (adjusted < 0)
    adjusted += 1024;
  return adjusted;
}

extern int32_t xran_ethdi_mbuf_send(struct rte_mbuf *mb, uint16_t ethertype, uint16_t vf_id);
extern uint16_t xran_map_ecpriPcid_to_vf(void *p_dev_ctx, int32_t dir, int32_t cc_id, int32_t ru_port_id);

void symbol_callback(void *args, struct xran_sense_of_time *p_sense_of_time)
{
  static int call_count = 0;
  static int first_call_true_count = 0;
  call_count++;
  if (call_count < 10 || (first_call_set && first_call_true_count++ < 10)) {
    LOG_D(HW, "[ORU] symbol_callback called, count=%d, first_call_set=%d\n", call_count, first_call_set);
  }
  if (!first_call_set) {
    return;
  }

  oran_symbol_callback_args_t *callback_args = args;
  int num_slots_per_subframe = 1 << callback_args->numerology;
  int num_slots_per_frame = 10 << callback_args->numerology;
  int slot_in_frame = p_sense_of_time->nSlotIdx + p_sense_of_time->nSubframeIdx * num_slots_per_subframe;
  clear_old_uplane_packets(&packet_processor_context, slot_in_frame, p_sense_of_time->nSymIdx);

  if ((callback_args->cb_symbol_mask & (1 << p_sense_of_time->nSymIdx)) == 0) {
    return;
  }

  int num_symbols = callback_args->num_symbols[p_sense_of_time->nSymIdx];
  int start_symbol = callback_args->start_symbol[p_sense_of_time->nSymIdx];

  // Adjust timing by symbol_diff
  int slot_index_increments = (p_sense_of_time->nSymIdx + callback_args->symbol_diff) / NR_SYMBOLS_PER_SLOT;

  int target_slot_in_frame =
      p_sense_of_time->nSlotIdx + p_sense_of_time->nSubframeIdx * num_slots_per_subframe + slot_index_increments;
  int frame = p_sense_of_time->nFrameIdx;
  while (target_slot_in_frame >= num_slots_per_frame) {
    target_slot_in_frame -= num_slots_per_frame;
    frame++;
    if (frame >= 1024) {
      frame = 0;
    }
  }

  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  int mu = fh_cfg->frame_conf.nNumerology;
  AssertFatal(mu == 1, "Only numerology 1 supported for RU\n");

  LOG_D(HW,
        "Callback triggered at frame.slot.symbol %d.%d.%d targets %d.%d.%d, num_symbols %d, symbol_diff %d\n",
        p_sense_of_time->nFrameIdx,
        slot_in_frame,
        p_sense_of_time->nSymIdx,
        frame,
        target_slot_in_frame,
        start_symbol,
        num_symbols,
        callback_args->symbol_diff);

  notifiedFIFO_elt_t *req = newNotifiedFIFO_elt(sizeof(ru_dl_sync_info_t), 0, NULL, NULL);
  ru_dl_sync_info_t *info = NotifiedFifoData(req);
  info->frame = frame;
  info->slot = target_slot_in_frame;
  info->symbol = start_symbol;
  info->num_symbols = num_symbols;

  int slot_duration_uS[] = {1000, 500, 250, 125};
  uint64_t slot_in_second_offset_nS = ((uint64_t)p_sense_of_time->tti_counter * slot_duration_uS[mu]) * 1000UL;

  float symbol_duration_nS = ((float)slot_duration_uS[mu] * 1000) / 14.0f;
  uint64_t symbol_in_slot_offset_nS = (uint64_t)((p_sense_of_time->nSymIdx + callback_args->symbol_diff) * symbol_duration_nS);

  info->ts.tv_sec = p_sense_of_time->nSecond;
  info->ts.tv_nsec = slot_in_second_offset_nS + symbol_in_slot_offset_nS;
  if (info->ts.tv_nsec >= 1000000000UL) {
    info->ts.tv_sec += 1;
    info->ts.tv_nsec -= 1000000000UL;
  }

  AssertFatal(info->ts.tv_nsec < 1000000000UL, "ORAN: Invalid tv_nsec %ld\n", info->ts.tv_nsec);

  static int push_count = 0;
  if (push_count++ < 10) {
    LOG_D(HW, "[ORU] Pushing DL sync to FIFO: frame=%d, slot=%d, symbol=%d, count=%d\n", info->frame, info->slot, info->symbol, push_count);
  }
  pushNotifiedFIFO(&ru_dl_sync_fifo, req);
}

int xran_oru_tx_read_slot(uint32_t **txdataF, int nb_tx, int *frame, int *slot, int *symbol, int *num_symbols, struct timespec *ts)
{
  static int call_count = 0;
  call_count++;
  if (call_count <= 10 || call_count % 1000 == 0) {
    LOG_D(HW, "[ORU north] xran_oru_tx_read_slot called, count=%d\n", call_count);
  }

  notifiedFIFO_elt_t *res = pullNotifiedFIFO(&ru_dl_sync_fifo);
  ru_dl_sync_info_t *info = NotifiedFifoData(res);

  *slot = info->slot;
  *frame = info->frame;
  *symbol = info->symbol;
  *num_symbols = info->num_symbols;
  *ts = info->ts;

  if (call_count <= 10) {
    LOG_D(HW, "[ORU north] Read from FIFO: frame=%d, slot=%d, symbol=%d, num_symbols=%d\n",
          *frame, *slot, *symbol, *num_symbols);
  }

  delNotifiedFIFO_elt(res);

  if (*frame % 256 == 0 && *slot == 0 && *symbol == 0) {
    print_statistics(&packet_processor_context);
  }

  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  int nPRBs = fh_cfg->nDLRBs;
  int fftsize = 1 << fh_cfg->nDLFftSize;

  int first_carrier_offset = fftsize - (nPRBs * NR_NB_SC_PER_RB / 2);
  int num_sc_first_copy = (fftsize - first_carrier_offset);
  int num_sc_second_copy = nPRBs * NR_NB_SC_PER_RB - num_sc_first_copy;

  c16_t tx_data_sym[nb_tx][nPRBs * NR_NB_SC_PER_RB];
  static int total_symbols_processed = 0;
  static int symbols_with_packets = 0;
  for (int sym = *symbol; sym < *symbol + *num_symbols; sym++) {
    uplane_data_t *uplane_data[MAX_UPLANE_PACKETS_PER_SYMBOL];
    int num_packets = pull_uplane_packet_data(&packet_processor_context, uplane_data, MAX_UPLANE_PACKETS_PER_SYMBOL, *slot, sym);
    total_symbols_processed++;
    if (num_packets) {
      symbols_with_packets++;
      if (symbols_with_packets <= 10 || symbols_with_packets % 100 == 0) {
        LOG_D(HW, "[ORU north] Symbol %d has %d packets (total symbols: %d, with packets: %d)\n",
              sym, num_packets, total_symbols_processed, symbols_with_packets);
      }
      memset(tx_data_sym, 0, sizeof(tx_data_sym));
      // AssertFatal(comp_meth == XRAN_COMPMETHOD_NONE, "Compression not supported\n");
      static int dl_decomp_log_count = 0;
      for (int i = 0; i < num_packets; i++) {
          int start_prb = uplane_data[i]->start_prb;
          int num_prb = uplane_data[i]->num_prb;
          int comp_meth = uplane_data[i]->comp_meth;
          int aatx = uplane_data[i]->aatx;
          void *iq_data = uplane_data[i]->iq_data;
          uint16_t *source = (uint16_t *)iq_data;
          int16_t *destination = (int16_t *)&tx_data_sym[aatx][start_prb * NR_NB_SC_PER_RB];

          if (dl_decomp_log_count < 10) {
            LOG_D(HW, "[ORU DL RX] sym=%d, packet %d/%d: comp_meth=%d, iq_width=%d, start_prb=%d, num_prb=%d\n",
                  sym, i, num_packets, comp_meth, uplane_data[i]->iq_width, start_prb, num_prb);
            dl_decomp_log_count++;
          }

          if (comp_meth == XRAN_COMPMETHOD_NONE) {
          for (int j = 0; j < num_prb * NR_NB_SC_PER_RB * 2; j++) {
            destination[j] = (int16_t)ntohs(source[j]);
          }
        } else if (comp_meth == XRAN_COMPMETHOD_BLKFLOAT) {
              struct xranlib_decompress_request bfp_decom_req = {};
              struct xranlib_decompress_response bfp_decom_rsp = {};

              bfp_decom_req.data_in = (int8_t *)source;
              bfp_decom_req.numRBs = num_prb;
              bfp_decom_req.len = (3 * uplane_data[i]->iq_width + 1) *num_prb;
              bfp_decom_req.compMethod = comp_meth;
              bfp_decom_req.iqWidth = uplane_data[i]->iq_width;

              bfp_decom_rsp.data_out = (int16_t *)&tx_data_sym[aatx][start_prb * NR_NB_SC_PER_RB];
              bfp_decom_rsp.len = 0;

              oai_bfp_decompression(bfp_decom_req.iqWidth, bfp_decom_req.numRBs, bfp_decom_req.data_in, bfp_decom_rsp.data_out);
              bfp_decom_rsp.len = bfp_decom_req.numRBs * 24 * sizeof(int16_t);
            }
              else if (comp_meth == XRAN_COMPMETHOD_BLKSCALE) {
              struct xranlib_decompress_request bs_decom_req = {};
              struct xranlib_decompress_response bs_decom_rsp = {};

              bs_decom_req.data_in = (int8_t *)source;
              bs_decom_req.numRBs = num_prb;
              bs_decom_req.len = (3 * uplane_data[i]->iq_width + 1) * num_prb;
              bs_decom_req.compMethod = comp_meth;
              bs_decom_req.iqWidth = uplane_data[i]->iq_width;

              bs_decom_rsp.data_out = (int16_t *)&tx_data_sym[aatx][start_prb * NR_NB_SC_PER_RB];
              bs_decom_rsp.len = 0;

              xranlib_decompress_blkscale_avx512(&bs_decom_req, &bs_decom_rsp);
            }

            else if (comp_meth == XRAN_COMPMETHOD_ULAW) {
              struct xranlib_decompress_request ulaw_decom_req = {};
              struct xranlib_decompress_response ulaw_decom_rsp = {};

              ulaw_decom_req.data_in = (int8_t *)source;
              ulaw_decom_req.numRBs = num_prb;
              ulaw_decom_req.len = (3 * uplane_data[i]->iq_width + 1) * num_prb;
              ulaw_decom_req.compMethod = comp_meth;
              ulaw_decom_req.iqWidth = uplane_data[i]->iq_width;

              ulaw_decom_rsp.data_out = (int16_t *)&tx_data_sym[aatx][start_prb * NR_NB_SC_PER_RB];
              ulaw_decom_rsp.len = 0;

              xranlib_decompress_ulaw_avx512(&ulaw_decom_req, &ulaw_decom_rsp);
            } else {                                                                                                                                                     
                  AssertFatal(0, "Unsupported DL compression method %d\n", comp_meth);                                                                                       
            }                                                                                                                                                            
        

        rte_pktmbuf_free(uplane_data[i]->mbuf_to_free);
      }
      push_uplane_packet_data(&packet_processor_context, uplane_data, num_packets);
    }

    if (num_packets) {
      for (int aatx = 0; aatx < nb_tx; aatx++) {
        memcpy(&txdataF[aatx][fftsize * sym + first_carrier_offset], tx_data_sym[aatx], num_sc_first_copy * sizeof(c16_t));
        memcpy(&txdataF[aatx][fftsize * sym], &tx_data_sym[aatx][num_sc_first_copy], num_sc_second_copy * sizeof(c16_t));
      }
    } else {
      static int no_packet_count = 0;
      no_packet_count++;
      if (no_packet_count <= 20 || no_packet_count % 1000 == 0) {
        LOG_D(HW, "[ORU north] No packets for frame=%d, slot=%d, symbol=%d (count=%d)\n",
              *frame, *slot, sym, no_packet_count);
      }
      for (int aatx = 0; aatx < nb_tx; aatx++) {
        memset(&txdataF[aatx][sym * fftsize], 0, sizeof(c16_t) * fftsize);
      }
    }
  }
  return 0;
}

int process_ru_uplane(struct rte_mbuf *pkt,
                      void *handle,
                      struct xran_eaxc_info *p_cid,
                      uint16_t port_id,
                      struct xran_sense_of_time *p_sense_of_time)
{
  static int call_count = 0;
  if (call_count++ < 10) {
    LOG_D(HW, "[ORU] process_ru_uplane called, count=%d\n", call_count);
  }
  if (!first_call_set) {
    return MBUF_FREE;
  }
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  void *iq_data_start = NULL;
  uint8_t CC_ID = 0xFF;
  uint8_t Ant_ID = 0xFF;
  uint8_t frame_id = 0;
  uint8_t subframe_id = 0;
  uint8_t slot_id = 0;
  uint8_t symb_id = 0;
  uint8_t filter_id = 0;
  union ecpri_seq_id seq_id = {0};
  uint16_t num_prbu = 0;
  uint16_t start_prbu = 0;
  uint16_t sym_inc = 0;
  uint16_t rb = 0;
  uint16_t sect_id = 0;
  int expect_comp = fh_cfg->ru_conf.compMeth != XRAN_COMPMETHOD_NONE;
  enum xran_comp_hdr_type staticComp = fh_cfg->ru_conf.xranCompHdrType;
  uint8_t compMeth = XRAN_COMPMETHOD_NONE;
  uint8_t iqWidth = 0;
  uint8_t is_prach = 0;
  int ret = xran_extract_iq_samples(pkt,
                                    &iq_data_start,
                                    &CC_ID,
                                    &Ant_ID,
                                    &frame_id,
                                    &subframe_id,
                                    &slot_id,
                                    &symb_id,
                                    &filter_id,
                                    &seq_id,
                                    &num_prbu,
                                    &start_prbu,
                                    &sym_inc,
                                    &rb,
                                    &sect_id,
                                    expect_comp,
                                    staticComp,
                                    &compMeth,
                                    &iqWidth,
                                    &is_prach);
  if (ret == 0) {
    packet_processor_context.up_malformed++;
    packet_processor_context.up_dropped++;
    return MBUF_FREE;
  }
  LOG_D(HW,
        "ORAN: U-plane packet received. CC_ID %d, Ant_ID %d, frame_id %d, subframe_id %d, slot_id %d, symb_id %d, filter_id %d, "
        "num_prbu %d, start_prbu %d, sym_inc %d, rb %d, sect_id %d, compMeth %d, iqWidth %d, is_prach %d\n",
        CC_ID,
        Ant_ID,
        frame_id,
        subframe_id,
        slot_id,
        symb_id,
        filter_id,
        num_prbu,
        start_prbu,
        sym_inc,
        rb,
        sect_id,
        compMeth,
        iqWidth,
        is_prach);

  // AssertFatal(compMeth == XRAN_COMPMETHOD_NONE, "Compression not supported\n");
  int mu = fh_cfg->frame_conf.nNumerology;
  int slots_per_subframe = 1 << mu;
  int current_symbol_index = NR_SYMBOLS_PER_SLOT * (p_sense_of_time->nSlotIdx + p_sense_of_time->nSubframeIdx * slots_per_subframe) + p_sense_of_time->nSymIdx;
  int target_symbol_index = NR_SYMBOLS_PER_SLOT * (slot_id + subframe_id * slots_per_subframe) + symb_id;
  int diff = target_symbol_index - current_symbol_index;
  int max_sym = NR_SYMBOLS_PER_SLOT * NR_NUMBER_OF_SUBFRAMES_PER_FRAME * slots_per_subframe;
  if (diff < -max_sym / 2) {
    diff += max_sym;
  }

  int slot_duration_uS = 1000 / (1 << mu);
  int symbol_duration_uS = slot_duration_uS / NR_SYMBOLS_PER_SLOT;
  int rx_window_start = fh_cfg->T2a_max_up / symbol_duration_uS;
  int rx_window_end = fh_cfg->T2a_min_up / symbol_duration_uS;
  if (diff < rx_window_end) {
    packet_processor_context.up_late++;
    return MBUF_FREE;
  } else if (diff > rx_window_start) {
    packet_processor_context.up_early++;
  }


  int num_slots_per_frame = NR_NUMBER_OF_SUBFRAMES_PER_FRAME * slots_per_subframe;
  int slot_in_frame = slot_id + subframe_id * slots_per_subframe;
  if (slot_in_frame >= num_slots_per_frame && slot_id < num_slots_per_frame) {
    slot_in_frame = slot_id;
  }
  static int parsed_log_count = 0;
  if (parsed_log_count < 20) {
    LOG_D(HW, "[ORU] U-plane parsed: port=%u frame=%u subframe=%u slot=%u slot_in_frame=%d symbol=%u ant=%u start_prb=%u num_prb=%u ret=%d\n",
          port_id, frame_id, subframe_id, slot_id, slot_in_frame, symb_id, Ant_ID, start_prbu, num_prbu, ret);
    parsed_log_count++;
  }
  if (slot_in_frame < 0 || slot_in_frame >= num_slots_per_frame || symb_id >= NR_SYMBOLS_PER_SLOT || Ant_ID >= fh_cfg->neAxc ||
      start_prbu >= fh_cfg->nDLRBs || num_prbu == 0 || start_prbu + num_prbu > fh_cfg->nDLRBs) {
    LOG_W(HW, "[ORU] Drop invalid U-plane packet: port=%u frame=%u subframe=%u slot=%u slot_in_frame=%d symbol=%u ant=%u start_prb=%u num_prb=%u nDLRBs=%u neAxc=%u\n",
          port_id, frame_id, subframe_id, slot_id, slot_in_frame, symb_id, Ant_ID, start_prbu, num_prbu, fh_cfg->nDLRBs, fh_cfg->neAxc);
    packet_processor_context.up_malformed++;
    packet_processor_context.up_dropped++;
    return MBUF_FREE;
  }

  enqueue_uplane_packet(&packet_processor_context,
                        iq_data_start,
                        pkt,
                        start_prbu,
                        num_prbu,
                        iqWidth,
                        compMeth,
                        slot_in_frame,
                        symb_id,
                        Ant_ID);

  return MBUF_KEEP;
}

int32_t process_ru_cplane(struct rte_mbuf *pkt, void *handle, uint16_t port_id, struct xran_sense_of_time *p_sense_of_time)
{
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  struct xran_ecpri_hdr *ecpri_hdr;
  struct xran_recv_packet_info xran_recv_packet_info;
  int ret = xran_parse_ecpri_hdr(pkt, &ecpri_hdr, &xran_recv_packet_info);
  if (ret != XRAN_STATUS_SUCCESS) {
    return MBUF_FREE;
  }
  struct xran_cp_radioapp_common_header *apphdr = (void *)rte_pktmbuf_adj(pkt, sizeof(struct xran_ecpri_hdr));
  if (apphdr == NULL) {
    LOG_W(HW, "issue extracting apphdr\n");
    return MBUF_FREE;
  }
  apphdr->field.all_bits = rte_be_to_cpu_32(apphdr->field.all_bits);
  if (apphdr->field.payloadVer != XRAN_PAYLOAD_VER) {
    LOG_W(HW, "Invalid payloadVer field %d\n", apphdr->field.payloadVer);
    return MBUF_FREE;
  }

  switch (apphdr->sectionType) {
    case XRAN_CP_SECTIONTYPE_3: {
      struct xran_cp_radioapp_section3_header *hdr = (struct xran_cp_radioapp_section3_header *)apphdr;
      if (hdr->cmnhdr.numOfSections != 1) {
        LOG_W(HW, "Only support one section\n");
        return MBUF_FREE;
      }

      hdr->timeOffset = rte_be_to_cpu_16(hdr->timeOffset);
      hdr->cpLength = rte_be_to_cpu_16(hdr->cpLength);

      struct xran_cp_radioapp_section3 *section = (void *)rte_pktmbuf_adj(pkt, sizeof(struct xran_cp_radioapp_section3_header));
      if (section == NULL) {
        LOG_W(HW, "Issue extracting section\n");
        return MBUF_FREE;
      }
      *((uint64_t *)section) = rte_be_to_cpu_64(*((uint64_t *)section));
      int mu = fh_cfg->frame_conf.nNumerology;
      int aarx = xran_recv_packet_info.eaxc.ruPortId - fh_cfg->prach_conf.eAxC_offset;
      int slot = hdr->cmnhdr.field.slotId + hdr->cmnhdr.field.subframeId * (1 << mu);
      if (aarx < 0 || aarx >= MAX_NUM_ANTENNAS || slot < 0 || slot >= PRACH_SLOTS_PER_FRAME) {
        LOG_W(HW,
              "Invalid PRACH C-plane config: frame=%d subframe=%d slot_id=%d slot=%d aarx=%d eAxC_offset=%u\n",
              hdr->cmnhdr.field.frameId,
              hdr->cmnhdr.field.subframeId,
              hdr->cmnhdr.field.slotId,
              slot,
              aarx,
              fh_cfg->prach_conf.eAxC_offset);
        return MBUF_FREE;
      }
      oran_prach_cplane_config_t prach_config = {
          .frame = hdr->cmnhdr.field.frameId,
          .slot = slot,
          .num_prb = section->hdr.u1.common.numPrbc,
          .start_prb = section->hdr.u1.common.startPrbc,
          .section_id = section->hdr.u1.common.sectionId,
          .mu = mu,
          .filter_id = hdr->cmnhdr.field.filterIndex
      };
      prach_config_by_frame_slot[aarx][prach_config.frame][slot] = prach_config;
#ifdef ORU_PRACH_CPLANE_DEBUG
      static int prach_cp_rx_dbg_count = 0;
      if (prach_cp_rx_dbg_count < 64) {
        LOG_A(HW,
              "[ORU PRACH CP RX] frame=%d subframe=%d slot_id=%d slot=%d aarx=%d section=%d start_prb=%d num_prb=%d filter=%d mu=%d eaxc=%u\n",
              hdr->cmnhdr.field.frameId,
              hdr->cmnhdr.field.subframeId,
              hdr->cmnhdr.field.slotId,
              slot,
              aarx,
              prach_config.section_id,
              prach_config.start_prb,
              prach_config.num_prb,
              prach_config.filter_id,
              mu,
              xran_recv_packet_info.eaxc.ruPortId);
        prach_cp_rx_dbg_count++;
      }
#endif
      return MBUF_FREE;
    }
    case XRAN_CP_SECTIONTYPE_1: {
      struct xran_cp_radioapp_section1_header *hdr = (struct xran_cp_radioapp_section1_header *)apphdr;
      struct xran_cp_radioapp_section1 *section = (void *)rte_pktmbuf_adj(pkt, sizeof(struct xran_cp_radioapp_section1_header));
      if (section == NULL) {
        LOG_W(HW, "Issue extracting section\n");
        return MBUF_FREE;
      }
      *((uint64_t *)section) = rte_be_to_cpu_64(*((uint64_t *)section));
      if (hdr->cmnhdr.field.dataDirection == XRAN_DIR_DL) {
        // For now skip DL
        return MBUF_FREE;
      }
      //int frame = hdr->cmnhdr.field.frameId;
      int section_id = section->hdr.u1.common.sectionId;
      int mu = fh_cfg->frame_conf.nNumerology;
      int slot = hdr->cmnhdr.field.slotId + hdr->cmnhdr.field.subframeId * (1 << mu);
      uint32_t start_symbol = hdr->cmnhdr.field.startSymbolId;
      int startPrbc = section->hdr.u1.common.startPrbc;
      int numPrbc = section->hdr.u1.common.numPrbc;
      int num_symbols = section->hdr.u.s1.numSymbol;
      int aarx = xran_recv_packet_info.eaxc.ruPortId;
      if (aarx < 0 || aarx >= MAX_NUM_ANTENNAS || slot < 0 || slot >= 20) {
        LOG_W(HW,
              "Invalid PUSCH C-plane config: frame=%d subframe=%d slot_id=%d slot=%d aarx=%d start_sym=%u num_sym=%d start_prb=%d num_prb=%d\n",
              hdr->cmnhdr.field.frameId,
              hdr->cmnhdr.field.subframeId,
              hdr->cmnhdr.field.slotId,
              slot,
              aarx,
              start_symbol,
              num_symbols,
              startPrbc,
              numPrbc);
        return MBUF_FREE;
      }
      if (start_symbol + num_symbols > 14) {
        LOG_W(HW, "Invalid symbol index >= 14 start_symbol %d num_symbols %d\n", start_symbol, num_symbols);
      }
      LOG_D(HW,
            "section_id %d mu %d slot %d frame %d startprb %d num_prb %d start_sym %u num_sym %d\n",
            section_id,
            mu,
            slot,
            hdr->cmnhdr.field.frameId,
            startPrbc,
            numPrbc,
            start_symbol,
            num_symbols);
#ifdef ORU_PUSCH_CPLANE_DEBUG
      static int pusch_cp_rx_dbg_count = 0;
      const int pusch_cp_msg3_debug = oru_pusch_msg3_debug_window(slot, start_symbol, startPrbc, numPrbc, 0);
      if (pusch_cp_msg3_debug && pusch_cp_rx_dbg_count < 4096) {
        LOG_A(HW,
              "[ORU PUSCH CP RX] frame=%d subframe=%d slot_id=%d slot=%d aarx=%d section=%d start_prb=%d num_prb=%d start_sym=%u num_sym=%d filter=%d mu=%d eaxc=%u\n",
              hdr->cmnhdr.field.frameId,
              hdr->cmnhdr.field.subframeId,
              hdr->cmnhdr.field.slotId,
              slot,
              aarx,
              section_id,
              startPrbc,
              numPrbc,
              start_symbol,
              num_symbols,
              hdr->cmnhdr.field.filterIndex,
              mu,
              xran_recv_packet_info.eaxc.ruPortId);
        pusch_cp_rx_dbg_count++;
      }
#endif
      for (int symbol = start_symbol; symbol < start_symbol + num_symbols && symbol < 14; symbol++) {
        pusch_config[aarx][slot][symbol].section_id = section_id;
        pusch_config[aarx][slot][symbol].start_prb = startPrbc;
        pusch_config[aarx][slot][symbol].num_prb = numPrbc;
        pusch_config[aarx][slot][symbol].frame = hdr->cmnhdr.field.frameId;
        pusch_config[aarx][slot][symbol].slot = slot;
        pusch_config[aarx][slot][symbol].start_symbol = start_symbol;
        pusch_config[aarx][slot][symbol].num_symbols = num_symbols;
        pusch_config[aarx][slot][symbol].mu = mu;
        pusch_config[aarx][slot][symbol].filter_id = hdr->cmnhdr.field.filterIndex;
      }
      return MBUF_FREE;
    }
    default:
      return MBUF_FREE;
  }
}

void init_oru_packet_processor(void *handle, int callbacks_per_slot)
{
  AssertFatal(callbacks_per_slot <= NR_SYMBOLS_PER_SLOT,
              "Can do at most %d callbacks per slot",
              NR_SYMBOLS_PER_SLOT);
  static bool installed = false;
  AssertFatal(!installed, "Cannot init oru twice\n");
  installed = true;
  for (int aarx = 0; aarx < MAX_NUM_ANTENNAS; aarx++) {
    for (int frame = 0; frame < PRACH_FRAME_ID_MOD; frame++) {
      for (int slot = 0; slot < PRACH_SLOTS_PER_FRAME; slot++) {
        prach_config_by_frame_slot[aarx][frame][slot].section_id = -1;
        prach_config_by_frame_slot[aarx][frame][slot].num_prb = -1;
        prach_config_by_frame_slot[aarx][frame][slot].start_prb = -1;
        prach_config_by_frame_slot[aarx][frame][slot].slot = -1;
        prach_config_by_frame_slot[aarx][frame][slot].frame = -1;
      }
    }
  }
  for (int aarx = 0; aarx < MAX_NUM_ANTENNAS; aarx++) {
    for (int slot = 0; slot < 20; slot++) {
      for (int symbol = 0; symbol < 14; symbol++) {
        pusch_config[aarx][slot][symbol].section_id = -1;
        pusch_config[aarx][slot][symbol].frame = -1;
        pusch_config[aarx][slot][symbol].slot = -1;
        pusch_config[aarx][slot][symbol].start_symbol = -1;
        pusch_config[aarx][slot][symbol].num_symbols = -1;
        pusch_config[aarx][slot][symbol].start_prb = -1;
        pusch_config[aarx][slot][symbol].num_prb = -1;
      }
    }
  }


  // Represents RX window end
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  int mu = fh_cfg->frame_conf.nNumerology;
  init_packet_processor_context(&packet_processor_context, mu);
  uint32_t T2a_min = fh_cfg->T2a_min_up;
  int slot_duration_uS[] = {1000, 500, 250, 125};
  float symbol_duration_nS = ((float)slot_duration_uS[mu] * 1000) / 14.0f;
  uint32_t symbol_offset = (float)(T2a_min * 1000) / symbol_duration_nS;
  AssertFatal(symbol_offset > 0, "The amount of time after RX window end for O-RU is 0. Adjust T2a_min_up %u [uS]\n", T2a_min);
  LOG_I(HW, "Installing %d callbacks %d symbols before OTA\n", callbacks_per_slot, symbol_offset);

  static oran_symbol_callback_args_t args = {0};
  args.numerology = mu;

  int symbols_per_callback = NR_SYMBOLS_PER_SLOT / callbacks_per_slot;

  int start_symbol = 0;
  for (int i = 0; i < callbacks_per_slot; i++) {
    int extra_symbols = 0;
    if (i == callbacks_per_slot - 1) {
      // Extend last callback to include leftover symbols
      extra_symbols += NR_SYMBOLS_PER_SLOT % callbacks_per_slot;
    }
    int num_sybmols_this_callback = symbols_per_callback + extra_symbols;
    int end_symbol = start_symbol + num_sybmols_this_callback - 1;
    int callback_symbol = (end_symbol - symbol_offset + NR_SYMBOLS_PER_SLOT) % NR_SYMBOLS_PER_SLOT;
    args.cb_symbol_mask |= (1U << callback_symbol);
    args.num_symbols[callback_symbol] = num_sybmols_this_callback;
    args.start_symbol[callback_symbol] = start_symbol;
    args.symbol_diff = symbol_offset;
    start_symbol += symbols_per_callback;
  }
  xran_hook_install(handle, process_ru_uplane, NULL, process_ru_cplane, NULL, symbol_callback, &args, mu);
}

extern void *xran_ethdi_mbuf_alloc(void);

void fill_ecpri_header(struct xran_ecpri_hdr *ecpri_header,
                       uint8_t ecpri_mesg_type,
                       size_t ecpri_payload_size,
                       uint8_t CC_ID,
                       uint8_t Ant_ID,
                       uint8_t seq_id,
                       uint8_t oxu_port_id)
{
  ecpri_header->cmnhdr.data.data_num_1 = 0x0;
  ecpri_header->cmnhdr.bits.ecpri_ver = XRAN_ECPRI_VER;
  ecpri_header->cmnhdr.bits.ecpri_mesg_type = ecpri_mesg_type;
  ecpri_header->cmnhdr.bits.ecpri_payl_size = rte_cpu_to_be_16(ecpri_payload_size);
  ecpri_header->ecpri_xtc_id = xran_compose_cid(0, 0, CC_ID, Ant_ID);
  ecpri_header->ecpri_seq_id.bits.seq_id = seq_id;
  ecpri_header->ecpri_seq_id.bits.e_bit = 1;
  ecpri_header->ecpri_seq_id.bits.sub_seq_id = 0;
  /// No byteswap for ecpri_seq_id. Possibly because of inverse definition in xran
}

void fill_radio_app_header(struct radio_app_common_hdr *radio_app_header,
                           int filter_id,
                           int direction,
                           int frame,
                           int slot,
                           int symbol,
                           int mu)
{
  radio_app_header->frame_id = frame & 0xff;
  radio_app_header->sf_slot_sym.slot_id = slot % (1 << mu);
  radio_app_header->sf_slot_sym.subframe_id = slot / (1 << mu);
  radio_app_header->sf_slot_sym.symb_id = symbol;
  radio_app_header->sf_slot_sym.value = rte_cpu_to_be_16(radio_app_header->sf_slot_sym.value);
  radio_app_header->data_feature.data_direction = direction;
  radio_app_header->data_feature.payl_ver = 1;
  radio_app_header->data_feature.filter_id = filter_id;
}

void fill_data_section_header(struct data_section_hdr *data_section_hdr, int num_prb, int start_prb, int section_id)
{
  data_section_hdr->fields.all_bits = 0;
  data_section_hdr->fields.num_prbu = (uint8_t)XRAN_CONVERT_NUMPRBC(num_prb);
  data_section_hdr->fields.start_prbu = (start_prb & 0x03ff);
  data_section_hdr->fields.sect_id = section_id;
  data_section_hdr->fields.all_bits = rte_cpu_to_be_32(data_section_hdr->fields.all_bits);
}

void xran_oru_send_prach(uint32_t *prachF, int aarx, int frame, int slot, int symbol)
{
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  uint8_t mu = fh_cfg->frame_conf.nNumerology;

  // AssertFatal(fh_cfg->ru_conf.compMeth_PRACH == XRAN_COMPMETHOD_NONE, "Compression not supported\n");
  // TODO: With compression, have to add compression header to header_len
  size_t header_length = sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr) + sizeof(struct data_section_hdr);

  // TODO: For compression, have to re-evaluate data size;
  // TODO: Only support short format PRACH
  const uint prach_length = 139;
  const uint num_prb = 12;
  const int prach_payload_iq16 = num_prb * 12 * 2;
  size_t data_len = sizeof(int16_t) * prach_payload_iq16;
  if (fh_cfg->ru_conf.compMeth_PRACH != XRAN_COMPMETHOD_NONE)
    data_len = (3 * fh_cfg->ru_conf.iqWidth_PRACH + 1) * num_prb;

  int tx_frame = adjusted_prach_frame(frame);
  int tx_frame8 = tx_frame & 0xff;
  if (aarx < 0 || aarx >= MAX_NUM_ANTENNAS || slot < 0 || slot >= PRACH_SLOTS_PER_FRAME) {
    LOG_W(HW, "Invalid PRACH TX lookup: frame.slot %d.%d adjusted %d.%d frame8 %d aarx %d\n", frame, slot, tx_frame, slot, tx_frame8, aarx);
    return;
  }
  oran_prach_cplane_config_t *prach_config = &prach_config_by_frame_slot[aarx][tx_frame8][slot];
  if (prach_config->section_id == -1) {
#ifdef ORU_PRACH_CPLANE_DEBUG
    static int prach_no_cp_dbg_count = 0;
    if (prach_no_cp_dbg_count < 64) {
      LOG_A(HW,
            "[ORU PRACH NO CP] rx_frame.slot.symbol=%d.%d.%d adjusted=%d.%d frame8=%d aarx=%d\n",
            frame,
            slot,
            symbol,
            tx_frame,
            slot,
            tx_frame8,
            aarx);
      prach_no_cp_dbg_count++;
    }
#endif
    RATE_LIMIT(1000)
      LOG_W(HW,
            "PRACH was not configured for frame.slot %d.%d adjusted %d.%d frame8 %d: no C-plane config cached\n",
            frame,
            slot,
            tx_frame,
            slot,
            tx_frame8);
    return;
  }
  if (prach_config->frame != tx_frame8 || prach_config->slot != slot) {
    RATE_LIMIT(1000)
      LOG_W(HW,
            "PRACH cache corruption for frame.slot %d.%d adjusted %d.%d frame8 %d, configuration is for frame.slot %d.%d\n",
            frame,
            slot,
            tx_frame,
            slot,
            tx_frame8,
            prach_config->frame,
            prach_config->slot);
    return;
  }

  struct rte_mbuf *mbuf = xran_ethdi_mbuf_alloc();
  AssertFatal(mbuf != NULL, "out of mbufs\n");
  char *buf = rte_pktmbuf_append(mbuf, header_length + data_len);
  AssertFatal(buf, "incorrect mbuf size\n");

  struct xran_ecpri_hdr *ecpri_header = (struct xran_ecpri_hdr *)rte_pktmbuf_mtod(mbuf, char *);
  uint16_t ecpri_payload_size = xran_get_ecpri_hdr_size() + sizeof(struct radio_app_common_hdr) + sizeof(struct data_section_hdr);
  // Add compression header size in DYNAMIC mode when compression is enabled
  if (fh_cfg->ru_conf.compMeth_PRACH != XRAN_COMPMETHOD_NONE && fh_cfg->ru_conf.xranCompHdrType == XRAN_COMP_HDR_TYPE_DYNAMIC) {
    ecpri_payload_size += sizeof(struct data_section_compression_hdr);
  }
  ecpri_payload_size += data_len;
  fill_ecpri_header(ecpri_header, ECPRI_IQ_DATA, ecpri_payload_size, 0, aarx + fh_cfg->prach_conf.eAxC_offset, prach_seq_id[aarx]++, 0);

  struct radio_app_common_hdr *radio_app_header = (struct radio_app_common_hdr *)(ecpri_header + 1);
  fill_radio_app_header(radio_app_header, prach_config->filter_id, XRAN_DIR_UL, tx_frame, slot, symbol, mu);

  struct data_section_hdr *data_section_header = (struct data_section_hdr *)(radio_app_header + 1);
  fill_data_section_header(data_section_header, prach_config->num_prb, prach_config->start_prb, prach_config->section_id);

  void *iq_data_start;
  // Add compression header in DYNAMIC mode when compression is enabled
  if (fh_cfg->ru_conf.compMeth_PRACH != XRAN_COMPMETHOD_NONE && fh_cfg->ru_conf.xranCompHdrType == XRAN_COMP_HDR_TYPE_DYNAMIC) {
    struct data_section_compression_hdr *compression_header = (struct data_section_compression_hdr *)(data_section_header + 1);
    compression_header->ud_comp_hdr.ud_comp_meth = fh_cfg->ru_conf.compMeth_PRACH;
    compression_header->ud_comp_hdr.ud_iq_width = XRAN_CONVERT_IQWIDTH(fh_cfg->ru_conf.iqWidth_PRACH);
    compression_header->rsrvd = 0;
    iq_data_start = (void *)(compression_header + 1);
  } else {
    iq_data_start = (void *)(data_section_header + 1);
  }

  int16_t *dest = (int16_t *)iq_data_start;
  uint16_t *src = (uint16_t *)prachF;
  memset(iq_data_start, 0, data_len);
  AssertFatal(g_kbar >= 0 && g_kbar + (int)(prach_length * 2) <= prach_payload_iq16,
              "Invalid PRACH g_kbar %d for payload iq16 count %d and PRACH length %u\n",
              g_kbar,
              prach_payload_iq16,
              prach_length);
  bool prach_has_nonzero = false;
  for (int i = 0; i < prach_length * 2; i++) {
    if (src[i] != 0) {
      prach_has_nonzero = true;
      break;
    }
  }
  if (fh_cfg->ru_conf.compMeth_PRACH == XRAN_COMPMETHOD_NONE) {
    for (int i = 0; i < prach_length * 2; i++) {
      dest[i + g_kbar] = (int16_t)htons(src[i]);
    }

#ifdef ORU_PRACH_UPLANE_DEBUG
    int src_nonzero = 0;
    int dst_nonzero = 0;
    int16_t src_min = 0;
    int16_t src_max = 0;
    int16_t dst_min = 0;
    int16_t dst_max = 0;
    for (int i = 0; i < prach_length * 2; i++) {
      int16_t v = (int16_t)src[i];
      if (i == 0 || v < src_min)
        src_min = v;
      if (i == 0 || v > src_max)
        src_max = v;
      if (v != 0)
        src_nonzero++;
    }
    for (int i = 0; i < prach_payload_iq16; i++) {
      int16_t v = (int16_t)ntohs((uint16_t)dest[i]);
      if (i == 0 || v < dst_min)
        dst_min = v;
      if (i == 0 || v > dst_max)
        dst_max = v;
      if (v != 0)
        dst_nonzero++;
    }
    static int prach_tx_dbg_count = 0;
    if ((symbol == 0 && prach_tx_dbg_count < 1024) || prach_has_nonzero) {
      LOG_A(HW,
            "[ORU PRACH TX UNCOMP] rx_frame.slot.symbol=%d.%d.%d tx_frame=%d frame8=%d section=%d start_prb=%d num_prb=%d src_nonzero=%d/%u src_min=%d src_max=%d dst_nonzero=%d/%d dst_min=%d dst_max=%d\n",
            frame,
            slot,
            symbol,
            tx_frame,
            tx_frame8,
            prach_config->section_id,
            prach_config->start_prb,
            prach_config->num_prb,
            src_nonzero,
            prach_length * 2,
            src_min,
            src_max,
            dst_nonzero,
            prach_payload_iq16,
            dst_min,
            dst_max);
      LOG_A(HW,
            "[ORU PRACH TX UNCOMP] samples src[0]=%d src[1]=%d src[100]=%d src[277]=%d dst[0]=%d dst[g]=%d dst[g+1]=%d dst[g+100]=%d dst[g+277]=%d\n",
            (int16_t)src[0],
            (int16_t)src[1],
            (int16_t)src[100],
            (int16_t)src[277],
            (int16_t)ntohs((uint16_t)dest[0]),
            (int16_t)ntohs((uint16_t)dest[g_kbar]),
            (int16_t)ntohs((uint16_t)dest[g_kbar + 1]),
            (int16_t)ntohs((uint16_t)dest[g_kbar + 100]),
            (int16_t)ntohs((uint16_t)dest[g_kbar + 277]));
      if (symbol == 0 && !prach_has_nonzero)
        prach_tx_dbg_count++;
    }
#endif
  } else if (fh_cfg->ru_conf.compMeth_PRACH == XRAN_COMPMETHOD_BLKFLOAT) {

        /* PRACH uses 139 REs → pack into 12 PRBs */
        int nRBs = 12;
        //int payload_len = (3 * fh_cfg->ru_conf.iqWidth_PRACH + 1) * nRBs;

        /* Zero-padded local buffer for BFP input */
        int16_t local_src[12 * 12 * 2] __attribute__((aligned(64))) = {0};

        /* Copy PRACH data into RB-aligned buffer */
        for (int idx = 0; idx < 139 * 2; idx++) {
          local_src[idx + g_kbar] = src[idx];
        }

        // Log input and local_src for debugging
        static int prach_log_count = 0;
        bool has_nonzero = prach_has_nonzero;
        // Log first 5 times OR when we have non-zero data
        if (prach_log_count < 5 || has_nonzero) {
          LOG_D(HW, "[ORU PRACH COMP] frame=%d, slot=%d, symbol=%d, g_kbar=%d, has_nonzero=%d\n",
                frame, slot, symbol, g_kbar, has_nonzero);
          LOG_D(HW, "[ORU PRACH COMP] Input src: [0]=%d [1]=%d [100]=%d [138]=%d\n",
                src[0], src[1], src[100], src[138]);
          LOG_D(HW, "[ORU PRACH COMP] local_src: [0]=%d [4]=%d [100]=%d [281]=%d\n",
                local_src[0], local_src[4], local_src[100], local_src[281]);
          if (!has_nonzero && prach_log_count < 5) {
            prach_log_count++;
          }
        }

#if defined(__i386__) || defined(__x86_64__)
        struct xranlib_compress_request bfp_req = {};
        struct xranlib_compress_response bfp_rsp = {};

        bfp_req.data_in = local_src;
        bfp_req.numRBs = nRBs;
        bfp_req.len = data_len;
        bfp_req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
        bfp_req.iqWidth = fh_cfg->ru_conf.iqWidth_PRACH;

        if (prach_log_count <= 5 || has_nonzero) {
          LOG_D(HW, "[ORU PRACH COMP] Using iqWidth=%d, numRBs=%d, data_len=%zu, compHdrType=%s\n",
                fh_cfg->ru_conf.iqWidth_PRACH, nRBs, data_len,
                fh_cfg->ru_conf.xranCompHdrType == XRAN_COMP_HDR_TYPE_DYNAMIC ? "DYNAMIC" : "STATIC");
        }

        bfp_rsp.data_out = (int8_t *)dest;
        bfp_rsp.len = 0;

        oai_bfp_compression(bfp_req.iqWidth, bfp_req.numRBs, bfp_req.data_in, bfp_rsp.data_out);
        bfp_rsp.len = (3 * bfp_req.iqWidth + 1) * bfp_req.numRBs;

        // Log compressed output
        if (prach_log_count <= 5 || has_nonzero) {
          LOG_D(HW, "[ORU PRACH COMP] Compressed output: [0]=0x%02x [1]=0x%02x [27]=0x%02x [28]=0x%02x [29]=0x%02x [55]=0x%02x [56]=0x%02x, len=%d\n",
                ((uint8_t*)dest)[0], ((uint8_t*)dest)[1], ((uint8_t*)dest)[27], ((uint8_t*)dest)[28],
                ((uint8_t*)dest)[29], ((uint8_t*)dest)[55], ((uint8_t*)dest)[56], bfp_rsp.len);
        }

#elif defined(__arm__) || defined(__aarch64__)
        armral_bfp_compression(
            fh_cfg->ru_conf.iqWidth_PRACH,
            nRBs,
            local_src,
            (int8_t *)dest
        );
#else
        AssertFatal(0, "PRACH BFP compression not supported on this architecture");
#endif

      } else if (fh_cfg->ru_conf.compMeth_PRACH == XRAN_COMPMETHOD_BLKSCALE ) {

        /* PRACH uses 139 REs → pack into 12 PRBs */
        int nRBs = 12;
        //int payload_len = (3 * fh_cfg->ru_conf.iqWidth_PRACH + 1) * nRBs;

        /* Zero-padded local buffer for BFP input */
        int16_t local_src[12 * 12 * 2] __attribute__((aligned(64))) = {0};

        /* Copy PRACH data into RB-aligned buffer */
        for (int idx = 0; idx < 139 * 2; idx++) {
          local_src[idx + g_kbar] = src[idx];
        }

        // Log input and local_src for debugging
        static int prach_log_count = 0;
        bool has_nonzero = prach_has_nonzero;
        // Log first 5 times OR when we have non-zero data
        if (prach_log_count < 5 || has_nonzero) {
          LOG_D(HW, "[ORU PRACH COMP] frame=%d, slot=%d, symbol=%d, g_kbar=%d, has_nonzero=%d\n",
                frame, slot, symbol, g_kbar, has_nonzero);
          LOG_D(HW, "[ORU PRACH COMP] Input src: [0]=%d [1]=%d [100]=%d [138]=%d\n",
                src[0], src[1], src[100], src[138]);
          LOG_D(HW, "[ORU PRACH COMP] local_src: [0]=%d [4]=%d [100]=%d [281]=%d\n",
                local_src[0], local_src[4], local_src[100], local_src[281]);
          if (!has_nonzero && prach_log_count < 5) {
            prach_log_count++;
          }
        }

#if defined(__i386__) || defined(__x86_64__)
        struct xranlib_compress_request bs_req = {};
        struct xranlib_compress_response bs_rsp = {};

        bs_req.data_in = local_src;
        bs_req.numRBs = nRBs;
        bs_req.len = data_len;
        bs_req.compMethod = XRAN_COMPMETHOD_BLKSCALE;
        bs_req.iqWidth = fh_cfg->ru_conf.iqWidth_PRACH;

        bs_rsp.data_out = (int8_t *)dest;
        bs_rsp.len = 0;

        xranlib_compress_blkscale_avx512(&bs_req, &bs_rsp);
#else
        AssertFatal(0, "PRACH Block Scale compression not supported on this architecture");
#endif

      }  else if (fh_cfg->ru_conf.compMeth_PRACH == XRAN_COMPMETHOD_ULAW ) {

        /* PRACH uses 139 REs → pack into 12 PRBs */
        int nRBs = 12;
        //int payload_len = (3 * fh_cfg->ru_conf.iqWidth_PRACH + 1) * nRBs;

        /* Zero-padded local buffer for BFP input */
        int16_t local_src[12 * 12 * 2] __attribute__((aligned(64))) = {0};

        /* Copy PRACH data into RB-aligned buffer */
        for (int idx = 0; idx < 139 * 2; idx++) {
          local_src[idx + g_kbar] = src[idx];
        }

        // Log input and local_src for debugging
        static int prach_log_count = 0;
        bool has_nonzero = prach_has_nonzero;
        // Log first 5 times OR when we have non-zero data
        if (prach_log_count < 5 || has_nonzero) {
          LOG_D(HW, "[ORU PRACH COMP] frame=%d, slot=%d, symbol=%d, g_kbar=%d, has_nonzero=%d\n",
                frame, slot, symbol, g_kbar, has_nonzero);
          LOG_D(HW, "[ORU PRACH COMP] Input src: [0]=%d [1]=%d [100]=%d [138]=%d\n",
                src[0], src[1], src[100], src[138]);
          LOG_D(HW, "[ORU PRACH COMP] local_src: [0]=%d [4]=%d [100]=%d [281]=%d\n",
                local_src[0], local_src[4], local_src[100], local_src[281]);
          if (!has_nonzero && prach_log_count < 5) {
            prach_log_count++;
          }
        }

#if defined(__i386__) || defined(__x86_64__)
        struct xranlib_compress_request ulaw_req = {};
        struct xranlib_compress_response ulaw_rsp = {};

        ulaw_req.data_in = local_src;
        ulaw_req.numRBs = nRBs;
        ulaw_req.len = data_len;
        ulaw_req.compMethod = XRAN_COMPMETHOD_ULAW;
        ulaw_req.iqWidth = fh_cfg->ru_conf.iqWidth_PRACH;

        ulaw_rsp.data_out = (int8_t *)dest;
        ulaw_rsp.len = 0;

        xranlib_compress_ulaw_avx512(&ulaw_req, &ulaw_rsp);
#else
        AssertFatal(0, "PRACH ULAW compression not supported on this architecture");
#endif

      } else {
        AssertFatal(0, "Unsupported PRACH compression method %d\n",
                    fh_cfg->ru_conf.compMeth_PRACH);
      }

  buf = rte_pktmbuf_prepend(mbuf, sizeof(struct rte_ether_hdr));
  AssertFatal(buf != NULL, "incorrect mbuf size\n");

  int vf_id = xran_map_ecpriPcid_to_vf(gxran_handle, XRAN_DIR_UL, 0, aarx + fh_cfg->prach_conf.eAxC_offset);
  int ret = xran_ethdi_mbuf_send(mbuf, ETHER_TYPE_ECPRI, vf_id);
  AssertFatal(ret == 1, "Error sending mbuf\n");
#ifdef ORU_PRACH_UPLANE_DEBUG
  if (prach_has_nonzero) {
    LOG_A(HW,
          "[RAR DEBUG] ORU PRACH TX frame.slot.symbol %d.%d.%d rx_frame %d ant %d section %d start_prb %d num_prb %d vf %d bytes %zu\n",
          tx_frame,
          slot,
          symbol,
          frame,
          aarx,
          prach_config->section_id,
          prach_config->start_prb,
          prach_config->num_prb,
          vf_id,
          header_length + data_len);
  }
#endif
}

void xran_oru_send_pusch(uint32_t *puschF, int aarx, int frame, int slot, int symbol)
{
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  const struct xran_frame_config *frame_conf = &fh_cfg->frame_conf;
  if (frame_conf->nFrameDuplexType == 1) {
    int slot_in_pattern = slot % frame_conf->nTddPeriod;
    AssertFatal(frame_conf->sSlotConfig[slot_in_pattern].nSymbolType[symbol] == 1, "Attempting to send PUSCH on non-UL slot\n");
  }

  oran_pusch_cplane_config_t cfg = pusch_config[aarx][slot][symbol];
  int section_id = cfg.section_id;

  if (section_id == -1) {
#ifdef ORU_PUSCH_CPLANE_DEBUG
    static int pusch_no_cp_dbg_count = 0;
    if (pusch_no_cp_dbg_count < 256) {
      LOG_D(HW, "[ORU PUSCH NO CP] frame=%d slot=%d symbol=%d aarx=%d cached_section=%d cached_frame=%d cached_start_prb=%d cached_num_prb=%d\n",
            frame, slot, symbol, aarx, cfg.section_id, cfg.frame, cfg.start_prb, cfg.num_prb);
      pusch_no_cp_dbg_count++;
    }
#endif
    return;
  }

  if (cfg.frame >= 0 && cfg.frame != (frame & 0xff)) {
#ifdef ORU_PUSCH_CPLANE_DEBUG
    static int pusch_frame_mismatch_dbg_count = 0;
    if (pusch_frame_mismatch_dbg_count < 256) {
      LOG_D(HW, "[ORU PUSCH CP FRAME MISMATCH] tx_frame=%d cp_frame=%d slot=%d symbol=%d aarx=%d section=%d start_prb=%d num_prb=%d cp_start_sym=%d cp_num_sym=%d\n",
            frame, cfg.frame, slot, symbol, aarx, section_id, cfg.start_prb, cfg.num_prb, cfg.start_symbol, cfg.num_symbols);
      pusch_frame_mismatch_dbg_count++;
    }
#endif
  }

  uint8_t mu = fh_cfg->frame_conf.nNumerology;
  const int num_ul_rbs = fh_cfg->nULRBs;
  int num_prb = cfg.num_prb;
  int start_prb = cfg.start_prb;
  // AssertFatal(num_prb == num_ul_rbs && start_prb == 0, "only support full bandwidth reception\n");

  // AssertFatal(fh_cfg->ru_conf.compMeth == XRAN_COMPMETHOD_NONE, "Compression not supported\n");
  // TODO: With compression, have to add compression header to header_len
  size_t header_length = sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr) + sizeof(struct data_section_hdr);

  // TODO: For compression, have to re-evaluate data size;
  const uint num_sc = num_ul_rbs * NR_NB_SC_PER_RB;
  size_t data_len = sizeof(int32_t) * num_sc;
  if (fh_cfg->ru_conf.compMeth != XRAN_COMPMETHOD_NONE)
    data_len = (3 * fh_cfg->ru_conf.iqWidth + 1) * num_prb;

  struct rte_mbuf *mbuf = xran_ethdi_mbuf_alloc();
  AssertFatal(mbuf != NULL, "out of mbufs\n");
  char *buf = rte_pktmbuf_append(mbuf, header_length + data_len);
  AssertFatal(buf, "incorrect mbuf size\n");

  struct xran_ecpri_hdr *ecpri_header = (struct xran_ecpri_hdr *)rte_pktmbuf_mtod(mbuf, char *);
  uint16_t ecpri_payload_size = xran_get_ecpri_hdr_size() + sizeof(struct radio_app_common_hdr) + sizeof(struct data_section_hdr) + data_len;
  fill_ecpri_header(ecpri_header, ECPRI_IQ_DATA, ecpri_payload_size, 0, aarx, pusch_seq_id[aarx]++, 0);

  struct radio_app_common_hdr *radio_app_header = (struct radio_app_common_hdr *)(ecpri_header + 1);
  fill_radio_app_header(radio_app_header, 0, XRAN_DIR_UL, frame, slot, symbol, mu);

  struct data_section_hdr *data_section_header = (struct data_section_hdr *)(radio_app_header + 1);
  if (fh_cfg->ru_conf.compMeth == XRAN_COMPMETHOD_NONE)
    fill_data_section_header(data_section_header, fh_cfg->nULRBs, 0, section_id);
  else
    fill_data_section_header(data_section_header, num_prb, 0, section_id);


  // TODO: O-DU expect compression header here even though the standard says it's not required
  struct data_section_compression_hdr *compression_header = (struct data_section_compression_hdr *)(data_section_header + 1);
  compression_header->ud_comp_hdr.ud_comp_meth = fh_cfg->ru_conf.compMeth;// XRAN_COMPMETHOD_NONE;
  compression_header->ud_comp_hdr.ud_iq_width = XRAN_CONVERT_IQWIDTH(fh_cfg->ru_conf.iqWidth);
  compression_header->rsrvd = 0;

  int fftsize = 1 << fh_cfg->nULFftSize;
  int first_carrier_offset = fftsize - (fh_cfg->nULRBs * NR_NB_SC_PER_RB / 2);
  int num_sc_first_copy = (fftsize - first_carrier_offset);
  int num_sc_second_copy = fh_cfg->nULRBs * NR_NB_SC_PER_RB - num_sc_first_copy;
  void *iq_data_start = (void *)(compression_header + 1);
  int16_t* dest = (int16_t*)iq_data_start;
  uint16_t* src = (uint16_t*)&puschF[first_carrier_offset];
  if (fh_cfg->ru_conf.compMeth != XRAN_COMPMETHOD_NONE) {                                                                                                      
    // Calculate split based on start position
    int neg_len = 0;                                                                                                                                           
    int pos_len = 0;                                                                                                                                           
                                              
    if (start_prb < (num_ul_rbs >> 1))                                                                                                                         
      neg_len = min((num_ul_rbs * NR_NB_SC_PER_RB / 2) - (start_prb * NR_NB_SC_PER_RB),                                                                        
                    num_prb * NR_NB_SC_PER_RB);                                                                                                                
                                                                                                                                                               
    pos_len = (num_prb * NR_NB_SC_PER_RB) - neg_len;                                                                                                           
                                                                                                                                                               
    // Set up source pointers                                                                                                                                  
    uint16_t *src1 = (uint16_t *)&puschF[(neg_len == 0)                                                                                                        
                     ? ((start_prb * NR_NB_SC_PER_RB) - (num_ul_rbs * NR_NB_SC_PER_RB / 2))                                                                    
                     : 0];                                                                                                                                     
                                                                  
    uint16_t *src2 = (uint16_t *)&puschF[(start_prb * NR_NB_SC_PER_RB) +                                                                                       
                                          fftsize - (num_ul_rbs * NR_NB_SC_PER_RB / 2)];                                                                       
                                                                                                                                                               
    // Reorder into local buffer                                                                                                                               
    uint32_t local_src[num_prb * NR_NB_SC_PER_RB] __attribute__((aligned(64)));                                                                                
    memcpy(local_src, src2, neg_len * 4);                                                                                                                      
    memcpy(&local_src[neg_len], src1, pos_len * 4);                                                                                                            
                                                                                                                                                               
    // Update src to point to reordered data                                                                                                                   
    src = (uint16_t *)local_src;                                                                                                                               
  }            


#ifdef ORU_PUSCH_UPLANE_DEBUG
   int pusch_src_nonzero = 0;
   int16_t pusch_src_min = INT16_MAX;
   int16_t pusch_src_max = INT16_MIN;
#endif

   /* ---------- NO COMPRESSION ---------- */
  if (fh_cfg->ru_conf.compMeth == XRAN_COMPMETHOD_NONE) {
  for (int i = 0; i < num_sc_first_copy * 2; i++) {
#ifdef ORU_PUSCH_UPLANE_DEBUG
    int16_t sample = (int16_t)src[i];
    if (sample != 0)
      pusch_src_nonzero++;
    if (sample < pusch_src_min)
      pusch_src_min = sample;
    if (sample > pusch_src_max)
      pusch_src_max = sample;
#endif
    *dest++ = (int16_t)htons(src[i]);
  }
  src = (uint16_t*)puschF;
  for (int i = 0; i < num_sc_second_copy * 2; i++) {
#ifdef ORU_PUSCH_UPLANE_DEBUG
    int16_t sample = (int16_t)src[i];
    if (sample != 0)
      pusch_src_nonzero++;
    if (sample < pusch_src_min)
      pusch_src_min = sample;
    if (sample > pusch_src_max)
      pusch_src_max = sample;
#endif
    *dest++ = (int16_t)htons(src[i]);
  }
}   else if (fh_cfg->ru_conf.compMeth == XRAN_COMPMETHOD_BLKFLOAT) {

      data_len =
          (3 * fh_cfg->ru_conf.iqWidth + 1) * num_prb;

#if defined(__i386__) || defined(__x86_64__)

      struct xranlib_compress_request  req = {};
      struct xranlib_compress_response rsp = {};

      req.data_in    = (int16_t *)src;
      req.numRBs     = num_prb;
      req.len        = data_len;
      req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
      req.iqWidth    = fh_cfg->ru_conf.iqWidth;

      rsp.data_out = (int8_t *)dest;
      oai_bfp_compression(req.iqWidth, req.numRBs, req.data_in, rsp.data_out);
      rsp.len = (3 * req.iqWidth + 1) * req.numRBs;

#elif defined(__arm__) || defined(__aarch64__)

      armral_bfp_compression(
          fh_cfg->ru_conf.iqWidth,
          num_prb,
          (int16_t *)src,
          (int8_t *)dest);

#else
      AssertFatal(0, "BFP compression not supported on this architecture");
#endif

    }

     else if (fh_cfg->ru_conf.compMeth == XRAN_COMPMETHOD_BLKSCALE) {

      data_len =
          (3 * fh_cfg->ru_conf.iqWidth + 1) * num_prb;

#if defined(__i386__) || defined(__x86_64__)

      struct xranlib_compress_request  req = {};
      struct xranlib_compress_response rsp = {};

      req.data_in    = (int16_t *)src;
      req.numRBs     = num_prb;
      req.len        = data_len;
      req.compMethod = XRAN_COMPMETHOD_BLKSCALE;
      req.iqWidth    = fh_cfg->ru_conf.iqWidth;

      rsp.data_out = (int8_t *)dest;
      xranlib_compress_blkscale_avx512(&req, &rsp);

#else
      AssertFatal(0, "BLKSCALE compression not supported on this architecture");
#endif

    }


    else if (fh_cfg->ru_conf.compMeth == XRAN_COMPMETHOD_ULAW) {

      data_len =
          (3 * fh_cfg->ru_conf.iqWidth + 1) * num_prb;

#if defined(__i386__) || defined(__x86_64__)

      struct xranlib_compress_request  req = {};
      struct xranlib_compress_response rsp = {};

      req.data_in    = (int16_t *)src;
      req.numRBs     = num_prb;
      req.len        = data_len;
      req.compMethod = XRAN_COMPMETHOD_ULAW;
      req.iqWidth    = fh_cfg->ru_conf.iqWidth;

      rsp.data_out = (int8_t *)dest;
      xranlib_compress_ulaw_avx512(&req, &rsp);

#else
      AssertFatal(0, "ULAW compression not supported on this architecture");
#endif

    }
        else {
      AssertFatal(0, "Unsupported PUSCH compression method %d\n",
                  fh_cfg->ru_conf.compMeth);
    }



#ifdef ORU_PUSCH_UPLANE_DEBUG
  if (fh_cfg->ru_conf.compMeth == XRAN_COMPMETHOD_NONE) {
    static int pusch_tx_dbg_count = 0;
    if (pusch_tx_dbg_count < 4096) {
      LOG_A(HW,
            "[ORU PUSCH TX] frame=%d slot=%d symbol=%d aarx=%d section=%d cp_frame=%d cp_start_sym=%d cp_num_sym=%d start_prb=%d num_prb=%d hdr_start_prb=%d hdr_num_prb=%d src_nonzero=%d/%u src_min=%d src_max=%d comp=%d\n",
            frame,
            slot,
            symbol,
            aarx,
            section_id,
            cfg.frame,
            cfg.start_symbol,
            cfg.num_symbols,
            start_prb,
            num_prb,
            0,
            fh_cfg->nULRBs,
            pusch_src_nonzero,
            num_sc * 2,
            pusch_src_nonzero ? pusch_src_min : 0,
            pusch_src_nonzero ? pusch_src_max : 0,
            fh_cfg->ru_conf.compMeth);
      pusch_tx_dbg_count++;
    }
  }
#endif

  buf = rte_pktmbuf_prepend(mbuf, sizeof(struct rte_ether_hdr));
  AssertFatal(buf != NULL, "incorrect mbuf size\n");

  int vf_id = xran_map_ecpriPcid_to_vf(gxran_handle, XRAN_DIR_UL, 0, aarx);
  int ret = xran_ethdi_mbuf_send(mbuf, ETHER_TYPE_ECPRI, vf_id);
  AssertFatal(ret == 1, "Error sending mbuf\n");
}
