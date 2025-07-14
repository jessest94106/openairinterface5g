/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.0  (the "License"); you may not use this file
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

#include "nr-ue-ru.h"
#include "nr-uesoftmodem.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"

/* NR UE RU configuration section name */
#define CONFIG_STRING_NRUE_RU_LIST "RUs"

#define NRUE_RU_NB_TX "nb_tx"
#define NRUE_RU_NB_RX "nb_rx"
#define NRUE_RU_ATT_TX "att_tx"
#define NRUE_RU_ATT_RX "att_rx"
#define NRUE_RU_MAX_RXGAIN "max_rxgain"
#define NRUE_RU_SDR_ADDRS "sdr_addrs"
#define NRUE_RU_TX_SUBDEV "tx_subdev"
#define NRUE_RU_RX_SUBDEV "rx_subdev"
#define NRUE_RU_CLOCK_SRC "clock_src"
#define NRUE_RU_TIME_SRC "time_src"
#define NRUE_RU_TUNE_OFFSET "tune_offset"
#define NRUE_RU_IF_FREQUENCY "if_freq"
#define NRUE_RU_IF_FREQ_OFFSET "if_offset"

#define NRUE_RU_SRC_CHECK                                                                                        \
  &(checkedparam_t)                                                                                              \
  {                                                                                                              \
    .s3a = { config_checkstr_assign_integer, {"internal", "external", "gpsdo"}, {internal, external, gpsdo}, 3 } \
  }

/*------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                                       NR UE RU configuration parameters                                                        */
/* optname                   helpstr                    paramflags  XXXptr        defXXXval              type         numelt  chkPptr             */
/*------------------------------------------------------------------------------------------------------------------------------------------------*/
// clang-format off
#define NRUE_RU_PARAMS_DESC { \
  {NRUE_RU_NB_TX,            CONFIG_HLP_UENANTT,        0,         .uptr=NULL,   .defuintval=1,          TYPE_UINT,   0,      NULL              }, \
  {NRUE_RU_NB_RX,            CONFIG_HLP_UENANTR,        0,         .uptr=NULL,   .defuintval=1,          TYPE_UINT,   0,      NULL              }, \
  {NRUE_RU_ATT_TX,           NULL,                      0,         .uptr=NULL,   .defuintval=0,          TYPE_UINT,   0,      NULL              }, \
  {NRUE_RU_ATT_RX,           NULL,                      0,         .uptr=NULL,   .defuintval=0,          TYPE_UINT,   0,      NULL              }, \
  {NRUE_RU_MAX_RXGAIN,       NULL,                      0,         .iptr=NULL,   .defintval=120,         TYPE_INT,    0,      NULL              }, \
  {NRUE_RU_SDR_ADDRS,        CONFIG_HLP_USRP_ARGS,      0,         .strptr=NULL, .defstrval="type=b200", TYPE_STRING, 0,      NULL              }, \
  {NRUE_RU_TX_SUBDEV,        CONFIG_HLP_TX_SUBDEV,      0,         .strptr=NULL, .defstrval="",          TYPE_STRING, 0,      NULL              }, \
  {NRUE_RU_RX_SUBDEV,        CONFIG_HLP_RX_SUBDEV,      0,         .strptr=NULL, .defstrval="",          TYPE_STRING, 0,      NULL              }, \
  {NRUE_RU_CLOCK_SRC,        NULL,                      0,         .strptr=NULL, .defstrval="internal",  TYPE_STRING, 0,      NRUE_RU_SRC_CHECK }, \
  {NRUE_RU_TIME_SRC,         NULL,                      0,         .strptr=NULL, .defstrval="internal",  TYPE_STRING, 0,      NRUE_RU_SRC_CHECK }, \
  {NRUE_RU_TUNE_OFFSET,      CONFIG_HLP_TUNE_OFFSET,    0,         .dblptr=NULL, .defdblval=0.0,         TYPE_DOUBLE, 0,      NULL              }, \
  {NRUE_RU_IF_FREQUENCY,     CONFIG_HLP_IF_FREQ,        0,         .u64ptr=NULL, .defint64val=0,         TYPE_UINT64, 0,      NULL              }, \
  {NRUE_RU_IF_FREQ_OFFSET,   CONFIG_HLP_IF_FREQ_OFF,    0,         .iptr=NULL,   .defintval=0,           TYPE_INT,    0,      NULL              }, \
}
// clang-format on

static int nrue_ru_count;
static nrUE_RU_params_t *nrue_rus;

