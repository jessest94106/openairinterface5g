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

/***********************************************************************
*
* FILENAME    :  srs_modulation_nr_nr.c
*
* MODULE      :
*
* DESCRIPTION :  function to set uplink reference symbols
*                see TS 38211 6.4.1.4 Sounding reference signal
*
************************************************************************/

#include <stdio.h>
#include <math.h>

#include "PHY/impl_defs_nr.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/NR_REFSIG/ss_pbch_nr.h"
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/NR_REFSIG/ul_ref_seq_nr.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"
#include "PHY/NR_UE_TRANSPORT/srs_modulation_nr.h"

static void configure_srs_info(fapi_nr_ul_config_srs_pdu *srs_config_pdu, nr_srs_info_t *nr_srs_info)
{
  nr_srs_info->B_SRS = srs_config_pdu->bandwidth_index;
  nr_srs_info->C_SRS = srs_config_pdu->config_index;
  nr_srs_info->b_hop = srs_config_pdu->frequency_hopping;
  nr_srs_info->comb_size = srs_config_pdu->comb_size;
  nr_srs_info->K_TC_overbar = srs_config_pdu->comb_offset;
  nr_srs_info->n_SRS_cs = srs_config_pdu->cyclic_shift;
  nr_srs_info->n_ID_SRS = srs_config_pdu->sequence_id;
  // It adjusts the SRS allocation to align with the common resource block grid in multiples of four
  nr_srs_info->n_shift = srs_config_pdu->frequency_position;
  nr_srs_info->n_RRC = srs_config_pdu->frequency_shift;
  nr_srs_info->groupOrSequenceHopping = srs_config_pdu->group_or_sequence_hopping;
  nr_srs_info->l_offset = srs_config_pdu->time_start_position;
  nr_srs_info->T_SRS = srs_config_pdu->t_srs;
  nr_srs_info->T_offset = srs_config_pdu->t_offset;
  nr_srs_info->R = 1 << srs_config_pdu->num_repetitions;
  nr_srs_info->N_symb_SRS = 1 << srs_config_pdu->num_symbols; // Number of consecutive OFDM symbols
  nr_srs_info->n_srs_ports = 1 << srs_config_pdu->num_ant_ports; // Number of antenna port for transmission
  nr_srs_info->resource_type = srs_config_pdu->resource_type;
}

