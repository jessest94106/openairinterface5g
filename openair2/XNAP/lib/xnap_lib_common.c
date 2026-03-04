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

#include "xnap_lib_common.h"
#include "common/utils/assertions.h"
#include "common/utils/utils.h"

bool eq_xnap_plmn(const plmn_id_t *a, const plmn_id_t *b)
{
  _EQ_CHECK_INT(a->mcc, b->mcc);
  _EQ_CHECK_INT(a->mnc, b->mnc);
  _EQ_CHECK_INT(a->mnc_digit_length, b->mnc_digit_length);
  return true;
}

bool eq_xnap_snssai(const nssai_t *a, const nssai_t *b)
{
  _EQ_CHECK_INT(a->sst, b->sst);
  _EQ_CHECK_UINT32(a->sd, b->sd);
  return true;
}

bool eq_xnap_plmn_support(const xnap_plmn_support_t *a, const xnap_plmn_support_t *b)
{
  if (!eq_xnap_plmn(&a->plmn, &b->plmn)) return false;
  _EQ_CHECK_INT(a->num_nssai, b->num_nssai);
  for (int i = 0; i < a->num_nssai; i++) {
    if (!eq_xnap_snssai(&a->nssai[i], &b->nssai[i]))
      return false;
  }
  return true;
}

bool eq_xnap_tai_support(const xnap_tai_support_t *a, const xnap_tai_support_t *b)
{
  _EQ_CHECK_UINT32(a->tac, b->tac);
  _EQ_CHECK_INT(a->num_plmn, b->num_plmn);
  for (int i = 0; i < a->num_plmn; i++) {
    if (!eq_xnap_plmn_support(&a->plmn_support[i], &b->plmn_support[i]))
      return false;
  }
  return true;
}