openair0_config_t openair0_cfg[MAX_CARDS];
openair0_device_t openair0_dev[MAX_CARDS];

int nrue_get_ru_count(void)
{
  return nrue_ru_count;
}

const nrUE_RU_params_t *nrue_get_ru(int ru_id)
{
  AssertFatal(ru_id >= 0 && ru_id < nrue_ru_count, "Invalid RU ID %d! RU count = %d\n", ru_id, nrue_ru_count);
  return &nrue_rus[ru_id];
}

void nrue_set_ru_params(configmodule_interface_t *cfg)
{
  paramdef_t RUParams[] = NRUE_RU_PARAMS_DESC;
  paramlist_def_t RUParamList = {CONFIG_STRING_NRUE_RU_LIST, NULL, 0};
  config_getlist(cfg, &RUParamList, RUParams, sizeofArray(RUParams), NULL);

  if (RUParamList.numelt <= 0) {
    nrue_ru_count = 1;
    nrue_rus = calloc_or_fail(nrue_ru_count, sizeof(nrUE_RU_params_t));
    nrue_rus[0] = (nrUE_RU_params_t){.nb_tx = get_nrUE_params()->nb_antennas_tx,
                                     .nb_rx = get_nrUE_params()->nb_antennas_rx,
                                     .att_tx = get_nrUE_params()->tx_gain,
                                     .att_rx = 0,
                                     .max_rxgain = get_nrUE_params()->rx_gain,
                                     .sdr_addrs = get_nrUE_params()->usrp_args,
                                     .tx_subdev = get_nrUE_params()->tx_subdev,
                                     .rx_subdev = get_nrUE_params()->rx_subdev,
                                     .clock_source = get_softmodem_params()->clock_source,
                                     .time_source = get_softmodem_params()->timing_source,
                                     .tune_offset = get_softmodem_params()->tune_offset,
                                     .if_frequency = get_nrUE_params()->if_freq,
                                     .if_freq_offset = get_nrUE_params()->if_freq_off};
    return;
  }

  nrue_ru_count = RUParamList.numelt;
  nrue_rus = calloc_or_fail(nrue_ru_count, sizeof(nrUE_RU_params_t));

  LOG_W(NR_PHY, "Ignoring command line RU parameters, using the following instead:\n");

  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    nrue_rus[ru_id].nb_tx            = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_NB_TX)->uptr);
    nrue_rus[ru_id].nb_rx            = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_NB_RX)->uptr);
    nrue_rus[ru_id].att_tx           = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_ATT_TX)->uptr);
    nrue_rus[ru_id].att_rx           = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_ATT_RX)->uptr);
    nrue_rus[ru_id].max_rxgain       = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_MAX_RXGAIN)->iptr);
    nrue_rus[ru_id].tune_offset      = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_TUNE_OFFSET)->dblptr);
    nrue_rus[ru_id].if_frequency     = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_IF_FREQUENCY)->u64ptr);
    nrue_rus[ru_id].if_freq_offset   = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_IF_FREQ_OFFSET)->iptr);

    nrue_rus[ru_id].sdr_addrs = strdup(*(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_SDR_ADDRS)->strptr));
    nrue_rus[ru_id].tx_subdev = strdup(*(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_TX_SUBDEV)->strptr));
    nrue_rus[ru_id].rx_subdev = strdup(*(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_RX_SUBDEV)->strptr));

    int clock_src_idx = config_paramidx_fromname(RUParams, sizeofArray(RUParams), NRUE_RU_CLOCK_SRC);
    AssertFatal(clock_src_idx >= 0, "Index for clock_src config option not found!\n");
    nrue_rus[ru_id].clock_source = config_get_processedint(cfg, &RUParamList.paramarray[ru_id][clock_src_idx]);

    int time_src_idx = config_paramidx_fromname(RUParams, sizeofArray(RUParams), NRUE_RU_TIME_SRC);
    AssertFatal(time_src_idx >= 0, "Index for time_src config option not found!\n");
    nrue_rus[ru_id].time_source = config_get_processedint(cfg, &RUParamList.paramarray[ru_id][time_src_idx]);

    LOG_I(NR_PHY,
          "RU %d: nb_tx %d, nb_rx %d, att_tx %d, att_rx %d, max_rxgain %d, tune_offset %f, if_frequency %lu, if_freq_offset %d, "
          "sdr_addrs \"%s\", tx_subdev \"%s\", rx_subdev \"%s\", clock_source %d, time_source %d\n",
          ru_id,
          nrue_rus[ru_id].nb_tx,
          nrue_rus[ru_id].nb_rx,
          nrue_rus[ru_id].att_tx,
          nrue_rus[ru_id].att_rx,
          nrue_rus[ru_id].max_rxgain,
          nrue_rus[ru_id].tune_offset,
          nrue_rus[ru_id].if_frequency,
          nrue_rus[ru_id].if_freq_offset,
          nrue_rus[ru_id].sdr_addrs,
          nrue_rus[ru_id].tx_subdev,
          nrue_rus[ru_id].rx_subdev,
          nrue_rus[ru_id].clock_source,
          nrue_rus[ru_id].time_source);
  }
}

