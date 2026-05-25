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
#include "PHY/defs_RU.h"
#include "assertions.h"
#include "common/config/config_userapi.h"
#include "common/utils/nr/nr_common.h"
#include "nr-oru.h"
#include "openair1/PHY/defs_nr_common.h"
#include "openair1/PHY/MODULATION/nr_modulation.h"
#include "openair1/SCHED_NR/sched_nr.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "thread-pool.h"

#define CONFIG_SECTION_ORU "ORUs.[0]"

// clang-format off
#define CONFIG_STRING_ORU_TX_BW_LIST               "tx_bw"
#define CONFIG_STRING_ORU_RX_BW_LIST               "rx_bw"
#define CONFIG_STRING_ORU_CARRIER_TX_LIST          "carrier_tx"
#define CONFIG_STRING_ORU_CARRIER_RX_LIST          "carrier_rx"
#define CONFIG_STRING_ORU_FRAME_TYPE               "frame_type"
#define CONFIG_STRING_ORU_PRACH_CONFIGID           "prach_config_index"
#define CONFIG_STRING_ORU_PRACH_MSG1FREQ           "prach_msg1_start"
#define CONFIG_STRING_ORU_NUMEROLOGY               "mu"
#define CONFIG_STRING_ORU_TDD_PERIOD               "tdd_period"
#define CONFIG_STRING_ORU_NUM_DL_SLOTS             "num_dl_slots"
#define CONFIG_STRING_ORU_NUM_UL_SLOTS             "num_ul_slots"
#define CONFIG_STRING_ORU_NUM_DL_SYMBOLS           "num_dl_symbols"
#define CONFIG_STRING_ORU_NUM_UL_SYMBOLS           "num_ul_symbols"
#define CONFIG_STRING_ORU_TP_CORES                  "tp_cores"

#define HLP_ORU_TX_BW "set the TX bandwidth list per component carrier"
#define HLP_ORU_RX_BW "set the RX bandwidth list per component carrier"
#define HLP_ORU_CARRIER_TX "set the TX carrier frequencies per component carrier"
#define HLP_ORU_CARRIER_RX "set the RX carrier frequencies per component carrier"
#define HLP_ORU_FRAMETYPE "set the Frame type TDD/FDD of all component carriers"
#define HLP_ORU_PRACH_CONFIGID "set the PRACH configuration id of all component carriers"
#define HLP_ORU_PRACH_MSG1FREQ "set the PRACH MSG1 frequency of all component carriers"
#define HLP_ORU_NUMEROLOGY     "set the numerology of the RU"
#define HLP_ORU_TDD_PERIOD     "set the 3GPP TDD periodificty 0-9"
#define HLP_ORU_NUM_DL_SLOTS   "set the number of DL Slots in TDD"
#define HLP_ORU_NUM_UL_SLOTS   "set the number of UL Slots in TDD"
#define HLP_ORU_NUM_DL_SYMBOLS "set the number of DL symbols in the mixed slot"
#define HLP_ORU_NUM_UL_SYMBOLS "set the number of UL symbols in the mixed slot"
#define HLP_ORU_TP_CORES       "CPU cores used for threadpool"

#define CMDLINE_PARAMS_DESC_ORU \
{ \
  {CONFIG_STRING_ORU_TX_BW_LIST,                HLP_ORU_TX_BW,                      0,    .iptr=NULL,       .defintarrayval=DEFBW,        TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_RX_BW_LIST,                HLP_ORU_RX_BW,                      0,    .iptr=NULL,       .defintarrayval=DEFBW,        TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_CARRIER_TX_LIST,           HLP_ORU_CARRIER_TX,                 0,    .iptr=NULL,       .defintarrayval=DEFCARRIER,   TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_CARRIER_RX_LIST,           HLP_ORU_CARRIER_RX,                 0,    .iptr=NULL,       .defintarrayval=DEFCARRIER,   TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_FRAME_TYPE,                HLP_ORU_FRAMETYPE,                  0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_PRACH_CONFIGID,            HLP_ORU_PRACH_CONFIGID,             0,    .uptr=NULL,       .defintval=152,               TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_PRACH_MSG1FREQ,            HLP_ORU_PRACH_MSG1FREQ,             0,    .uptr=NULL,       .defintval=0,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUMEROLOGY,                HLP_ORU_NUMEROLOGY,                 0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_TDD_PERIOD,                HLP_ORU_TDD_PERIOD,                 0,    .uptr=NULL,       .defintval=5,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_DL_SLOTS,              HLP_ORU_NUM_DL_SLOTS,               0,    .uptr=NULL,       .defintval=3,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_UL_SLOTS,              HLP_ORU_NUM_UL_SLOTS,               0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_DL_SYMBOLS,            HLP_ORU_NUM_DL_SYMBOLS,             0,    .uptr=NULL,       .defintval=7,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_UL_SYMBOLS,            HLP_ORU_NUM_UL_SYMBOLS,             0,    .uptr=NULL,       .defintval=3,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_TP_CORES,                  HLP_ORU_TP_CORES,                   0,    .iptr=NULL,       .defintarrayval=DEFTPCORES,   TYPE_INTARRAY,     4}, \
}
// clang-format on

extern void set_scs_parameters(NR_DL_FRAME_PARMS *fp, int mu, int N_RB_DL, int ssb_case);
typedef struct {
  openair0_timestamp_t sample;
  int slot;
  int frame;
  int symbol;
} initial_sync_t;

typedef struct {
  int frame_unwrap;
  int last_frame;
  int64_t sync_offset;
} sync_params_t;