/*******************************************************************
*
* NAME :         ue_srs_procedures_nr
*
* PARAMETERS :   pointer to ue context
*                pointer to rxtx context*
*
* RETURN :        0 if it is a valid slot for transmitting srs
*                -1 if srs should not be transmitted
*
* DESCRIPTION :  ue srs procedure
*                send srs according to current configuration
*
*********************************************************************/
int ue_srs_procedures_nr(PHY_VARS_NR_UE *ue,
                         const UE_nr_rxtx_proc_t *proc,
                         c16_t **txdataF,
                         nr_phy_data_tx_t *phy_data,
                         bool was_symbol_used[NR_NUMBER_OF_SYMBOLS_PER_SLOT])
{
  if(phy_data->srs_vars.active == false) {
    return -1;
  }

  fapi_nr_ul_config_srs_pdu *srs_config_pdu = &phy_data->srs_vars.srs_config_pdu;
  int first_srs_symbol = ue->frame_parms.symbols_per_slot - 1 - srs_config_pdu->time_start_position;
  // Num consecutive SRS symbols according to 38.211 6.4.1.4.1
  int num_srs_symbols[] = {1, 2, 4, 8, 12};
  int last_srs_symbol = first_srs_symbol + num_srs_symbols[srs_config_pdu->num_symbols] - 1;
  for (int i = first_srs_symbol; i <= last_srs_symbol; i++) {
    was_symbol_used[i] = true;
  }

#ifdef SRS_DEBUG
  LOG_I(NR_PHY,"Frame = %i, slot = %i\n", proc->frame_tx, proc->nr_slot_tx);
  LOG_I(NR_PHY,"srs_config_pdu->rnti = 0x%04x\n", srs_config_pdu->rnti);
  LOG_I(NR_PHY,"srs_config_pdu->handle = %u\n", srs_config_pdu->handle);
  LOG_I(NR_PHY,"srs_config_pdu->bwp_size = %u\n", srs_config_pdu->bwp_size);
  LOG_I(NR_PHY,"srs_config_pdu->bwp_start = %u\n", srs_config_pdu->bwp_start);
  LOG_I(NR_PHY,"srs_config_pdu->subcarrier_spacing = %u\n", srs_config_pdu->subcarrier_spacing);
  LOG_I(NR_PHY,"srs_config_pdu->cyclic_prefix = %u (0: Normal; 1: Extended)\n", srs_config_pdu->cyclic_prefix);
  LOG_I(NR_PHY,"srs_config_pdu->num_ant_ports = %u (0 = 1 port, 1 = 2 ports, 2 = 4 ports)\n", srs_config_pdu->num_ant_ports);
  LOG_I(NR_PHY,"srs_config_pdu->num_symbols = %u (0 = 1 symbol, 1 = 2 symbols, 2 = 4 symbols)\n", srs_config_pdu->num_symbols);
  LOG_I(NR_PHY,"srs_config_pdu->num_repetitions = %u (0 = 1, 1 = 2, 2 = 4)\n", srs_config_pdu->num_repetitions);
  LOG_I(NR_PHY,"srs_config_pdu->time_start_position = %u\n", srs_config_pdu->time_start_position);
  LOG_I(NR_PHY,"srs_config_pdu->config_index = %u\n", srs_config_pdu->config_index);
  LOG_I(NR_PHY,"srs_config_pdu->sequence_id = %u\n", srs_config_pdu->sequence_id);
  LOG_I(NR_PHY,"srs_config_pdu->bandwidth_index = %u\n", srs_config_pdu->bandwidth_index);
  LOG_I(NR_PHY,"srs_config_pdu->comb_size = %u (0 = comb size 2, 1 = comb size 4, 2 = comb size 8)\n", srs_config_pdu->comb_size);
  LOG_I(NR_PHY,"srs_config_pdu->comb_offset = %u\n", srs_config_pdu->comb_offset);
  LOG_I(NR_PHY,"srs_config_pdu->cyclic_shift = %u\n", srs_config_pdu->cyclic_shift);
  LOG_I(NR_PHY,"srs_config_pdu->frequency_position = %u\n", srs_config_pdu->frequency_position);
  LOG_I(NR_PHY,"srs_config_pdu->frequency_shift = %u\n", srs_config_pdu->frequency_shift);
  LOG_I(NR_PHY,"srs_config_pdu->frequency_hopping = %u\n", srs_config_pdu->frequency_hopping);
  LOG_I(NR_PHY,"srs_config_pdu->group_or_sequence_hopping = %u (0 = No hopping, 1 = Group hopping groupOrSequenceHopping, 2 = Sequence hopping)\n", srs_config_pdu->group_or_sequence_hopping);
  LOG_I(NR_PHY,"srs_config_pdu->resource_type = %u (0: aperiodic, 1: semi-persistent, 2: periodic)\n", srs_config_pdu->resource_type);
  LOG_I(NR_PHY,"srs_config_pdu->t_srs = %u\n", srs_config_pdu->t_srs);
  LOG_I(NR_PHY,"srs_config_pdu->t_offset = %u\n", srs_config_pdu->t_offset);
#endif

  configure_srs_info(srs_config_pdu, ue->nr_srs_info);
  NR_DL_FRAME_PARMS *frame_parms = &(ue->frame_parms);
  uint16_t symbol_offset = (frame_parms->symbols_per_slot - 1 - srs_config_pdu->time_start_position)*frame_parms->ofdm_symbol_size;

  if (generate_srs_nr(frame_parms,
                      txdataF,
                      symbol_offset,
                      srs_config_pdu->bwp_start,
                      ue->nr_srs_info,
                      AMP,
                      proc->frame_tx,
                      proc->nr_slot_tx)) {
    return 0;
  } else {
    return -1;
  }
}
