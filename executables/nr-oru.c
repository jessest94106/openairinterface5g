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
#include "common/config/config_userapi.h"
#include "nr-oru.h"
#include "openair1/PHY/defs_nr_common.h"

#define CONFIG_SECTION_ORU "ORUs.[0]"

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
}

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

int get_oru_options(ORU_t *oru)
{
  int DEFBW[] = {273};
  int DEFCARRIER[] = {3430560};
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

  notifiedFIFO_elt_t * elt = pullNotifiedFIFO(&oru->sync_fifo);
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
    if (sense_of_time.frame % 256 == 0 && sense_of_time.slot == 0) {
      LOG_I(PHY,
            "[RU_thread] read data: frame %d, slot %d, start_symbol %d, num_symbols %d\n",
            sense_of_time.frame,
            sense_of_time.slot,
            sense_of_time.symbol,
            num_symbols);
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
  LOG_A(PHY,
        "ORU South read thread started at frame %d, slot %d\n",
        frame,
        slot);

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