typedef struct {
  RU_t *ru;
  NR_DL_FRAME_PARMS *fp;
  int slot;
  int start_symbol;
  int num_symbols;
  int aatx;
  c16_t *txdataF;
  task_ans_t *task_ans;
} dl_symbol_process_t;

typedef struct {
  ORU_t *oru;
  int frame;
  int slot;
  int symbol;
  int aarx;
} pusch_symbol_job_t;

extern void tx_rf_symbols(RU_t *ru, int frame, int slot, uint64_t timestamp, int start_symbol, int num_symbols);
static void receive_pusch(void *args);

extern void rx_nr_prach_ru_internal(prach_item_t *p,
                                    int beam_id,
                                    int prachStartSymbol,
                                    int prachOccasion,
                                    int32_t **rxdata,
                                    NR_DL_FRAME_PARMS *fp,
                                    int N_TA_offset,
                                    int rep_index,
                                    uint reps);

#ifdef ORU_PRACH_RAW_DEBUG
static void debug_oru_prach_raw_window(ORU_t *oru, int frame, int slot, int prach_symbol)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  prach_item_t *p = &oru->prach_item;
  const int prachStartSymbol = oru->prach_info.start_symbol;
  const int sum = fp->ofdm_symbol_size + fp->nb_prefix_samples;
  const int sum0 = fp->ofdm_symbol_size + fp->nb_prefix_samples0;
  int sample_offset_slot;
  if (prachStartSymbol == 0) {
    sample_offset_slot = 0;
  } else if (fp->slots_per_subframe == 1) {
    if (prachStartSymbol <= 7)
      sample_offset_slot = sum * (prachStartSymbol - 1) + sum0;
    else
      sample_offset_slot = sum * (prachStartSymbol - 2) + sum0 * 2;
  } else {
    if (!(slot % (fp->slots_per_subframe / 2)))
      sample_offset_slot = sum * (prachStartSymbol - 1) + sum0;
    else
      sample_offset_slot = sum * prachStartSymbol;
  }

  int Ncp = 0;
  int dftlen = 0;
  const int mu = p->numerology_index;
  if (p->prach_sequence_length == 0) {
    switch (p->pdu.prach_format) {
      case 0: Ncp = 3168; dftlen = 24576; break;
      case 1: Ncp = 21024; dftlen = 24576; break;
      case 2: Ncp = 4688; dftlen = 24576; break;
      case 3: Ncp = 3168; dftlen = 6144; break;
      default: return;
    }
  } else {
    switch (p->pdu.prach_format) {
      case 4: Ncp = 288 >> mu; break;
      case 5: Ncp = 576 >> mu; break;
      case 6: Ncp = 864 >> mu; break;
      case 7: Ncp = 216 >> mu; break;
      case 8: Ncp = 936 >> mu; break;
      case 9: Ncp = 1240 >> mu; break;
      case 10: Ncp = 2048 >> mu; break;
      default: return;
    }
    dftlen = 2048 >> mu;
  }

  if (p->numerology_index == 0) {
    if (prachStartSymbol == 0 || prachStartSymbol == 7)
      Ncp += 16;
  } else {
    if (slot % (fp->slots_per_subframe / 2) == 0 && prachStartSymbol == 0)
      Ncp += 16;
  }

  switch (fp->samples_per_subframe) {
    case 7680: Ncp >>= 2; dftlen >>= 2; break;
    case 15360: Ncp >>= 1; dftlen >>= 1; break;
    case 23040: Ncp = (Ncp * 3) / 4; dftlen = (dftlen * 3) / 4; break;
    case 30720: break;
    case 46080: Ncp = (Ncp * 3) / 2; dftlen = (dftlen * 3) / 2; break;
    case 61440: Ncp <<= 1; dftlen <<= 1; break;
    case 92160: Ncp *= 3; dftlen *= 3; break;
    case 122880: Ncp <<= 2; dftlen <<= 2; break;
    case 184320: Ncp *= 6; dftlen *= 6; break;
    case 245760: Ncp <<= 3; dftlen <<= 3; break;
    default: return;
  }

  const int frame_samples = fp->samples_per_frame;
  const int slot_start = get_samples_slot_timestamp(fp, slot);
  const int slot_len = get_samples_slot_duration(fp, slot, 1);
  const int base = slot_start + sample_offset_slot - ru->N_TA_offset;
  const int fft_start = base + Ncp + prach_symbol * dftlen;
  const int raw_span = Ncp + (prach_symbol + 1) * dftlen;

  for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
    c16_t *rx = (c16_t *)ru->common.rxdata[aarx];
    int slot_nonzero = 0;
    int slot_first_nonzero = -1;
    int slot_last_nonzero = -1;
    int slot_peak_index = -1;
    uint64_t slot_energy = 0;
    int slot_peak = 0;
    for (int i = 0; i < slot_len; i++) {
      int idx = (slot_start + i) % frame_samples;
      int re = rx[idx].r;
      int im = rx[idx].i;
      int mag = abs(re) + abs(im);
      if (mag != 0) {
        if (slot_first_nonzero < 0)
          slot_first_nonzero = i;
        slot_last_nonzero = i;
        slot_nonzero++;
      }
      if (mag > slot_peak) {
        slot_peak = mag;
        slot_peak_index = i;
      }
      slot_energy += (uint64_t)re * re + (uint64_t)im * im;
    }

    int raw_nonzero = 0;
    int raw_first_nonzero = -1;
    int raw_last_nonzero = -1;
    int raw_peak_index = -1;
    uint64_t raw_energy = 0;
    int raw_peak = 0;
    for (int i = 0; i < raw_span; i++) {
      int idx = (base + i + frame_samples) % frame_samples;
      int re = rx[idx].r;
      int im = rx[idx].i;
      int mag = abs(re) + abs(im);
      if (mag != 0) {
        if (raw_first_nonzero < 0)
          raw_first_nonzero = i;
        raw_last_nonzero = i;
        raw_nonzero++;
      }
      if (mag > raw_peak) {
        raw_peak = mag;
        raw_peak_index = i;
      }
      raw_energy += (uint64_t)re * re + (uint64_t)im * im;
    }

    int fft_nonzero = 0;
    int fft_first_nonzero = -1;
    int fft_last_nonzero = -1;
    int fft_peak_index = -1;
    uint64_t fft_energy = 0;
    int fft_peak = 0;
    for (int i = 0; i < dftlen; i++) {
      int idx = (fft_start + i + frame_samples) % frame_samples;
      int re = rx[idx].r;
      int im = rx[idx].i;
      int mag = abs(re) + abs(im);
      if (mag != 0) {
        if (fft_first_nonzero < 0)
          fft_first_nonzero = i;
        fft_last_nonzero = i;
        fft_nonzero++;
      }
      if (mag > fft_peak) {
        fft_peak = mag;
        fft_peak_index = i;
      }
      fft_energy += (uint64_t)re * re + (uint64_t)im * im;
    }

    const bool raw_dbg_frame = (frame >= 500 && frame <= 850 && slot == 19 && prach_symbol == 0);
    if (raw_dbg_frame || raw_nonzero || fft_nonzero) {
      LOG_I(HW,
            "[ORU PRACH RAW] frame.slot.prach_symbol=%d.%d.%d ant=%d start_symbol=%d N_TA=%d sample_offset=%d Ncp=%d dftlen=%d slot_start=%d slot_len=%d base=%d fft_start=%d raw_span=%d slot_nz=%d raw_nz=%d fft_nz=%d slot_first=%d slot_last=%d slot_peak_idx=%d raw_first=%d raw_last=%d raw_peak_idx=%d fft_first=%d fft_last=%d fft_peak_idx=%d slot_peak=%d raw_peak=%d fft_peak=%d slot_energy=%lu raw_energy=%lu fft_energy=%lu\n",
            frame,
            slot,
            prach_symbol,
            aarx,
            prachStartSymbol,
            ru->N_TA_offset,
            sample_offset_slot,
            Ncp,
            dftlen,
            slot_start,
            slot_len,
            base,
            fft_start,
            raw_span,
            slot_nonzero,
            raw_nonzero,
            fft_nonzero,
            slot_first_nonzero,
            slot_last_nonzero,
            slot_peak_index,
            raw_first_nonzero,
            raw_last_nonzero,
            raw_peak_index,
            fft_first_nonzero,
            fft_last_nonzero,
            fft_peak_index,
            slot_peak,
            raw_peak,
            fft_peak,
            slot_energy,
            raw_energy,
            fft_energy);
      LOG_I(HW,
            "[ORU PRACH RAW] samples base=(%d,%d) fft=(%d,%d) fft+100=(%d,%d) fft+511=(%d,%d)\n",
            rx[(base + frame_samples) % frame_samples].r,
            rx[(base + frame_samples) % frame_samples].i,
            rx[(fft_start + frame_samples) % frame_samples].r,
            rx[(fft_start + frame_samples) % frame_samples].i,
            rx[(fft_start + 100 + frame_samples) % frame_samples].r,
            rx[(fft_start + 100 + frame_samples) % frame_samples].i,
            rx[(fft_start + dftlen - 1 + frame_samples) % frame_samples].r,
            rx[(fft_start + dftlen - 1 + frame_samples) % frame_samples].i);
    }
  }
}

