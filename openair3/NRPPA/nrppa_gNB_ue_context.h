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

#include <stdint.h>
#include "tree.h"
#include "ds/byte_array.h"
#include "nrppa_common.h"

#ifndef NRPPA_GNB_UE_CONTEXT_H_
#define NRPPA_GNB_UE_CONTEXT_H_

typedef struct nrppa_gNB_ue_context_s {
  /* Tree related data */
  RB_ENTRY(nrppa_gNB_ue_context_s) entries;
  uint16_t transaction_id;
  uint32_t gNB_ue_ngap_id;
  uint64_t amf_ue_ngap_id;
  byte_array_t routing_id;
} nrppa_gNB_ue_context_t;

void nrppa_store_ue_context(const nrppa_gnb_ue_info_t *info, const uint16_t transaction_id);
nrppa_gNB_ue_context_t *nrppa_get_ue_context(uint16_t transaction_id);
nrppa_gNB_ue_context_t *nrppa_detach_ue_context(uint16_t transaction_id);
void nrppa_free_ue_context(nrppa_gNB_ue_context_t *ue_info);

#endif /* NRPPA_GNB_UE_CONTEXT_H_ */
