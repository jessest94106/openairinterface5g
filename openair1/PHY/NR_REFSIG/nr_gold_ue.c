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

#include "refsig_defs_ue.h"
#include "openair1/PHY/LTE_TRANSPORT/transport_proto.h" // for lte_gold_generic()

void nr_init_pusch_dmrs(PHY_VARS_NR_UE* ue, uint16_t N_n_scid, uint8_t n_scid)
{
  NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  uint32_t ****pusch_dmrs = ue->nr_gold_pusch_dmrs;
  int pusch_dmrs_init_length = ((fp->N_RB_UL * 12) >> 5) + 1;
  for (int slot = 0; slot < fp->slots_per_frame; slot++) {
    for (int symb = 0; symb < fp->symbols_per_slot; symb++) {
      int reset = 1;
      uint32_t x1 = 0;
      uint64_t t_x2 = ((1UL << 17) * (fp->symbols_per_slot*slot + symb + 1) * ((N_n_scid << 1) + 1) + ((N_n_scid << 1) + n_scid));
      uint32_t x2 = t_x2 % (1U << 31);
      LOG_D(PHY,"DMRS slot %d, symb %d, N_n_scid %d, n_scid %d, x2 %x\n", slot, symb, N_n_scid, n_scid, x2);
      for (int n = 0; n < pusch_dmrs_init_length; n++) {
        pusch_dmrs[slot][symb][n_scid][n] = lte_gold_generic(&x1, &x2, reset);
        reset = 0;
      }
    }
  }
}

void init_nr_gold_prs(PHY_VARS_NR_UE* ue)
{
  unsigned int x1 = 0, x2 = 0;
  uint16_t Nid;

  NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  uint8_t reset;
  uint8_t slotNum, symNum, gnb, rsc;
  
  for(gnb = 0; gnb < ue->prs_active_gNBs; gnb++) {
    for(rsc = 0; rsc < ue->prs_vars[gnb]->NumPRSResources; rsc++) {
      Nid = ue->prs_vars[gnb]->prs_resource[rsc].prs_cfg.NPRSID; // seed value
      LOG_I(PHY,"Initialised NR-PRS sequence with PRS_ID %3d for resource %d\n",Nid, rsc);
      for (slotNum = 0; slotNum < fp->slots_per_frame; slotNum++) {
        for (symNum = 0; symNum < fp->symbols_per_slot ; symNum++) {
          reset = 1;
          // initial x2 for prs as ts138.211
          uint32_t c_init1, c_init2, c_init3;
          uint32_t pow22=1<<22;
          uint32_t pow10=1<<10;
          c_init1 = pow22*ceil(Nid/1024);
          c_init2 = pow10*(slotNum+symNum+1)*(2*(Nid%1024)+1);
          c_init3 = Nid%1024;
          x2 = c_init1 + c_init2 + c_init3;

          for (uint8_t n=0; n<NR_MAX_PRS_INIT_LENGTH_DWORD; n++) {
            ue->nr_gold_prs[gnb][rsc][slotNum][symNum][n] = lte_gold_generic(&x1, &x2, reset);      
            reset = 0;
            //printf("%d \n",gNB->nr_gold_prs[slotNum][symNum][n]); 
	    
          }
        }
      }
    } // for rsc
  } // for gnb
}

void sl_init_psbch_dmrs_gold_sequences(PHY_VARS_NR_UE *UE)
{
  unsigned int x1, x2;
  uint16_t slss_id;
  uint8_t reset;

  for (slss_id = 0; slss_id < SL_NR_NUM_SLSS_IDs; slss_id++) {
    reset = 1;
    x2 = slss_id;

#ifdef SL_DEBUG_INIT
    printf("\nPSBCH DMRS GOLD SEQ for SLSSID :%d  :\n", slss_id);
#endif

    for (uint8_t n = 0; n < SL_NR_NUM_PSBCH_DMRS_RE_DWORD; n++) {
      UE->SL_UE_PHY_PARAMS.init_params.psbch_dmrs_gold_sequences[slss_id][n] = lte_gold_generic(&x1, &x2, reset);
      reset = 0;

#ifdef SL_DEBUG_INIT_DATA
      printf("%x\n", SL_UE_INIT_PARAMS.sl_psbch_dmrs_gold_sequences[slss_id][n]);
#endif
    }
  }
}