#endif


static void oru_downlink_processing(ORU_t *oru,
                                    c16_t *txDataF_ptr[oru->ru->nb_tx],
                                    int frame,
                                    int slot,
                                    int start_symbol,
                                    int num_symbols,
                                    openair0_timestamp_t timestamp_tx);

int get_oru_options(ORU_t *oru)
{
  int DEFBW[] = {273};
  int DEFCARRIER[] = {3430560};
  int DEFTPCORES[] = {-1, -1, -1, -1};
  paramdef_t param[] = CMDLINE_PARAMS_DESC_ORU;
  int nump = sizeofArray(param);

  int ret = config_get(config_get_if(), param, nump, CONFIG_SECTION_ORU);
  if (ret <= 0) {
    printf("problem reading section \"%s\"\n", CONFIG_SECTION_ORU);
    return -1;
  }

  for (int i = 0; i < oru->ru->num_bands; i++) {
    oru->bw_tx[i] = gpd(param, nump, CONFIG_STRING_ORU_TX_BW_LIST)->iptr[i];
    oru->bw_rx[i] = gpd(param, nump, CONFIG_STRING_ORU_RX_BW_LIST)->iptr[i];
    oru->carrier_freq_tx[i] = gpd(param, nump, CONFIG_STRING_ORU_CARRIER_TX_LIST)->iptr[i];
    oru->carrier_freq_rx[i] = gpd(param, nump, CONFIG_STRING_ORU_CARRIER_RX_LIST)->iptr[i];
  }
  oru->frame_type = *gpd(param, nump, CONFIG_STRING_ORU_FRAME_TYPE)->iptr;
  oru->prach_config_index = *gpd(param, nump, CONFIG_STRING_ORU_PRACH_CONFIGID)->iptr;
  oru->prach_msg1_freq = *gpd(param, nump, CONFIG_STRING_ORU_PRACH_MSG1FREQ)->iptr;
  oru->numerology = *gpd(param, nump, CONFIG_STRING_ORU_NUMEROLOGY)->iptr;
  oru->tdd_period = *gpd(param, nump, CONFIG_STRING_ORU_TDD_PERIOD)->iptr;
  oru->num_DL_slots = *gpd(param, nump, CONFIG_STRING_ORU_NUM_DL_SLOTS)->iptr;
  oru->num_UL_slots = *gpd(param, nump, CONFIG_STRING_ORU_NUM_UL_SLOTS)->iptr;
  oru->num_DL_symbols = *gpd(param, nump, CONFIG_STRING_ORU_NUM_DL_SYMBOLS)->iptr;
  oru->num_UL_symbols = *gpd(param, nump, CONFIG_STRING_ORU_NUM_UL_SYMBOLS)->iptr;

  int* tp_cores = gpd(param, nump, CONFIG_STRING_ORU_TP_CORES)->iptr;
  int num_tp_cores = gpd(param, nump, CONFIG_STRING_ORU_TP_CORES)->numelt;
  AssertFatal(num_tp_cores > 0, "No threadpool cores specified\n");

  char tpool_config[(3 + 1) * num_tp_cores + 1];
  char* tpool_config_p = tpool_config;
  for (int i = 0; i < num_tp_cores; i++) {
    int ret = snprintf(tpool_config_p, 4, "%d,", tp_cores[i]);
    AssertFatal(ret > 0, "snprintf failed\n");
    tpool_config_p += ret;
  }
  *tpool_config_p = '\0';

  LOG_A(PHY, "ORU threadpool cores: %s\n", tpool_config);
  initTpool(tpool_config, &oru->tpool, false);

  return 0;
}

