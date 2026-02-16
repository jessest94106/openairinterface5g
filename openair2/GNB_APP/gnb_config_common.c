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

#include "openair2/GNB_APP/gnb_config_common.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "RRC_nr_paramsvalues.h"
#include "gnb_paramdef.h"
#include "sctp_default_values.h"

uint16_t set_snssai_config(nssai_t *nssai, const int max_num_ssi, uint8_t k, uint8_t l)
{
  char snssaistr[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(snssaistr, sizeof(snssaistr), "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, k, GNB_CONFIG_STRING_PLMN_LIST, l);
  GET_PARAMS_LIST(SNSSAIParamList,
                  SNSSAIParams,
                  GNBSNSSAIPARAMS_DESC,
                  GNB_CONFIG_STRING_SNSSAI_LIST,
                  snssaistr,
                  SNSSAIPARAMS_CHECK);
  uint16_t num_ssi = SNSSAIParamList.numelt;
  AssertFatal(num_ssi < max_num_ssi, "S-NSSAI size %d exceeds the max array size %d", num_ssi, max_num_ssi);
  for (int s = 0; s < num_ssi; ++s) {
    nssai[s].sst = *SNSSAIParamList.paramarray[s][GNB_SLICE_SERVICE_TYPE_IDX].uptr;
    // SD is optional
    // 0xffffff is "no SD", see 23.003 Sec 28.4.2
    nssai[s].sd = *SNSSAIParamList.paramarray[s][GNB_SLICE_DIFFERENTIATOR_IDX].uptr;
    AssertFatal(nssai[s].sd <= 0xffffff, "SD cannot be bigger than 0xffffff, but is %d\n", nssai[s].sd);
  }
  return num_ssi;
}

uint8_t set_plmn_config(plmn_id_t *p, uint8_t idx)
{
  char gnbpath[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(gnbpath, sizeof(gnbpath), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, idx);
  GET_PARAMS_LIST(PLMNParamList, PLMNParams, GNBPLMNPARAMS_DESC, GNB_CONFIG_STRING_PLMN_LIST, gnbpath, PLMNPARAMS_CHECK);
  uint8_t num_plmn = PLMNParamList.numelt;
  AssertFatal(num_plmn >= 1 && num_plmn <= 6, "The number of PLMN IDs must be in [1,6], but is %d\n", num_plmn);
  for (int l = 0; l < num_plmn; ++l) {
    plmn_id_t *plmn = &p[l];
    plmn->mcc = *PLMNParamList.paramarray[l][GNB_MOBILE_COUNTRY_CODE_IDX].uptr;
    plmn->mnc = *PLMNParamList.paramarray[l][GNB_MOBILE_NETWORK_CODE_IDX].uptr;
    plmn->mnc_digit_length = *PLMNParamList.paramarray[l][GNB_MNC_DIGIT_LENGTH].u8ptr;
    AssertFatal((plmn->mnc_digit_length == 2) || (plmn->mnc_digit_length == 3), "BAD MNC DIGIT LENGTH %d", plmn->mnc_digit_length);
  }
  return num_plmn;
}
