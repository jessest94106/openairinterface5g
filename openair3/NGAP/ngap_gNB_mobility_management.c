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
#include <stdio.h>
#include <stdlib.h>
#include "ngap_common.h"
#include "ngap_msg_includes.h"
#include "ngap_gNB_defs.h"
#include "ngap_gNB_ue_context.h"
#include "oai_asn1.h"
#include "ngap_gNB_management_procedures.h"
#include "ngap_gNB_encoder.h"
#include "ngap_gNB_itti_messaging.h"

/** @brief UE Mobility Management: encode Handover Required
 *         (9.2.3.1 of 3GPP TS 38.413) NG-RAN node → AMF */
NGAP_NGAP_PDU_t *encode_ng_handover_required(const ngap_handover_required_t *msg)
{
  NGAP_NGAP_PDU_t *pdu = malloc_or_fail(sizeof(*pdu));

  /* Prepare the NGAP message to encode */
  pdu->present = NGAP_NGAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, head);
  head->procedureCode = NGAP_ProcedureCode_id_HandoverPreparation;
  head->criticality = NGAP_Criticality_reject;
  head->value.present = NGAP_InitiatingMessage__value_PR_HandoverRequired;
  NGAP_HandoverRequired_t *out = &head->value.choice.HandoverRequired;

  // AMF UE NGAP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverRequiredIEs_t, ie1);
  ie1->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
  ie1->criticality = NGAP_Criticality_reject;
  ie1->value.present = NGAP_HandoverRequiredIEs__value_PR_AMF_UE_NGAP_ID;
  asn_uint642INTEGER(&ie1->value.choice.AMF_UE_NGAP_ID, msg->amf_ue_ngap_id);

  // RAN UE NGAP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverRequiredIEs_t, ie2);
  ie2->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
  ie2->criticality = NGAP_Criticality_reject;
  ie2->value.present = NGAP_HandoverRequiredIEs__value_PR_RAN_UE_NGAP_ID;
  ie2->value.choice.RAN_UE_NGAP_ID = msg->gNB_ue_ngap_id;

  // Handover Type (M)
  asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverRequiredIEs_t, ie3);
  ie3->id = NGAP_ProtocolIE_ID_id_HandoverType;
  ie3->criticality = NGAP_Criticality_reject;
  ie3->value.present = NGAP_HandoverRequiredIEs__value_PR_HandoverType;
  ie3->value.choice.HandoverType = msg->handoverType;

  // Cause (M)
  asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverRequiredIEs_t, ie4);
  ie4->id = NGAP_ProtocolIE_ID_id_Cause;
  ie4->criticality = NGAP_Criticality_ignore;
  ie4->value.present = NGAP_HandoverRequiredIEs__value_PR_Cause;
  encode_ngap_cause(&ie4->value.choice.Cause, &msg->cause);

  // Target ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverRequiredIEs_t, ie5);
  encode_ngap_target_id(ie5, &msg->target_gnb_id);

  // PDU Session Resource List (M)
  asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverRequiredIEs_t, ie6);
  ie6->id = NGAP_ProtocolIE_ID_id_PDUSessionResourceListHORqd;
  ie6->criticality = NGAP_Criticality_reject;
  ie6->value.present = NGAP_HandoverRequiredIEs__value_PR_PDUSessionResourceListHORqd;
  for (int i = 0; i < msg->nb_of_pdusessions; ++i) {
    asn1cSequenceAdd(ie6->value.choice.PDUSessionResourceListHORqd.list, NGAP_PDUSessionResourceItemHORqd_t, hoRequiredPduSession);
    // PDU Session ID (M)
    hoRequiredPduSession->pDUSessionID = msg->pdusessions[i].pdusession_id;
    // Handover Required Transfer (M)
    NGAP_HandoverRequiredTransfer_t hoRequiredTransfer = {0};
    uint8_t ho_req_transfer_transparent_container_buffer[128] = {0};
    if (LOG_DEBUGFLAG(DEBUG_ASN1))
      xer_fprint(stdout, &asn_DEF_NGAP_HandoverRequiredTransfer, &hoRequiredTransfer);
    asn_enc_rval_t enc_rval = aper_encode_to_buffer(&asn_DEF_NGAP_HandoverRequiredTransfer,
                                                    NULL,
                                                    &hoRequiredTransfer,
                                                    ho_req_transfer_transparent_container_buffer,
                                                    128);
    AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
    hoRequiredPduSession->handoverRequiredTransfer.buf = CALLOC(1, (enc_rval.encoded + 7) / 8);
    memcpy(hoRequiredPduSession->handoverRequiredTransfer.buf,
           ho_req_transfer_transparent_container_buffer,
           (enc_rval.encoded + 7) / 8);
    hoRequiredPduSession->handoverRequiredTransfer.size = (enc_rval.encoded + 7) / 8;

    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_HandoverRequiredTransfer, &hoRequiredTransfer);
  }

  // Source NG-RAN Node to Target NG-RAN Node Transparent Container (M)
  asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverRequiredIEs_t, ie);
  ie->id = NGAP_ProtocolIE_ID_id_SourceToTarget_TransparentContainer;
  ie->criticality = NGAP_Criticality_reject;
  ie->value.present = NGAP_HandoverRequiredIEs__value_PR_SourceToTarget_TransparentContainer;

  NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer_t *source2target = calloc_or_fail(1, sizeof(*source2target));

  // RRC Container (M) (HandoverPreparationInformation)
  source2target->rRCContainer.size = msg->source2target->handoverInfo.len;
  source2target->rRCContainer.buf = malloc_or_fail(msg->source2target->handoverInfo.len);
  memcpy(source2target->rRCContainer.buf, msg->source2target->handoverInfo.buf, source2target->rRCContainer.size);

  // PDU Session Resource Information List (O)
  asn1cCalloc(source2target->pDUSessionResourceInformationList, pduSessionList);
  for (uint8_t i = 0; i < msg->nb_of_pdusessions; ++i) {
    const pdusession_resource_t *pduSession = &msg->pdusessions[i];
    NGAP_DEBUG("Handover Required: preparing PDU Session Resource Information List for PDU Session ID %d\n",
               pduSession->pdusession_id);
    asn1cSequenceAdd(pduSessionList->list, NGAP_PDUSessionResourceInformationItem_t, item);
    // PDU Session ID (M)
    item->pDUSessionID = msg->source2target->pdu_session_resource[i].pdusession_id;
    // QoS Flow Information List (M)
    for (int q = 0; q < msg->source2target->pdu_session_resource[i].nb_of_qos_flow; ++q) {
      asn1cSequenceAdd(item->qosFlowInformationList.list, NGAP_QosFlowInformationItem_t, qosFlowInfo);
      qosFlowInfo->qosFlowIdentifier = msg->source2target->pdu_session_resource[i].qos_flow_info[q].qfi;
    }
  }

  // Target Cell ID (NG-RAN CGI) (M)
  source2target->targetCell_ID.present = NGAP_NGRAN_CGI_PR_nR_CGI;
  asn1cCalloc(source2target->targetCell_ID.choice.nR_CGI, tNrCGI);
  encode_ngap_nr_cgi(tNrCGI, &msg->target_gnb_id.plmn_identity, msg->source2target->targetCellId.nrCellIdentity);

  // UE history Information (M)
  asn1cSequenceAdd(source2target->uEHistoryInformation.list, NGAP_LastVisitedCellItem_t, lastVisitedCell);
  lastVisitedCell->iE_Extensions = NULL;
  // Last Visited Cell Information (M)
  lastVisitedCell->lastVisitedCellInformation.present = NGAP_LastVisitedCellInformation_PR_nGRANCell;
  // CHOICE (M): NG-RAN Cell
  asn1cCalloc(lastVisitedCell->lastVisitedCellInformation.choice.nGRANCell, lastVisitedNR);
  // Cell Type (M)
  lastVisitedNR->cellType.cellSize = msg->source2target->ue_history_info.type;
  // Global Cell ID (M)
  lastVisitedNR->globalCellID.present = NGAP_NGRAN_CGI_PR_nR_CGI;
  asn1cCalloc(lastVisitedNR->globalCellID.choice.nR_CGI, lastVisitedNrCGI);
  cell_id_t *cell = &msg->source2target->ue_history_info.id;
  encode_ngap_nr_cgi(lastVisitedNrCGI, &cell->plmn_identity, cell->nrCellIdentity);
  // HO Cause Value (O)
  if (msg->source2target->ue_history_info.cause) {
    asn1cCalloc(lastVisitedNR->hOCauseValue, lastVisitedCause);
    encode_ngap_cause(lastVisitedCause, msg->source2target->ue_history_info.cause);
  }
  // Time UE Stayed in Cell (M)
  lastVisitedNR->timeUEStayedInCell = msg->source2target->ue_history_info.time_in_cell;

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer, source2target);

  uint8_t source_to_target_transparent_container_buf[16384] = {0};
  asn_enc_rval_t enc_rval = aper_encode_to_buffer(&asn_DEF_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer,
                                                  NULL,
                                                  (void *)source2target,
                                                  (void *)&source_to_target_transparent_container_buf,
                                                  16384);
  ASN_STRUCT_FREE(asn_DEF_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer, source2target);
  if (enc_rval.encoded < 0) {
    AssertFatal(enc_rval.encoded > 0,
                "HO LOG: Source to Transparent ASN1 message encoding failed (%s, %lu)!\n",
                enc_rval.failed_type->name,
                enc_rval.encoded);
    return NULL;
  }

  int total_bytes = (enc_rval.encoded + 7) / 8;
  int ret = OCTET_STRING_fromBuf(&ie->value.choice.SourceToTarget_TransparentContainer,
                                 (const char *)&source_to_target_transparent_container_buf,
                                 total_bytes);
  if (ret != 0) {
    LOG_E(NR_RRC, "HO LOG: Can not perform OCTET_STRING_fromBuf for the SourceToTarget_TransparentContainer");
    return NULL;
  }

  return pdu;
}

NGAP_NGAP_PDU_t *encode_ng_handover_failure(const ngap_handover_failure_t *msg)
{
  NGAP_NGAP_PDU_t *pdu = malloc_or_fail(sizeof(*pdu));

  /* Prepare the NGAP message to encode */
  pdu->present = NGAP_NGAP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu->choice.unsuccessfulOutcome, head);
  head->procedureCode = NGAP_ProcedureCode_id_HandoverResourceAllocation;
  head->criticality = NGAP_Criticality_reject;
  head->value.present = NGAP_UnsuccessfulOutcome__value_PR_HandoverFailure;
  NGAP_HandoverFailure_t *out = &head->value.choice.HandoverFailure;

  // AMF_UE_NGAP_ID (M)
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverFailureIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_HandoverFailureIEs__value_PR_AMF_UE_NGAP_ID;
    asn_uint642INTEGER(&ie->value.choice.AMF_UE_NGAP_ID, msg->amf_ue_ngap_id);
  }

  // Cause (M)
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_HandoverFailureIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_Cause;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_HandoverFailureIEs__value_PR_Cause;
    encode_ngap_cause(&ie->value.choice.Cause, &msg->cause);
  }

  return pdu;
}