void oru_init_frame_parms(ORU_t *oru)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  fp->frame_type = oru->frame_type;
  ru->config.cell_config.frame_duplex_type.value = oru->frame_type;
  ru->config.cell_config.frame_duplex_type.tl.tag = 0x100D;
  fp->N_RB_DL = oru->bw_tx[0];
  ru->config.ssb_config.scs_common.value = ru->numerology;
  ru->config.carrier_config.dl_grid_size[ru->config.ssb_config.scs_common.value].value = oru->bw_tx[0];
  fp->N_RB_UL = oru->bw_rx[0];
  ru->config.carrier_config.ul_grid_size[ru->config.ssb_config.scs_common.value].value = oru->bw_rx[0];
  fp->numerology_index = ru->numerology;
  LOG_I(NR_PHY,
        "Set RU frame type to %s, N_RB_DL %d, N_RB_UL %d, mu %d\n",
        oru->frame_type == TDD ? "TDD" : "FDD",
        oru->bw_tx[0],
        oru->bw_rx[0],
        ru->numerology);
  set_scs_parameters(fp, fp->numerology_index, oru->bw_tx[0], 0);
  fp->slots_per_frame = 10 * fp->slots_per_subframe;
  fp->nb_antennas_rx = ru->nb_rx;
  fp->nb_antennas_tx = ru->nb_tx;
  fp->symbols_per_slot = 14;
  fp->samples_per_subframe_wCP = fp->ofdm_symbol_size * fp->symbols_per_slot * fp->slots_per_subframe;
  fp->samples_per_frame_wCP = 10 * fp->samples_per_subframe_wCP;
  fp->samples_per_slot_wCP = fp->symbols_per_slot * fp->ofdm_symbol_size;
  fp->samples_per_slotN0 = (fp->nb_prefix_samples + fp->ofdm_symbol_size) * fp->symbols_per_slot;
  fp->samples_per_slot0 =
      fp->nb_prefix_samples0 + ((fp->symbols_per_slot - 1) * fp->nb_prefix_samples) + (fp->symbols_per_slot * fp->ofdm_symbol_size);
  fp->samples_per_subframe = (fp->nb_prefix_samples0 + fp->ofdm_symbol_size) * 2
                             + (fp->nb_prefix_samples + fp->ofdm_symbol_size) * (fp->symbols_per_slot * fp->slots_per_subframe - 2);
  fp->samples_per_frame = 10 * fp->samples_per_subframe;
  fp->freq_range = (oru->carrier_freq_tx[0] < 6e6) ? FR1 : FR2;
  ru->N_TA_offset = set_default_nta_offset(fp->freq_range, fp->samples_per_subframe);
  LOG_I(PHY,
        "ORU Setting N_TA_offset to %d samples (UL Freq %lu, N_RB %d, mu %d)\n",
        ru->N_TA_offset,
        oru->carrier_freq_rx[0],
        fp->N_RB_UL,
        fp->numerology_index);

  fp->dl_CarrierFreq = (double)oru->carrier_freq_tx[0] * 1000;
  fp->ul_CarrierFreq = (double)oru->carrier_freq_rx[0] * 1000;
  fp->Ncp = NORMAL;
  fp->ofdm_offset_divisor = 8;

  // Split 7.2 parameters
  ru->config.prach_config.num_prach_fd_occasions.value = 1;
  ru->config.prach_config.prach_ConfigurationIndex.value = oru->prach_config_index;
  ru->config.prach_config.prach_ConfigurationIndex.tl.tag = 0x1029;
  ru->config.prach_config.num_prach_fd_occasions_list = malloc(sizeof(*ru->config.prach_config.num_prach_fd_occasions_list));
  ru->config.prach_config.num_prach_fd_occasions_list[0].k1.value = oru->prach_msg1_freq;
  if (ru->config.cell_config.frame_duplex_type.value == 1 /* TDD */) {
    ru->config.tdd_table.tdd_period.value = oru->tdd_period;
    ru->config.tdd_table.tdd_period.tl.tag = 0x1026;
    int numb_slots_frame = (1 << ru->numerology) * NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
    int numb_period_frame = get_nb_periods_per_frame(oru->tdd_period);
    int numb_slots_period = numb_slots_frame / numb_period_frame;
    ru->config.tdd_table.max_tdd_periodicity_list =
        malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list) * (numb_slots_frame));
    for (int n = 0; n < numb_slots_frame; n++) {
      int s = 0;
      int p = n % numb_slots_period;
      if (p < oru->num_DL_slots) {
        ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list =
            malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list)
                   * NR_SYMBOLS_PER_SLOT);
        for (s = 0; s < 14; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 0;
      } else if (p == oru->num_DL_slots) {
        ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list =
            malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list)
                   * NR_SYMBOLS_PER_SLOT);
        for (s = 0; s < oru->num_DL_symbols; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 0;
        for (; s < NR_SYMBOLS_PER_SLOT - oru->num_UL_symbols; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 2;
        for (; s < NR_SYMBOLS_PER_SLOT; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 1;
      } else {
        ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list =
            malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list)
                   * NR_SYMBOLS_PER_SLOT);
        for (s = 0; s < NR_SYMBOLS_PER_SLOT; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 1;
      }
    }
  }
}

