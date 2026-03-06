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

#ifndef NRPPA_COMMON_H_
#define NRPPA_COMMON_H_

#include "oai_asn1.h"
#include "common/utils/LOG/log.h"
#include "ds/byte_array.h"
#include "nrppa_includes.h"

#define NRPPA_FIND_PROTOCOLIE_BY_ID(IE_TYPE, ie, container, IE_ID, mandatory)                                                  \
  do {                                                                                                                         \
    IE_TYPE **ptr;                                                                                                             \
    ie = NULL;                                                                                                                 \
    for (ptr = container->protocolIEs.list.array; ptr < &container->protocolIEs.list.array[container->protocolIEs.list.count]; \
         ptr++) {                                                                                                              \
      if ((*ptr)->id == IE_ID) {                                                                                               \
        ie = *ptr;                                                                                                             \
        break;                                                                                                                 \
      }                                                                                                                        \
    }                                                                                                                          \
    if (ie == NULL) {                                                                                                          \
      if (mandatory) {                                                                                                         \
        AssertFatal(false, "NRPPA_FIND_PROTOCOLIE_BY_ID ie is NULL (searching for ie: %ld)\n", IE_ID);                         \
      } else {                                                                                                                 \
        LOG_I(NRPPA, "NRPPA_FIND_PROTOCOLIE_BY_ID ie is NULL (searching for ie: %ld)\n", IE_ID);                               \
      }                                                                                                                        \
    }                                                                                                                          \
  } while (0);                                                                                                                 \
  if (mandatory && !ie)                                                                                                        \
  return -1

// gnb and ue related info in NRPPA message
typedef struct nrppa_gnb_ue_info_s {
  instance_t instance;
  int32_t gNB_ue_ngap_id;
  int64_t amf_ue_ngap_id;
  byte_array_t routing_id;
} nrppa_gnb_ue_info_t;

typedef int (*nrppa_message_decoded_callback)(nrppa_gnb_ue_info_t *nrppa_msg_info, const NRPPA_NRPPA_PDU_t *pdu);

#endif /* NRPPA_COMMON_H_ */
