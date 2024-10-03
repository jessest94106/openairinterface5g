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

#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"

//#define NR_CSIRS_DEBUG

void nr_generate_csi_rs(const NR_DL_FRAME_PARMS *frame_parms,
                        int32_t **dataF,
                        const int16_t amp,
                        nr_csi_info_t *nr_csi_info,
                        const nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *csi_params,
                        const int slot,
                        uint8_t *N_cdm_groups,
                        uint8_t *CDM_group_size,
                        uint8_t *k_prime,
                        uint8_t *l_prime,
                        uint8_t *N_ports,
                        uint8_t *j_cdm,
                        uint8_t *k_overline,
                        uint8_t *l_overline)
{
#ifdef NR_CSIRS_DEBUG
  LOG_I(NR_PHY, "csi_params->subcarrier_spacing = %i\n", csi_params->subcarrier_spacing);
  LOG_I(NR_PHY, "csi_params->cyclic_prefix = %i\n", csi_params->cyclic_prefix);
  LOG_I(NR_PHY, "csi_params->start_rb = %i\n", csi_params->start_rb);
  LOG_I(NR_PHY, "csi_params->nr_of_rbs = %i\n", csi_params->nr_of_rbs);
  LOG_I(NR_PHY, "csi_params->csi_type = %i (0:TRS, 1:CSI-RS NZP, 2:CSI-RS ZP)\n", csi_params->csi_type);
  LOG_I(NR_PHY, "csi_params->row = %i\n", csi_params->row);
  LOG_I(NR_PHY, "csi_params->freq_domain = %i\n", csi_params->freq_domain);
  LOG_I(NR_PHY, "csi_params->symb_l0 = %i\n", csi_params->symb_l0);
  LOG_I(NR_PHY, "csi_params->symb_l1 = %i\n", csi_params->symb_l1);
  LOG_I(NR_PHY, "csi_params->cdm_type = %i\n", csi_params->cdm_type);
  LOG_I(NR_PHY, "csi_params->freq_density = %i (0: dot5 (even RB), 1: dot5 (odd RB), 2: one, 3: three)\n", csi_params->freq_density);
  LOG_I(NR_PHY, "csi_params->scramb_id = %i\n", csi_params->scramb_id);
  LOG_I(NR_PHY, "csi_params->power_control_offset = %i\n", csi_params->power_control_offset);
  LOG_I(NR_PHY, "csi_params->power_control_offset_ss = %i\n", csi_params->power_control_offset_ss);
#endif

  int dataF_offset = slot * frame_parms->samples_per_slot_wCP;
  //*8(max allocation per RB)*2(QPSK))
  int csi_rs_length =  frame_parms->N_RB_DL << 4;
  int16_t mod_csi[frame_parms->symbols_per_slot][csi_rs_length>>1] __attribute__((aligned(16)));
  uint32_t beta = amp;
  nr_csi_info->csi_rs_generated_signal_bits = log2_approx(amp);

  csi_mapping_parms_t phy_csi_parms = get_csi_mapping_parms(csi_params->row,
                                                            csi_params->freq_domain,
                                                            csi_params->symb_l0,
                                                            csi_params->symb_l1);

  // setting the frequency density from its index
  double rho = 0;
  switch (csi_params->freq_density) {
    case 0:
      rho = 0.5;
      break;
    case 1:
      rho = 0.5;
      break;
     case 2:
      rho = 1;
      break;
     case 3:
      rho = 3;
      break;
    default:
      AssertFatal(false, "Invalid frequency density index for CSI\n");
  }

  double alpha = 0;
  if (phy_csi_parms.ports == 1)
    alpha = rho;
  else
    alpha = 2 * rho;

#ifdef NR_CSIRS_DEBUG
  printf(" rho %f, alpha %f\n", rho, alpha);
#endif

  // CDM group size from CDM type index
  int gs = 0;
  switch (csi_params->cdm_type) {
    case 0:
      gs = 1;
      break;
    case 1:
      gs = 2;
      break;
    case 2:
      gs = 4;
      break;
    case 3:
      gs = 8;
      break;
    default:
      AssertFatal(false, "Invalid cdm type index for CSI\n");
  }

  int kprime = phy_csi_parms.kprime;
  int csi_length;
  if (rho < 1) {
    if (csi_params->freq_density == 0) {
      csi_length = (((csi_params->start_rb + csi_params->nr_of_rbs) >> 1) << kprime) << 1;
    } else {
      csi_length = ((((csi_params->start_rb + csi_params->nr_of_rbs) >> 1) << kprime) + 1) << 1;
    }
  } else {
    csi_length = (((uint16_t) rho * (csi_params->start_rb + csi_params->nr_of_rbs)) << kprime) << 1;
  }

#ifdef NR_CSIRS_DEBUG
  printf(" start rb %d, nr of rbs %d, csi length %d\n", csi_params->start_rb, csi_params->nr_of_rbs, csi_length);
#endif

  if (N_cdm_groups)
    *N_cdm_groups = phy_csi_parms.size;
  if (CDM_group_size)
    *CDM_group_size = gs;
  if (k_prime)
    *k_prime = kprime;
  if (l_prime)
    *l_prime = phy_csi_parms.lprime;
  if (N_ports)
    *N_ports = phy_csi_parms.ports;
  if (j_cdm)
    memcpy(j_cdm, phy_csi_parms.j, 16 * sizeof(uint8_t));
  if (k_overline)
    memcpy(k_overline, phy_csi_parms.koverline, 16 * sizeof(uint8_t));
  if (l_overline)
    memcpy(l_overline, phy_csi_parms.loverline, 16 * sizeof(uint8_t));

#ifdef NR_CSIRS_DEBUG
  if (N_ports)
    LOG_I(NR_PHY, "nr_csi_info->N_ports = %d\n", *N_ports);
  if (N_cdm_groups)
    LOG_I(NR_PHY, "nr_csi_info->N_cdm_groups = %d\n", *N_cdm_groups);
  if (CDM_group_size)
    LOG_I(NR_PHY, "nr_csi_info->CDM_group_size = %d\n", *CDM_group_size);
  if (k_prime)
    LOG_I(NR_PHY, "nr_csi_info->kprime = %d\n", *k_prime);
  if (l_prime)
    LOG_I(NR_PHY, "nr_csi_info->lprime = %d\n", *l_prime);
  if (N_cdm_groups) {
    for(int ji = 0; ji < *N_cdm_groups; ji++) {
      LOG_I(NR_PHY,
            "(CDM group %d) j = %d, koverline = %d, loverline = %d\n",
            ji,
            phy_csi_parms.j[ji],
            phy_csi_parms.koverline[ji],
            phy_csi_parms.loverline[ji]);
    }
  }
#endif

  if (csi_params->csi_type == 2) // ZP-CSI
    return;

  // assuming amp is the amplitude of SSB channels
  switch (csi_params->power_control_offset_ss) {
    case 0:
      beta = (amp*ONE_OVER_SQRT2_Q15)>>15;
      break;
    case 1:
      beta = amp;
      break;
    case 2:
      beta = (amp*ONE_OVER_SQRT2_Q15)>>14;
      break;
    case 3:
      beta = amp << 1;
      break;
    default:
      AssertFatal(false, "Invalid SS power offset density index for CSI\n");
  }

  for (int lp = 0; lp <= phy_csi_parms.lprime; lp++) {
    int symb = csi_params->symb_l0;
    const uint32_t *gold =
        nr_gold_csi_rs(frame_parms->N_RB_DL, frame_parms->symbols_per_slot, slot, symb + lp, csi_params->scramb_id);
    nr_modulation(gold, csi_length, DMRS_MOD_ORDER, mod_csi[symb + lp]);
    uint8_t row = csi_params->row;
    if ((row == 5) || (row == 7) || (row == 11) || (row == 13) || (row == 16)) {
      const uint32_t *gold =
          nr_gold_csi_rs(frame_parms->N_RB_DL, frame_parms->symbols_per_slot, slot, symb + 1, csi_params->scramb_id);
      nr_modulation(gold, csi_length, DMRS_MOD_ORDER, mod_csi[symb + 1]);
    }
    if ((row == 14) || (row == 13) || (row == 16) || (row == 17)) {
      symb = csi_params->symb_l1;
      const uint32_t *gold =
          nr_gold_csi_rs(frame_parms->N_RB_DL, frame_parms->symbols_per_slot, slot, symb + lp, csi_params->scramb_id);
      nr_modulation(gold, csi_length, DMRS_MOD_ORDER, mod_csi[symb + lp]);
      if ((row == 13) || (row == 16)) {
        const uint32_t *gold =
            nr_gold_csi_rs(frame_parms->N_RB_DL, frame_parms->symbols_per_slot, slot, symb + 1, csi_params->scramb_id);
        nr_modulation(gold, csi_length, DMRS_MOD_ORDER, mod_csi[symb + 1]);
      }
    }
  }

  uint16_t start_sc = frame_parms->first_carrier_offset;
  // resource mapping according to 38.211 7.4.1.5.3
  for (int n = csi_params->start_rb; n < (csi_params->start_rb + csi_params->nr_of_rbs); n++) {
   if ((csi_params->freq_density > 1) || (csi_params->freq_density == (n % 2))) {  // for freq density 0.5 checks if even or odd RB
    for (int ji = 0; ji < phy_csi_parms.size; ji++) { // loop over CDM groups
      for (int s = 0 ; s < gs; s++)  { // loop over each CDM group size
        int p = s + phy_csi_parms.j[ji] * gs; // port index
        for (int kp = 0; kp <= kprime; kp++) { // loop over frequency resource elements within a group
          // frequency index of current resource element
          int k = (start_sc + (n * NR_NB_SC_PER_RB) + phy_csi_parms.koverline[ji] + kp) % (frame_parms->ofdm_symbol_size);
          // wf according to tables 7.4.5.3-2 to 7.4.5.3-5
          int wf = kp == 0 ? 1 : (-2 * (s % 2) + 1);
          int na = n * alpha;
          int kpn = (rho * phy_csi_parms.koverline[ji]) / NR_NB_SC_PER_RB;
          int mprime = na + kp + kpn; // sequence index
          for (int lp = 0; lp <= phy_csi_parms.lprime; lp++) { // loop over frequency resource elements within a group
            int l = lp + phy_csi_parms.loverline[ji];
            // wt according to tables 7.4.5.3-2 to 7.4.5.3-5
            int wt;
            if (s < 2)
              wt = 1;
            else if (s < 4)
              wt = -2*(lp % 2) + 1;
            else if (s < 6)
              wt = -2 * (lp / 2) + 1;
            else {
              if ((lp == 0) || (lp == 3))
                wt = 1;
              else
                wt = -1;
            }
            int index = ((l * frame_parms->ofdm_symbol_size + k) << 1) + (2 * dataF_offset);
            ((int16_t*)dataF[p])[index] = (beta * wt * wf * mod_csi[l][mprime << 1]) >> 15;
            ((int16_t*)dataF[p])[index + 1] = (beta * wt * wf * mod_csi[l][(mprime << 1) + 1]) >> 15;
#ifdef NR_CSIRS_DEBUG
            printf("l,k (%d,%d)  seq. index %d \t port %d \t (%d,%d)\n",
                   l,
                   k,
                   mprime,
                   p + 3000,
                   ((int16_t*)dataF[p])[index],
                   ((int16_t*)dataF[p])[index + 1]);
#endif
          }
        }
      }
    }
   }
  }
}