void initialize_sync_params(NR_DL_FRAME_PARMS *fp, sync_params_t *sync_params, initial_sync_t *initial_sync)
{
  sync_params->frame_unwrap = 0;
  sync_params->last_frame = initial_sync->frame;
  sync_params->sync_offset = initial_sync->sample;
  sync_params->sync_offset -=
      (uint64_t)(initial_sync->frame) * fp->samples_per_subframe * 10 + get_samples_slot_timestamp(fp, initial_sync->slot);
}

static openair0_timestamp_t get_timestamp(ORU_t *oru, sense_of_time_t *sense_of_time, sync_params_t *sync_params)
{
  if (sync_params->last_frame > sense_of_time->frame) {
    sync_params->frame_unwrap++;
  }
  sync_params->last_frame = sense_of_time->frame;
  NR_DL_FRAME_PARMS *fp = oru->ru->nr_frame_parms;
  int num_frames = sense_of_time->frame + sync_params->frame_unwrap * 1024;

  uint64_t timestamp = (uint64_t)(num_frames)*fp->samples_per_subframe * 10 + get_samples_slot_timestamp(fp, sense_of_time->slot)
                       + get_samples_symbol_timestamp(fp, sense_of_time->slot, sense_of_time->symbol);

  timestamp += sync_params->sync_offset;
  return timestamp;
}

void receive_prach(ORU_t *oru, int frame, int slot, int prach_symbol)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  oru->prach_item.frame = frame;
  oru->prach_item.slot = slot;

#ifdef ORU_PRACH_RAW_DEBUG
  debug_oru_prach_raw_window(oru, frame, slot, prach_symbol);
#endif

  rx_nr_prach_ru_internal(&oru->prach_item,
                          0,
                          oru->prach_info.start_symbol,
                          0,
                          ru->common.rxdata,
                          fp,
                          ru->N_TA_offset,
                          prach_symbol,
                          1);
  for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
    ru->ifdevice.xran_api.write_prach((uint32_t *)oru->prach_item.rxsigF[0][aarx], aarx, frame, slot, prach_symbol);
  }
}

// Returns PRACH symbol that was received in current frame, slot and symbol.
// If no PRACH symbol was received, returns -1
int get_prach_symbol(ORU_t *oru, int frame, int slot, int symbol, int numerology)
{
  uint16_t RA_sfn_index;
  AssertFatal(oru->ru->nr_frame_parms->frame_type == TDD, "Only supports TDD\n");
  if (get_nr_prach_sched_from_info(oru->prach_info, oru->prach_config_index, frame, slot, numerology, FR1, &RA_sfn_index, true)) {
    int format = oru->prach_item.pdu.prach_format;
    int start_symbol = oru->prach_item.pdu.prach_start_symbol;
    symbol -= start_symbol;
    // TODO: Support more PRACH formats
    AssertFatal(format == 8, "only support format B4\n");
    // TODO: This is not exactly the case but it is correct
    if (symbol >= 0 && symbol < 12) {
      return symbol;
    }
  }
  return -1;
}

