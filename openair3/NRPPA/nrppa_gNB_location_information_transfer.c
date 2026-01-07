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

#include "intertask_interface.h"
#include "nrppa_common.h"
#include "nrppa_gNB_location_information_transfer.h"
#include "nrppa_gNB_ue_context.h"
#include "nrppa_messages_types.h"

void free_trp_information_request(nrppa_trp_information_req_t *msg)
{
  if (msg->trp_information_type_list.trp_information_type_item) {
    free(msg->trp_information_type_list.trp_information_type_item);
  }
}

int nrppa_gNB_handle_trp_information_request(nrppa_gnb_ue_info_t *nrppa_msg_info, const NRPPA_NRPPA_PDU_t *pdu)
{
  LOG_I(NRPPA, "Processing Received TRP Information Request \n");
  DevAssert(pdu != NULL);
  DevAssert(nrppa_msg_info != NULL);

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NRPPA_NRPPA_PDU, pdu);
  }

  // Forward request to RRC
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, 0, NRPPA_TRP_INFORMATION_REQ);
  nrppa_trp_information_req_t *req = &NRPPA_TRP_INFORMATION_REQ(msg);

  // Processing Received TRPInformationRequest
  NRPPA_TRPInformationRequest_t *container = NULL;
  NRPPA_TRPInformationRequest_IEs_t *ie = NULL;

  // IE 9.2.3 Message type : mandatory
  container = &pdu->choice.initiatingMessage->value.choice.TRPInformationRequest;

  // IE 9.2.4 nrppatransactionID : mandatory
  req->transaction_id = pdu->choice.initiatingMessage->nrppatransactionID;

  // IE TRP List : optional
  NRPPA_FIND_PROTOCOLIE_BY_ID(NRPPA_TRPInformationRequest_IEs_t, ie, container, NRPPA_ProtocolIE_ID_id_TRPList, false);

  if (ie == NULL) {
    req->has_trp_list = false;
  } else {
    LOG_W(NRPPA, "TRPInformationRequest IE TRP List : not handled\n");
  }

  // IE TRP Information Type List: mandatory
  // not implemented in oai-lmf
  NRPPA_FIND_PROTOCOLIE_BY_ID(NRPPA_TRPInformationRequest_IEs_t,
                              ie,
                              container,
                              NRPPA_ProtocolIE_ID_id_TRPInformationTypeList,
                              false);

  if (ie == NULL) {
    LOG_W(NRPPA, "TRPInformationRequest IE TRP Information Type List is mandatory but not handled\n");
  } else {
    uint8_t trp_info_list_len = ie->value.choice.TRPInformationTypeList.list.count;
    AssertError(trp_info_list_len > 0, return false, "at least 1 TRP Information Type must be present");
    nrppa_trp_information_type_list_t *trp_info_list = &req->trp_information_type_list;
    trp_info_list->trp_information_type_list_length = trp_info_list_len;
    trp_info_list->trp_information_type_item = calloc_or_fail(trp_info_list_len, sizeof(*trp_info_list->trp_information_type_item));
    for (int i = 0; i < trp_info_list_len; i++) {
      trp_info_list->trp_information_type_item[i] = *ie->value.choice.TRPInformationTypeList.list.array[i];
    }
  }

  nrppa_store_ue_context(nrppa_msg_info, req->transaction_id);

  LOG_I(NRPPA, "Forwarding to RRC TRPInformationRequest transaction_id=%d\n", req->transaction_id);
  itti_send_msg_to_task(TASK_RRC_GNB, 0, msg);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NRPPA_NRPPA_PDU, &pdu);
  return 0;
}
