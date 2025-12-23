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

void symbol_callback(void *args, struct xran_sense_of_time *p_sense_of_time)
{
  if (!first_call_set) {
    return;
  }

  oran_symbol_callback_args_t *callback_args = args;
  if ((callback_args->cb_symbol_mask & (1 << p_sense_of_time->nSymIdx)) == 0) {
    return;
  }

  int num_symbols = callback_args->num_symbols[p_sense_of_time->nSymIdx];
  int start_symbol = callback_args->start_symbol[p_sense_of_time->nSymIdx];

  // Adjust timing by symbol_diff
  int slot_index_increments = (p_sense_of_time->nSymIdx + callback_args->symbol_diff) / NR_SYMBOLS_PER_SLOT;
  int num_slots_per_subframe = 1 << callback_args->numerology;

  int target_slot_in_frame =
      p_sense_of_time->nSlotIdx + p_sense_of_time->nSubframeIdx * num_slots_per_subframe + slot_index_increments;
  int frame = p_sense_of_time->nFrameIdx;
  int num_slots_per_frame = 10 << callback_args->numerology;
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
        "Callback triggered at frame.slot.synbol %d.%d.%d targets %d.%d.%d, num_symbols %d, symbol_diff %d\n",
        p_sense_of_time->nFrameIdx,
        p_sense_of_time->nSlotIdx + p_sense_of_time->nSubframeIdx * num_slots_per_subframe,
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
  return 0;
}

int process_ru_uplane(struct rte_mbuf *pkt,
                      void *handle,
                      struct xran_eaxc_info *p_cid,
                      uint16_t port_id,
                      struct xran_sense_of_time *p_sense_of_time)
{
  static uint64_t num_packets = 0;
  num_packets++;
  if (num_packets % 10000 == 0) {
    LOG_I(HW, "received uplane packets %lu\n", num_packets);
  }

  const struct xran_fh_config *fh_cfg = get_xran_fh_config(port_id);
  void *iq_data_start = NULL;
  uint8_t CC_ID;
  uint8_t Ant_ID;
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
  xran_extract_iq_samples(pkt,
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
  (void)iq_data_start;
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

  return MBUF_FREE;
}

int32_t process_ru_cplane(struct rte_mbuf *pkt, void *handle, uint16_t port_id, struct xran_sense_of_time *p_sense_of_time)
{
  static uint64_t num_packets = 0;
  num_packets++;
  if (num_packets % 10000 == 0) {
    LOG_I(HW, "received cplane packets %lu\n", num_packets);
  }
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