void *oru_north_read_thread(void *arg)
{
  ORU_t *oru = (ORU_t *)arg;

  RU_t *ru = (RU_t *)oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  AssertFatal(ru->ifdevice.xran_api.north_in_func != NULL, "No fronthaul interface at north port");
  __attribute__((aligned(32))) c16_t txDataF[ru->nb_tx][fp->ofdm_symbol_size * 14];
  memset(txDataF, 0, sizeof(txDataF));
  c16_t *txDataF_ptr[ru->nb_tx];
  for (int aatx = 0; aatx < ru->nb_tx; aatx++) {
    txDataF_ptr[aatx] = txDataF[aatx];
  }
  ru->common.txdataF_BF = (int32_t **)txDataF_ptr;

  notifiedFIFO_elt_t *elt = pullNotifiedFIFO(&oru->sync_fifo);
  initial_sync_t *initial_sync = NotifiedFifoData(elt);
  sync_params_t sync_params;
  initialize_sync_params(fp, &sync_params, initial_sync);
  LOG_A(PHY,
        "ORU North read thread started at frame %d, slot %d, symbol %d\n",
        initial_sync->frame,
        initial_sync->slot,
        initial_sync->symbol);
  delNotifiedFIFO_elt(elt);

  while (!oai_exit) {
    int num_symbols = 0;
    sense_of_time_t sense_of_time;
    ru->ifdevice.xran_api.north_in_func((uint32_t **)txDataF_ptr, ru->nb_tx, &sense_of_time, &num_symbols);
    openair0_timestamp_t timestamp_tx = get_timestamp(oru, &sense_of_time, &sync_params);
    if ((sense_of_time.frame % 256 == 0) && sense_of_time.slot == 0) {
      LOG_I(PHY,
            "[RU_thread] read data: frame %d, slot %d, start_symbol %d, num_symbols %d\n",
            sense_of_time.frame,
            sense_of_time.slot,
            sense_of_time.symbol,
            num_symbols);
    }
    nfapi_nr_config_request_scf_t *cfg = &ru->config;
    int slot_type = nr_slot_select(cfg, sense_of_time.frame, sense_of_time.slot % fp->slots_per_frame);
    if (slot_type != NR_UPLINK_SLOT) {
      oru_downlink_processing(oru,
                              txDataF_ptr,
                              sense_of_time.frame,
                              sense_of_time.slot,
                              sense_of_time.symbol,
                              num_symbols,
                              timestamp_tx);
    }
  }
  return NULL;
}

void rx_initial_sync(ORU_t *oru, int *slot, int *frame)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  const int num_samples = 3000;
  c16_t throwaway_samples[ru->nb_rx][num_samples];
  void *rxp[ru->nb_rx];
  for (int i = 0; i < ru->nb_rx; i++)
    rxp[i] = throwaway_samples[i];

  openair0_timestamp_t timestamp;
  initial_sync_t initial_sync;
  while (!oai_exit) {
    int samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, num_samples, ru->nb_rx);
    AssertFatal(samples_read == num_samples, "Unexpected number of samples received\n");
    notifiedFIFO_elt_t *elt = pollNotifiedFIFO(&oru->sync_fifo);
    if (elt) {
      memcpy(&initial_sync, NotifiedFifoData(elt), sizeof(initial_sync));
      break;
    }
  }

  // Synchornize to ORAN timing
  int next_slot = initial_sync.slot;
  int next_frame = initial_sync.frame;
  openair0_timestamp_t next_sample = timestamp + num_samples;
  int64_t diff = next_sample - initial_sync.sample;
  LOG_I(PHY,
        "Sychronizing to frame slot %d.%d, sample %ld next_sample %ld diff %ld\n",
        next_frame,
        next_slot,
        initial_sync.sample,
        next_sample,
        diff);

  uint64_t samples_to_sync_by = 0;
  if (diff < 0) {
    samples_to_sync_by = -diff;
  } else {
    while (diff > 0) {
      uint32_t samples_per_slot = get_samples_per_slot(next_slot, fp);
      samples_to_sync_by += samples_per_slot;
      diff -= samples_per_slot;
      next_slot++;
      if (next_slot == fp->slots_per_frame) {
        next_slot = 0;
        next_frame++;
        if (next_frame == 1024) {
          next_frame = 0;
        }
      }
    }
    samples_to_sync_by += diff;
  }

  LOG_I(PHY, "Thrashing %lu samples to sync to slot %d, frame %d\n", samples_to_sync_by, next_slot, next_frame);
  while (!oai_exit && samples_to_sync_by > 0) {
    int samples_to_read = min(num_samples, samples_to_sync_by);
    int samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, samples_to_read, ru->nb_rx);
    AssertFatal(samples_to_read == samples_read, "Unexpected number of samples received\n");
    samples_to_sync_by -= samples_to_read;
  }
  *slot = next_slot;
  *frame = next_frame;
}

void *oru_south_read_thread(void *arg)
{
  ORU_t *oru = arg;

  int slot;
  int frame;
  rx_initial_sync(oru, &slot, &frame);
  LOG_A(PHY, "ORU South read thread started at frame %d, slot %d\n", frame, slot);

  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  const int max_pusch_jobs = 300;
  pusch_symbol_job_t pusch_job_pool[max_pusch_jobs];
  uint32_t pusch_job_index = 0;
  while (!oai_exit) {
    int rx_slot_type = nr_slot_select(&ru->config, frame, slot);
    for (int symbol = 0; symbol < 14; symbol++) {
      int samples_to_read = get_samples_symbol_duration(fp, slot, symbol, 1);
      size_t offset = get_samples_slot_timestamp(fp, slot) + get_samples_symbol_timestamp(fp, slot, symbol);
      c16_t *rxp[fp->nb_antennas_rx];
      for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
        rxp[aarx] = (c16_t *)&ru->common.rxdata[aarx][offset];
      }

      openair0_timestamp_t timestamp;
      int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, (void **)rxp, samples_to_read, ru->nb_rx);
      AssertFatal(num_samples_read == samples_to_read, "Unexpected number of samples received\n");
      LOG_D(PHY,
            "[ORU south] read data: frame %d, slot %d, symbol %d, timestamp %ld num_symbols %d, samples %d\n",
            frame,
            slot,
            symbol,
            timestamp,
            1,
            num_samples_read);

      if (rx_slot_type == NR_UPLINK_SLOT || rx_slot_type == NR_MIXED_SLOT) {

        nfapi_nr_config_request_scf_t *config = &ru->config;
        nfapi_nr_tdd_table_t *tdd_table = &config->tdd_table;
        AssertFatal(tdd_table->tdd_period.tl.tag == NFAPI_NR_CONFIG_TDD_PERIOD_TAG, "");
        int nb_periods_per_frame = get_nb_periods_per_frame(tdd_table->tdd_period.value);
        int n_tdd_period = fp->slots_per_frame / nb_periods_per_frame;
        nfapi_nr_max_num_of_symbol_per_slot_t *max_num_of_symbol_per_slot_list =
            config->tdd_table.max_tdd_periodicity_list[slot % n_tdd_period].max_num_of_symbol_per_slot_list;
        if (max_num_of_symbol_per_slot_list[symbol].slot_config.value != 1)
          continue;

        int prach_symbol = get_prach_symbol(oru, frame, slot, symbol, ru->numerology);
        if (prach_symbol != -1) {
          receive_prach(oru, frame, slot, prach_symbol);
        }
        for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
          pusch_symbol_job_t *job = &pusch_job_pool[pusch_job_index++ % max_pusch_jobs];
          job->oru = oru;
          job->aarx = aarx;
          job->frame = frame;
          job->slot = slot;
          job->symbol = symbol;
          task_t task = {.func = receive_pusch, .args = job};
          pushTpool(&oru->tpool, task);
        }
        stop_meas(&oru->rx);
      }
    }
    slot++;
    if (slot == fp->slots_per_frame) {
      slot = 0;
      frame++;
      if (frame == 1024) {
        frame = 0;
      }
    }
    if (frame % 256 == 0 && slot == 0) {
      LOG_I(PHY,
            "[ORU south] read data: frame %d, slot %d\n", frame, slot);
    }
  }

  // Perform RX processing
  return NULL;
}

