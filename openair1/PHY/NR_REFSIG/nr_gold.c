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

#include "nr_refsig.h"

void nr_init_prs(PHY_VARS_gNB* gNB)
{
  unsigned int x1 = 0, x2 = 0;
  uint16_t Nid;

  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  gNB->nr_gold_prs = (uint32_t ****)malloc16(gNB->prs_vars.NumPRSResources*sizeof(uint32_t ***));
  uint32_t ****prs = gNB->nr_gold_prs;
  AssertFatal(prs!=NULL, "NR init: positioning reference signal malloc failed\n");
  for (int rsc=0; rsc < gNB->prs_vars.NumPRSResources; rsc++) {
    prs[rsc] = (uint32_t ***)malloc16(fp->slots_per_frame*sizeof(uint32_t **));
    AssertFatal(prs[rsc]!=NULL, "NR init: positioning reference signal for rsc %d - malloc failed\n", rsc);

    for (int slot=0; slot<fp->slots_per_frame; slot++) {
      prs[rsc][slot] = (uint32_t **)malloc16(fp->symbols_per_slot*sizeof(uint32_t *));
      AssertFatal(prs[rsc][slot]!=NULL, "NR init: positioning reference signal for slot %d - malloc failed\n", slot);

      for (int symb=0; symb<fp->symbols_per_slot; symb++) {
        prs[rsc][slot][symb] = (uint32_t *)malloc16(NR_MAX_PRS_INIT_LENGTH_DWORD*sizeof(uint32_t));
        AssertFatal(prs[rsc][slot][symb]!=NULL, "NR init: positioning reference signal for rsc %d slot %d symbol %d - malloc failed\n", rsc, slot, symb);
      }
    }
  }

  uint8_t reset;
  uint8_t slotNum, symNum, rsc_id;

  for (rsc_id = 0; rsc_id < gNB->prs_vars.NumPRSResources; rsc_id++) {
    Nid = gNB->prs_vars.prs_cfg[rsc_id].NPRSID; // seed value
    LOG_I(PHY, "Initiaized NR-PRS sequence with PRS_ID %3d for resource %d\n", Nid, rsc_id);
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
          gNB->nr_gold_prs[rsc_id][slotNum][symNum][n] = lte_gold_generic(&x1, &x2, reset);      
          reset = 0;
          //printf("%d \n",gNB->nr_gold_prs[slotNum][symNum][n]); 
        }
      }
    }
  }
}
