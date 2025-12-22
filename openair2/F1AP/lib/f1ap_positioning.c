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

#include "f1ap_positioning.h"

#include "f1ap_lib_common.h"
#include "f1ap_lib_includes.h"
#include "f1ap_messages_types.h"

#include "common/utils/assertions.h"
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"
#include "common/utils/ds/byte_array.h"

/**
 * @brief Encode F1 positioning information request to ASN.1
 */
F1AP_F1AP_PDU_t *encode_positioning_information_req(const f1ap_positioning_information_req_t *msg)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_PositioningInformationExchange;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_InitiatingMessage__value_PR_PositioningInformationRequest;
  F1AP_PositioningInformationRequest_t *out = &tmp->value.choice.PositioningInformationRequest;

  /* mandatory : GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningInformationRequestIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_PositioningInformationRequestIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = msg->gNB_CU_ue_id;

  /* mandatory : GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningInformationRequestIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_PositioningInformationRequestIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = msg->gNB_DU_ue_id;

  return pdu;
}

/**
 * @brief Decode F1 positioning information request
 */
bool decode_positioning_information_req(const F1AP_F1AP_PDU_t *pdu, f1ap_positioning_information_req_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_PositioningInformationRequest_t *in = &pdu->choice.initiatingMessage->value.choice.PositioningInformationRequest;
  F1AP_PositioningInformationRequestIEs_t *ie;

  F1AP_LIB_FIND_IE(F1AP_PositioningInformationRequestIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID,
                   true);
  F1AP_LIB_FIND_IE(F1AP_PositioningInformationRequestIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID,
                   true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningInformationRequestIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningInformationRequestIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_RequestedSRSTransmissionCharacteristics:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld not handled, skipping\n", ie->id);
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 positioning information request deep copy
 */
f1ap_positioning_information_req_t cp_positioning_information_req(const f1ap_positioning_information_req_t *orig)
{
  /* copy all mandatory fields that are not dynamic memory */
  f1ap_positioning_information_req_t cp = {
      .gNB_CU_ue_id = orig->gNB_CU_ue_id,
      .gNB_DU_ue_id = orig->gNB_DU_ue_id,
  };

  return cp;
}

/**
 * @brief F1 positioning information request equality check
 */
bool eq_positioning_information_req(const f1ap_positioning_information_req_t *a, const f1ap_positioning_information_req_t *b)
{
  _F1_EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _F1_EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);

  return true;
}

/**
 * @brief Free Allocated F1 positioning information request
 */
void free_positioning_information_req(f1ap_positioning_information_req_t *msg)
{
  // nothing to free
}