void perform_initial_sync(ORU_t *oru, sense_of_time_t *sense_of_time, initial_sync_t *initial_sync)
{
  initial_sync->frame = sense_of_time->frame;
  initial_sync->slot = sense_of_time->slot;
  initial_sync->symbol = sense_of_time->symbol;
  initial_sync->sample = oru->ru->rfdevice.get_timestamp(&oru->ru->rfdevice, &sense_of_time->ts);
  LOG_I(PHY,
        "RU synchronized: frame, slot %d.%d, symbol %d, sample: %ld\n",
        initial_sync->frame,
        initial_sync->slot,
        initial_sync->symbol,
        initial_sync->sample);
}

void *oru_sync_thread(void *arg)
{
  ORU_t *oru = (ORU_t *)arg;

  RU_t *ru = (RU_t *)oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  AssertFatal(ru->ifdevice.xran_api.north_in_func != NULL, "No fronthaul interface at north port");
  __attribute__((aligned(32))) c16_t txDataF[ru->nb_tx][ceil_mod(fp->ofdm_symbol_size * 14, 32)];
  c16_t *txDataF_ptr[ru->nb_tx];
  for (int aatx = 0; aatx < ru->nb_tx; aatx++) {
    txDataF_ptr[aatx] = txDataF[aatx];
  }

  initial_sync_t initial_sync;
  while (!oai_exit) {
    int num_symbols = 0;
    sense_of_time_t sense_of_time;
    ru->ifdevice.xran_api.north_in_func((uint32_t **)txDataF_ptr, ru->nb_tx, &sense_of_time, &num_symbols);
    if (sense_of_time.symbol == 0) {
      perform_initial_sync(oru, &sense_of_time, &initial_sync);
      break;
    }
  }

  for (int i = 0; i < oru->num_sync_messages_needed; i++) {
    notifiedFIFO_elt_t *sync_msg = newNotifiedFIFO_elt(sizeof(initial_sync_t), 0, NULL, NULL);
    initial_sync_t *initial_sync_p = NotifiedFifoData(sync_msg);
    *initial_sync_p = initial_sync;
    pushNotifiedFIFO(&oru->sync_fifo, sync_msg);
  }

  return NULL;
}

static void dl_symbol_process(void *arg)
{
  dl_symbol_process_t *args = (dl_symbol_process_t *)arg;
  apply_nr_rotation_TX(args->fp,
                       args->txdataF,
                       false,
                       args->fp->symbol_rotation[0],
                       args->slot,
                       args->fp->N_RB_DL,
                       args->start_symbol,
                       args->num_symbols);
  nr_feptx0(args->ru, args->slot, args->start_symbol, args->num_symbols, args->aatx);
  completed_task_ans(args->task_ans);
}

