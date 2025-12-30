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
  context->uplane_data_ring = rte_ring_create("uplane_data_ring", UPLANE_DATA_RING_SIZE, rte_socket_id(), RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);
  AssertFatal(context->uplane_data_ring != NULL, "Failed to create ring uplane_data_ring\n");
  for (int i = 0; i < NUM_UPLANE_DATA_ELEMENTS; i++) {
    int ret = rte_ring_enqueue(context->uplane_data_ring, &context->uplane_data_pool[i]);
    AssertFatal(ret == 0, "Failed to push elements to uplane_data_ring\n");
  }
  LOG_I(HW, "Enqueued %d elements to uplane_data_ring\n", NUM_UPLANE_DATA_ELEMENTS);
  int num_slots_per_frame = 10 << mu;
  int num_symbol_rings = NR_SYMBOLS_PER_SLOT * num_slots_per_frame;
  context->uplane_symbol_rings = malloc(sizeof(struct rte_ring *) * num_symbol_rings);
  for (int i = 0; i < num_symbol_rings; i++) {
    char name[128];
    snprintf(name, sizeof(name), "up_symbol_%d", i);
    struct rte_ring *ring = rte_ring_create(name, UPLANE_SYMBOL_RING_SIZE, rte_socket_id(), RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);
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

void symbol_callback(void *args, struct xran_sense_of_time *p_sense_of_time)
{
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

  pushNotifiedFIFO(&ru_dl_sync_fifo, req);
}

int xran_oru_tx_read_slot(uint32_t **txdataF, int nb_tx, int *frame, int *slot, int *symbol, int *num_symbols, struct timespec *ts)
{
  notifiedFIFO_elt_t *res = pullNotifiedFIFO(&ru_dl_sync_fifo);
  ru_dl_sync_info_t *info = NotifiedFifoData(res);

  *slot = info->slot;
  *frame = info->frame;
  *symbol = info->symbol;
  *num_symbols = info->num_symbols;
  *ts = info->ts;
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
  for (int sym = *symbol; sym < *symbol + *num_symbols; sym++) {
    uplane_data_t *uplane_data[MAX_UPLANE_PACKETS_PER_SYMBOL];
    int num_packets = pull_uplane_packet_data(&packet_processor_context, uplane_data, MAX_UPLANE_PACKETS_PER_SYMBOL, *slot, sym);
    if (num_packets) {
      memset(tx_data_sym, 0, sizeof(tx_data_sym));
      for (int i = 0; i < num_packets; i++) {
        int start_prb = uplane_data[i]->start_prb;
        int num_prb = uplane_data[i]->num_prb;
        int comp_meth = uplane_data[i]->comp_meth;
        int aatx = uplane_data[i]->aatx;
        void *iq_data = uplane_data[i]->iq_data;
        AssertFatal(comp_meth == XRAN_COMPMETHOD_NONE, "Compression not supported\n");
        uint16_t *source = (uint16_t *)iq_data;
        int16_t *destination = (int16_t *)&tx_data_sym[aatx][start_prb * NR_NB_SC_PER_RB];
        for (int j = 0; j < num_prb * NR_NB_SC_PER_RB * 2; j++) {
          destination[j] = (int16_t)ntohs(source[j]);
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
  if (!first_call_set) {
    return MBUF_FREE;
  }
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(port_id);
  void *iq_data_start = NULL;
  uint8_t CC_ID = 0xFF;
  uint8_t Ant_ID = 0xFF;
  uint8_t frame_id;
  uint8_t subframe_id;
  uint8_t slot_id;
  uint8_t symb_id;
  uint8_t filter_id;
  union ecpri_seq_id seq_id;
  uint16_t num_prbu;
  uint16_t start_prbu;
  uint16_t sym_inc;
  uint16_t rb;
  uint16_t sect_id;
  int expect_comp = fh_cfg->ru_conf.compMeth != XRAN_COMPMETHOD_NONE;
  enum xran_comp_hdr_type staticComp = fh_cfg->ru_conf.xranCompHdrType;
  uint8_t compMeth = XRAN_COMPMETHOD_NONE;
  uint8_t iqWidth = 0;
  uint8_t is_prach;
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

  AssertFatal(compMeth == XRAN_COMPMETHOD_NONE, "Compression not supported\n");
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


  int slot_in_frame = slot_id + subframe_id * slots_per_subframe;
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
  return MBUF_FREE;
}

void init_oru_packet_processor(void *handle, int callbacks_per_slot)
{
  AssertFatal(callbacks_per_slot <= NR_SYMBOLS_PER_SLOT,
              "Can do at most %d callbacks per slot",
              NR_SYMBOLS_PER_SLOT);
  static bool installed = false;
  AssertFatal(!installed, "Cannot init oru twice\n");
  installed = true;


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