void nrue_init_openair0(const PHY_VARS_NR_UE *ue)
{
  int freq_off = 0;
  bool is_sidelink = (get_softmodem_params()->sl_mode) ? true : false;

  memset(openair0_cfg, 0, sizeof(openair0_config_t) * MAX_CARDS);
  memset(openair0_dev, 0, sizeof(openair0_device_t) * MAX_CARDS);

  AssertFatal(MAX_CARDS >= nrue_ru_count,
              "Too many RUs allocated (%d)! Maybe increase MAX_CARDS (%d).\n",
              nrue_ru_count,
              MAX_CARDS);

  const NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  if (is_sidelink)
    frame_parms = &ue->SL_UE_PHY_PARAMS.sl_frame_params;

  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    openair0_config_t *cfg = &openair0_cfg[ru_id];
    cfg->configFilename    = NULL;
    cfg->sample_rate       = frame_parms->samples_per_subframe * 1e3;
    cfg->samples_per_frame = frame_parms->samples_per_frame;

    if (frame_parms->frame_type == TDD)
      cfg->duplex_mode = duplex_mode_TDD;
    else
      cfg->duplex_mode = duplex_mode_FDD;

    cfg->Mod_id = 0;
    cfg->num_rb_dl = frame_parms->N_RB_DL;
    cfg->tx_num_channels = min(4, frame_parms->nb_antennas_tx);
    cfg->rx_num_channels = min(4, frame_parms->nb_antennas_rx);

    LOG_I(PHY,
          "HW: Configuring ru_id %d, sample_rate %f, tx/rx num_channels %d/%d, duplex_mode %s\n",
          ru_id,
          cfg->sample_rate,
          cfg->tx_num_channels,
          cfg->rx_num_channels,
          duplex_mode_txt[cfg->duplex_mode]);

    uint64_t dl_carrier, ul_carrier;
    if (is_sidelink || nrue_rus[ru_id].if_frequency == 0) {
      dl_carrier = frame_parms->dl_CarrierFreq;
      ul_carrier = frame_parms->ul_CarrierFreq;
    } else {
      dl_carrier = nrue_rus[ru_id].if_frequency;
      ul_carrier = nrue_rus[ru_id].if_frequency + nrue_rus[ru_id].if_freq_offset;
    }

    nr_rf_card_config_freq(cfg, ul_carrier, dl_carrier, freq_off);
    nr_rf_card_config_gain(cfg);

    cfg->configFilename = get_softmodem_params()->rf_config_file;

    cfg->sdr_addrs = nrue_rus[ru_id].sdr_addrs;
    cfg->tx_subdev = nrue_rus[ru_id].tx_subdev;
    cfg->rx_subdev = nrue_rus[ru_id].rx_subdev;
    cfg->clock_source = nrue_rus[ru_id].clock_source;
    cfg->time_source = nrue_rus[ru_id].time_source;
    cfg->tune_offset = nrue_rus[ru_id].tune_offset;
  }
}

void nrue_ru_start(void)
{
  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    openair0_config_t *cfg0 = &openair0_cfg[ru_id];
    openair0_device_t *dev0 = &openair0_dev[ru_id];

    dev0->host_type = RAU_HOST;
    int tmp = openair0_device_load(dev0, cfg0);
    AssertFatal(tmp == 0, "Could not load the device %d\n", ru_id);
    int tmp2 = dev0->trx_start_func(dev0);
    AssertFatal(tmp2 == 0, "Could not start the device %d\n", ru_id);
    if (usrp_tx_thread == 1)
      dev0->trx_write_init(dev0);
  }
}

void nrue_ru_end(void)
{
  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    if (openair0_dev[ru_id].trx_get_stats_func)
      openair0_dev[ru_id].trx_get_stats_func(&openair0_dev[ru_id]);
    if (openair0_dev[ru_id].trx_end_func)
      openair0_dev[ru_id].trx_end_func(&openair0_dev[ru_id]);
  }
}