static void oru_downlink_processing(ORU_t *oru,
                                    c16_t *txDataF_ptr[oru->ru->nb_tx],
                                    int frame,
                                    int slot,
                                    int start_symbol,
                                    int num_symbols,
                                    openair0_timestamp_t timestamp_tx)
{
  static int dl_proc_count = 0;
  if (dl_proc_count++ < 10) {
    LOG_I(PHY, "[ORU] oru_downlink_processing ENTER: frame %d, slot %d, sym %d, num_sym %d\n",
          frame, slot, start_symbol, num_symbols);
  }

  RU_t *ru = oru->ru;
  start_meas(&ru->tx_fhaul);
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  int num_paralell_workers_per_antenna = num_symbols > 4 ? 2 : 1; // Ensure at least quarter slot parallelization
  task_t tasks[ru->nb_tx][num_paralell_workers_per_antenna];
  dl_symbol_process_t dl_process_args[ru->nb_tx][num_paralell_workers_per_antenna];
  task_ans_t task_ans;
  init_task_ans(&task_ans, num_paralell_workers_per_antenna * ru->nb_tx);

  if (dl_proc_count <= 10) {
    LOG_I(PHY, "[ORU] Scheduling %d tasks (%d antennas x %d workers)\n",
          ru->nb_tx * num_paralell_workers_per_antenna, ru->nb_tx, num_paralell_workers_per_antenna);
  }

  for (int aatx = 0; aatx < ru->nb_tx; aatx++) {
    for (int i = 0; i < num_paralell_workers_per_antenna; i++) {
      tasks[aatx][i].func = dl_symbol_process;
      tasks[aatx][i].args = (void *)&dl_process_args[aatx][i];
      dl_process_args[aatx][i].ru = ru;
      dl_process_args[aatx][i].fp = fp;
      dl_process_args[aatx][i].slot = slot;
      dl_process_args[aatx][i].start_symbol = start_symbol + num_symbols / num_paralell_workers_per_antenna * i;
      dl_process_args[aatx][i].num_symbols =
          min(num_symbols / num_paralell_workers_per_antenna, num_symbols - (num_symbols / num_paralell_workers_per_antenna) * i);
      dl_process_args[aatx][i].aatx = aatx;
      dl_process_args[aatx][i].txdataF = txDataF_ptr[aatx];
      dl_process_args[aatx][i].task_ans = &task_ans;
      pushTpool(&oru->tpool, tasks[aatx][i]);
    }
  }

  if (dl_proc_count <= 10) {
    LOG_I(PHY, "[ORU] Tasks scheduled, joining...\n");
  }

  join_task_ans(&task_ans);

  if (dl_proc_count <= 10) {
    LOG_I(PHY, "[ORU] Tasks completed, calling tx_rf_symbols for frame %d, slot %d\n", frame, slot);
  }

  tx_rf_symbols(ru, frame, slot, timestamp_tx, start_symbol, num_symbols);

  if (dl_proc_count <= 10) {
    LOG_I(PHY, "[ORU] tx_rf_symbols returned\n");
  }

  stop_meas(&ru->tx_fhaul);
}

void prepare_prach_item(ORU_t *oru)
{
  AssertFatal(oru->ru != NULL, "ORU not configured\n");
  AssertFatal(oru->ru->nr_frame_parms != NULL, "ORU not configured\n");
  NR_DL_FRAME_PARMS *fp = oru->ru->nr_frame_parms;
  RU_t *ru = oru->ru;
  prach_item_t *prach_item = &oru->prach_item;
  prach_item->num_slots = oru->prach_info.format < 4 ? get_long_prach_dur(oru->prach_info.format, fp->numerology_index) : 1;
  prach_item->msg1_frequencystart = oru->prach_msg1_freq;
  prach_item->mu = fp->numerology_index;
  nfapi_nr_config_request_scf_t *cfg = &ru->config;
  prach_item->prach_sequence_length = cfg->prach_config.prach_sequence_length.value;
  prach_item->restricted_set = 0;
  prach_item->numerology_index = fp->numerology_index;
  prach_item->nb_rx = ru->nb_rx;
  prach_item->rx_prach = &oru->rx_prach;
  prach_item->beams[0] = 0; // TODO: Beamforming not supported yet

  // Fill PRACH PDU
  nfapi_nr_prach_pdu_t *prach_pdu = &prach_item->pdu;
  prach_pdu->prach_start_symbol = oru->prach_info.start_symbol;
  prach_pdu->num_prach_ocas = 1; // TODO: Hardcoded.

  uint16_t format0 = oru->prach_info.format & 0xff;
  uint16_t format1 = (oru->prach_info.format >> 8) & 0xff;
  if (format1 != 0xff) {
    switch (format0) {
      case 0xa1:
        prach_pdu->prach_format = 11;
        break;
      case 0xa2:
        prach_pdu->prach_format = 12;
        break;
      case 0xa3:
        prach_pdu->prach_format = 13;
        break;
      default:
        AssertFatal(1 == 0, "Only formats A1/B1 A2/B2 A3/B3 are valid for dual format");
    }
  } else {
    switch (format0) {
      case 0:
        prach_pdu->prach_format = 0;
        break;
      case 1:
        prach_pdu->prach_format = 1;
        break;
      case 2:
        prach_pdu->prach_format = 2;
        break;
      case 3:
        prach_pdu->prach_format = 3;
        break;
      case 0xa1:
        prach_pdu->prach_format = 4;
        break;
      case 0xa2:
        prach_pdu->prach_format = 5;
        break;
      case 0xa3:
        prach_pdu->prach_format = 6;
        break;
      case 0xb1:
        prach_pdu->prach_format = 7;
        break;
      case 0xb4:
        prach_pdu->prach_format = 8;
        break;
      case 0xc0:
        prach_pdu->prach_format = 9;
        break;
      case 0xc2:
        prach_pdu->prach_format = 10;
        break;
      default:
        AssertFatal(1 == 0, "Invalid PRACH format");
    }
  }
}

static void receive_pusch(void *args)
{
  pusch_symbol_job_t *job = args;
  ORU_t *oru = job->oru;
  int frame = job->frame;
  int slot = job->slot;
  int symbol = job->symbol;
  int aarx = job->aarx;
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  c16_t rxdataF[fp->ofdm_symbol_size] __attribute__((aligned(32)));

  nr_symbol_fep_ul(fp, (c16_t *)ru->common.rxdata[aarx], rxdataF, symbol, slot, ru->N_TA_offset);
  apply_nr_rotation_symbol_RX(fp,
                              rxdataF,
                              fp->symbol_rotation[link_type_ul],
                              fp->N_RB_UL,
                              slot,
                              symbol);
  ru->ifdevice.xran_api.write_pusch((uint32_t *)rxdataF, aarx, frame, slot, symbol);
}
