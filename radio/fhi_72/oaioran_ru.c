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
  int start_symbol;
  int num_symbols;
  int symbol_diff;
  int numerology;
} oran_symbol_callback_args_t;

int32_t symbol_callback(void *args, struct xran_sense_of_time *p_sense_of_time)
{
  if (!first_call_set) {
    return 0;
  }
  uint32_t frame = p_sense_of_time->nFrameIdx;
  oran_symbol_callback_args_t *callback_args = args;
  uint32_t slot = p_sense_of_time->nSlotIdx + p_sense_of_time->nSubframeIdx * (1 << callback_args->numerology);
  uint32_t subframe = p_sense_of_time->nSubframeIdx;

  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  int mu = fh_cfg->frame_conf.nNumerology;
  AssertFatal(mu == 1, "Only numerology 1 supported for RU\n");
  const int slots_in_sf = 1 << mu;
  uint32_t slot_in_frame = slot + subframe * slots_in_sf;

  LOG_D(HW,
        "Push %d.%d (slot %d, subframe %d, symbol_diff %d)\n",
        frame,
        slot_in_frame,
        slot,
        subframe,
        callback_args->symbol_diff);
  notifiedFIFO_elt_t *req = newNotifiedFIFO_elt(sizeof(ru_dl_sync_info_t), 0, NULL, NULL);
  ru_dl_sync_info_t *info = NotifiedFifoData(req);
  info->frame = frame;
  info->slot = slot;
  info->symbol = callback_args->start_symbol;
  info->num_symbols = callback_args->num_symbols;

  int slot_duration_uS[] = {1000, 500, 250, 125};
  uint64_t slot_in_second_offset_nS = ((uint64_t)p_sense_of_time->tti_counter * slot_duration_uS[mu]) * 1000UL;

  float symbol_duration_nS = ((float)slot_duration_uS[mu] * 1000) / 14.0f;
  uint64_t symbol_in_slot_offset_nS = (uint64_t)(callback_args->start_symbol * symbol_duration_nS);

  info->ts.tv_sec = p_sense_of_time->nSecond;
  info->ts.tv_nsec = slot_in_second_offset_nS + symbol_in_slot_offset_nS;

  AssertFatal(info->ts.tv_nsec < 1000000000UL, "ORAN: Invalid tv_nsec %ld\n", info->ts.tv_nsec);

  pushNotifiedFIFO(&ru_dl_sync_fifo, req);
  return 0;
}

int xran_oru_tx_read_slot(uint32_t **txdataF, int nb_tx, int *frame, int *slot, int *symbol, int* num_symbols, struct timespec *ts)
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

void init_oru_packet_processor(void* handle, int callbacks_per_slot)
{
  AssertFatal(callbacks_per_slot <= NR_SYMBOLS_PER_SLOT, "Can do at most %d callbacks per slot", NR_SYMBOLS_PER_SLOT);
  static bool installed = false;
  AssertFatal(!installed, "Cannot init oru twice\n");
  installed = true;

  static struct xran_sense_of_time sense_of_time[NR_SYMBOLS_PER_SLOT];
  static oran_symbol_callback_args_t args[NR_SYMBOLS_PER_SLOT];

  int symbols_per_callback = NR_SYMBOLS_PER_SLOT / callbacks_per_slot;

  int start_symbol = 0;
  for (int i = 0; i < callbacks_per_slot; i++) {
    args[i].start_symbol = start_symbol;
    args[i].num_symbols = symbols_per_callback;
    start_symbol += symbols_per_callback;
  }
  // Extend last callback to include leftover symbols
  args[callbacks_per_slot - 1].num_symbols += (NR_SYMBOLS_PER_SLOT - start_symbol);

  // TODO: handle RX window
  for (int i = 0; i < callbacks_per_slot; i++) {
    xran_reg_sym_cb(handle,
                    symbol_callback,
                    &args[i],
                    &sense_of_time[i],
                    args[i].start_symbol,
                    XRAN_CB_SYM_OTA_TIME);
    LOG_I(HW, "Installed callback for symbol %d\n", args[i].start_symbol + args[i].num_symbols);
  }
}
