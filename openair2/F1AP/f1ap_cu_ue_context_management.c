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

/*! \file f1ap_cu_ue_context_management.c
 * \brief F1AP UE Context Management, CU side
 * \author EURECOM/NTUST
 * \date 2018
 * \version 0.1
 * \company Eurecom
 * \email: navid.nikaein@eurecom.fr, bing-kai.hong@eurecom.fr
 * \note
 * \warning
 */

#include "f1ap_common.h"
#include "f1ap_encoder.h"
#include "f1ap_itti_messaging.h"
#include "f1ap_cu_ue_context_management.h"
#include "lib/f1ap_ue_context.h"
#include <string.h>

#include "rrc_extern.h"
#include "openair2/RRC/NR/rrc_gNB_NGAP.h"

#ifdef E2_AGENT
#include "openair2/RRC/NR/rrc_gNB_UE_context.h"
#include "openair2/E2AP/RAN_FUNCTION/O-RAN/ran_func_rc_extern.h"
#endif

int CU_send_UE_CONTEXT_SETUP_REQUEST(sctp_assoc_t assoc_id, const f1ap_ue_context_setup_req_t *req)
{
  F1AP_F1AP_PDU_t *pdu = encode_ue_context_setup_req(req);

  uint8_t *buffer = NULL;
  uint32_t len = 0;
  if (f1ap_encode_pdu(pdu, &buffer, &len) < 0) {
    LOG_E(F1AP, "Failed to encode F1 UE CONTEXT SETUP REQUEST\n");
    return -1;
  }

  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);

#ifdef E2_AGENT
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(RC.nrrrc[0], req->gNB_CU_ue_id);
  signal_ue_id(&ue_context_p->ue_context, F1_NETWORK_INTERFACE_TYPE, 0);
#endif

  return 0;
}

int CU_handle_UE_CONTEXT_SETUP_RESPONSE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  MessageDef                       *msg_p;
  F1AP_UEContextSetupResponse_t    *container;
  F1AP_UEContextSetupResponseIEs_t *ie;
  DevAssert(pdu);
  msg_p = itti_alloc_new_message(TASK_DU_F1, 0,  F1AP_UE_CONTEXT_SETUP_RESP);
  msg_p->ittiMsgHeader.originInstance = assoc_id;
  f1ap_ue_context_setup_t *f1ap_ue_context_setup_resp = &F1AP_UE_CONTEXT_SETUP_RESP(msg_p);
  container = &pdu->choice.successfulOutcome->value.choice.UEContextSetupResponse;
  int i;
  /* GNB_CU_UE_F1AP_ID */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  f1ap_ue_context_setup_resp->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
  LOG_D(F1AP, "f1ap_ue_context_setup_resp->gNB_CU_ue_id is: %d \n", f1ap_ue_context_setup_resp->gNB_CU_ue_id);
  /* GNB_DU_UE_F1AP_ID */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);
  f1ap_ue_context_setup_resp->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
  LOG_D(F1AP, "f1ap_ue_context_setup_resp->gNB_DU_ue_id is: %d \n", f1ap_ue_context_setup_resp->gNB_DU_ue_id);
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container, F1AP_ProtocolIE_ID_id_C_RNTI, false);
  if (ie) {
    f1ap_ue_context_setup_resp->crnti = calloc(1, sizeof(uint16_t));
    *f1ap_ue_context_setup_resp->crnti = ie->value.choice.C_RNTI;
  }

  // DUtoCURRCInformation
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_DUtoCURRCInformation, true);

  if (ie == NULL) {
    LOG_E(F1AP,"%s %d: ie is a NULL pointer \n",__FILE__,__LINE__);
    return -1;
  }

  f1ap_ue_context_setup_resp->du_to_cu_rrc_information = (du_to_cu_rrc_information_t *)calloc(1,sizeof(du_to_cu_rrc_information_t));
  f1ap_ue_context_setup_resp->du_to_cu_rrc_information->cellGroupConfig = (uint8_t *)calloc(1,ie->value.choice.DUtoCURRCInformation.cellGroupConfig.size);
  memcpy(f1ap_ue_context_setup_resp->du_to_cu_rrc_information->cellGroupConfig, ie->value.choice.DUtoCURRCInformation.cellGroupConfig.buf, ie->value.choice.DUtoCURRCInformation.cellGroupConfig.size);
  f1ap_ue_context_setup_resp->du_to_cu_rrc_information->cellGroupConfig_length = ie->value.choice.DUtoCURRCInformation.cellGroupConfig.size;
  // DRBs_Setup_List
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_DRBs_Setup_List, false);

  if(ie!=NULL) {
    f1ap_ue_context_setup_resp->drbs_to_be_setup_length = ie->value.choice.DRBs_Setup_List.list.count;
    f1ap_ue_context_setup_resp->drbs_to_be_setup = calloc(f1ap_ue_context_setup_resp->drbs_to_be_setup_length,
        sizeof(f1ap_drb_to_be_setup_t));
    AssertFatal(f1ap_ue_context_setup_resp->drbs_to_be_setup,
                "could not allocate memory for f1ap_ue_context_setup_resp->drbs_setup\n");

    for (i = 0; i < f1ap_ue_context_setup_resp->drbs_to_be_setup_length; ++i) {
      f1ap_drb_to_be_setup_t *drb_p = &f1ap_ue_context_setup_resp->drbs_to_be_setup[i];
      F1AP_DRBs_Setup_Item_t *drbs_setup_item_p;
      drbs_setup_item_p = &((F1AP_DRBs_Setup_ItemIEs_t *)ie->value.choice.DRBs_Setup_List.list.array[i])->value.choice.DRBs_Setup_Item;
      drb_p->drb_id = drbs_setup_item_p->dRBID;
      // TODO in the following, assume only one UP UL TNL is present.
      // this matches/assumes OAI CU/DU implementation, can be up to 2!
      drb_p->up_dl_tnl_length = 1;
      AssertFatal(drbs_setup_item_p->dLUPTNLInformation_ToBeSetup_List.list.count > 0,
                  "no DL UP TNL Information in DRBs to be Setup list\n");
      F1AP_DLUPTNLInformation_ToBeSetup_Item_t *dl_up_tnl_info_p = (F1AP_DLUPTNLInformation_ToBeSetup_Item_t *)drbs_setup_item_p->dLUPTNLInformation_ToBeSetup_List.list.array[0];
      F1AP_GTPTunnel_t *dl_up_tnl0 = dl_up_tnl_info_p->dLUPTNLInformation.choice.gTPTunnel;
      BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&dl_up_tnl0->transportLayerAddress, drb_p->up_dl_tnl[0].tl_address);
      OCTET_STRING_TO_UINT32(&dl_up_tnl0->gTP_TEID, drb_p->up_dl_tnl[0].teid);
    }
  }

  // SRBs_FailedToBeSetup_List
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_SRBs_FailedToBeSetup_List, false);

  if(ie!=NULL) {
    f1ap_ue_context_setup_resp->srbs_failed_to_be_setup_length = ie->value.choice.SRBs_FailedToBeSetup_List.list.count;
    f1ap_ue_context_setup_resp->srbs_failed_to_be_setup = calloc(f1ap_ue_context_setup_resp->srbs_failed_to_be_setup_length,
        sizeof(f1ap_rb_failed_to_be_setup_t));
    AssertFatal(f1ap_ue_context_setup_resp->srbs_failed_to_be_setup,
                "could not allocate memory for f1ap_ue_context_setup_resp->srbs_failed_to_be_setup\n");

    for (i = 0; i < f1ap_ue_context_setup_resp->srbs_failed_to_be_setup_length; ++i) {
      f1ap_rb_failed_to_be_setup_t *srb_p = &f1ap_ue_context_setup_resp->srbs_failed_to_be_setup[i];
      srb_p->rb_id = ((F1AP_SRBs_FailedToBeSetup_Item_t *)ie->value.choice.SRBs_FailedToBeSetup_List.list.array[i])->sRBID;
    }
  }

  // DRBs_FailedToBeSetup_List
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_DRBs_FailedToBeSetup_List, false);

  if(ie!=NULL) {
    f1ap_ue_context_setup_resp->drbs_failed_to_be_setup_length = ie->value.choice.DRBs_FailedToBeSetup_List.list.count;
    f1ap_ue_context_setup_resp->drbs_failed_to_be_setup = calloc(f1ap_ue_context_setup_resp->drbs_failed_to_be_setup_length,
        sizeof(f1ap_rb_failed_to_be_setup_t));
    AssertFatal(f1ap_ue_context_setup_resp->drbs_failed_to_be_setup,
                "could not allocate memory for f1ap_ue_context_setup_resp->drbs_failed_to_be_setup\n");

    for (i = 0; i < f1ap_ue_context_setup_resp->drbs_failed_to_be_setup_length; ++i) {
      f1ap_rb_failed_to_be_setup_t *drb_p = &f1ap_ue_context_setup_resp->drbs_failed_to_be_setup[i];
      drb_p->rb_id = ((F1AP_DRBs_FailedToBeSetup_Item_t *)ie->value.choice.DRBs_FailedToBeSetup_List.list.array[i])->dRBID;
    }
  }

  // SCell_FailedtoSetup_List
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_SCell_FailedtoSetup_List, false);

  if(ie!=NULL) {
    LOG_E (F1AP, "Not supporting handling of SCell_FailedtoSetup_List \n");
  }

  // SRBs_Setup_List
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextSetupResponseIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_SRBs_Setup_List, false);

  if(ie!=NULL) {
    f1ap_ue_context_setup_resp->srbs_to_be_setup_length = ie->value.choice.SRBs_Setup_List.list.count;
    f1ap_ue_context_setup_resp->srbs_to_be_setup = calloc(f1ap_ue_context_setup_resp->srbs_to_be_setup_length,
        sizeof(f1ap_srb_to_be_setup_t));
    AssertFatal(f1ap_ue_context_setup_resp->srbs_to_be_setup,
                "could not allocate memory for f1ap_ue_context_setup_resp->drbs_setup\n");

    for (i = 0; i < f1ap_ue_context_setup_resp->srbs_to_be_setup_length; ++i) {
      f1ap_srb_to_be_setup_t *srb_p = &f1ap_ue_context_setup_resp->srbs_to_be_setup[i];
      F1AP_SRBs_Setup_Item_t *srbs_setup_item_p;
      srbs_setup_item_p = &((F1AP_SRBs_Setup_ItemIEs_t *)ie->value.choice.SRBs_Setup_List.list.array[i])->value.choice.SRBs_Setup_Item;
      srb_p->srb_id = srbs_setup_item_p->sRBID;
      srb_p->lcid = srbs_setup_item_p->lCID;
    }
  }

  itti_send_msg_to_task(TASK_RRC_GNB, instance, msg_p);
  return 0;
}

int CU_handle_UE_CONTEXT_SETUP_FAILURE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  AssertFatal(1==0,"Not implemented yet\n");
}

int CU_handle_UE_CONTEXT_RELEASE_REQUEST(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  MessageDef *msg = itti_alloc_new_message(TASK_CU_F1, 0,  F1AP_UE_CONTEXT_RELEASE_REQ);
  msg->ittiMsgHeader.originInstance = assoc_id;
  f1ap_ue_context_release_req_t *req = &F1AP_UE_CONTEXT_RELEASE_REQ(msg);
  F1AP_UEContextReleaseRequest_t    *container;
  F1AP_UEContextReleaseRequestIEs_t *ie;
  DevAssert(pdu);
  container = &pdu->choice.initiatingMessage->value.choice.UEContextReleaseRequest;
  /* GNB_CU_UE_F1AP_ID */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextReleaseRequestIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  req->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;

  /* GNB_DU_UE_F1AP_ID */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextReleaseRequestIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);
  req->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;

  /* Cause */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextReleaseRequestIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_Cause, true);

  switch(ie->value.choice.Cause.present)
  {
    case F1AP_Cause_PR_radioNetwork:
      req->cause = F1AP_CAUSE_RADIO_NETWORK;
      req->cause_value = ie->value.choice.Cause.choice.radioNetwork;
      break;
    case F1AP_Cause_PR_transport:
      req->cause = F1AP_CAUSE_TRANSPORT;
      req->cause_value = ie->value.choice.Cause.choice.transport;
      break;
    case F1AP_Cause_PR_protocol:
      req->cause = F1AP_CAUSE_PROTOCOL;
      req->cause_value = ie->value.choice.Cause.choice.protocol;
      break;
    case F1AP_Cause_PR_misc:
      req->cause = F1AP_CAUSE_MISC;
      req->cause_value = ie->value.choice.Cause.choice.misc;
      break;
    case F1AP_Cause_PR_NOTHING:
    default:
      req->cause = F1AP_CAUSE_NOTHING;
      break;
  }

  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextReleaseRequestIEs_t, ie, container, F1AP_ProtocolIE_ID_id_targetCellsToCancel, false);
  if (ie != NULL) {
    LOG_W(F1AP, "ignoring list of target cells to cancel in UE Context Release Request: implementation missing\n");
  }

  itti_send_msg_to_task(TASK_RRC_GNB, instance, msg);

  return 0;
}

int CU_send_UE_CONTEXT_RELEASE_COMMAND(sctp_assoc_t assoc_id, f1ap_ue_context_release_cmd_t *cmd)
{
  F1AP_F1AP_PDU_t                   pdu= {0};
  F1AP_UEContextReleaseCommand_t    *out;
  uint8_t  *buffer=NULL;
  uint32_t  len=0;
  /* Create */
  /* 0. Message Type */
  pdu.present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu.choice.initiatingMessage, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextRelease;
  tmp->criticality   = F1AP_Criticality_reject;
  tmp->value.present = F1AP_InitiatingMessage__value_PR_UEContextReleaseCommand;
  out = &tmp->value.choice.UEContextReleaseCommand;
  /* mandatory */
  /* c1. GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie1);
  ie1->id                             = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality                    = F1AP_Criticality_reject;
  ie1->value.present                  = F1AP_UEContextReleaseCommandIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = cmd->gNB_CU_ue_id;
  /* mandatory */
  /* c2. GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie2);
  ie2->id                             = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality                    = F1AP_Criticality_reject;
  ie2->value.present                  = F1AP_UEContextReleaseCommandIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = cmd->gNB_DU_ue_id;
  /* mandatory */
  /* c3. Cause */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie3);
  ie3->id                             = F1AP_ProtocolIE_ID_id_Cause;
  ie3->criticality                    = F1AP_Criticality_ignore;
  ie3->value.present                  = F1AP_UEContextReleaseCommandIEs__value_PR_Cause;

  switch (cmd->cause) {
    case F1AP_CAUSE_RADIO_NETWORK:
      ie3->value.choice.Cause.present = F1AP_Cause_PR_radioNetwork;
      ie3->value.choice.Cause.choice.radioNetwork = cmd->cause_value;
      break;

    case F1AP_CAUSE_TRANSPORT:
      ie3->value.choice.Cause.present = F1AP_Cause_PR_transport;
      ie3->value.choice.Cause.choice.transport = cmd->cause_value;
      break;

    case F1AP_CAUSE_PROTOCOL:
      ie3->value.choice.Cause.present = F1AP_Cause_PR_protocol;
      ie3->value.choice.Cause.choice.protocol = cmd->cause_value;
      break;

    case F1AP_CAUSE_MISC:
      ie3->value.choice.Cause.present = F1AP_Cause_PR_misc;
      ie3->value.choice.Cause.choice.misc = cmd->cause_value;
      break;

    case F1AP_CAUSE_NOTHING:
    default:
      ie3->value.choice.Cause.present = F1AP_Cause_PR_NOTHING;
      break;
  }

  /* optional */
  /* c4. RRCContainer */
  if(cmd->rrc_container!=NULL){
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie4);
    ie4->id                             = F1AP_ProtocolIE_ID_id_RRCContainer;
    ie4->criticality                    = F1AP_Criticality_ignore;
    ie4->value.present                  = F1AP_UEContextReleaseCommandIEs__value_PR_RRCContainer;
    OCTET_STRING_fromBuf(&ie4->value.choice.RRCContainer, (const char *)cmd->rrc_container,
                       cmd->rrc_container_length);

    // conditionally have SRBID if RRC Container
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie5);
    ie5->id = F1AP_ProtocolIE_ID_id_SRBID;
    ie5->criticality = F1AP_Criticality_ignore;
    ie5->value.present = F1AP_UEContextReleaseCommandIEs__value_PR_SRBID;
    ie5->value.choice.SRBID = cmd->srb_id;
  }

  /* encode */
  if (f1ap_encode_pdu(&pdu, &buffer, &len) < 0) {
    LOG_E(F1AP, "Failed to encode F1 context release command\n");
    return -1;
  }

  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  return 0;
}

int CU_handle_UE_CONTEXT_RELEASE_COMPLETE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  F1AP_UEContextReleaseComplete_t    *container;
  F1AP_UEContextReleaseCompleteIEs_t *ie;
  DevAssert(pdu);
  MessageDef *msg_p = itti_alloc_new_message(TASK_DU_F1, 0,  F1AP_UE_CONTEXT_RELEASE_COMPLETE);
  msg_p->ittiMsgHeader.originInstance = assoc_id;
  f1ap_ue_context_release_complete_t *complete = &F1AP_UE_CONTEXT_RELEASE_COMPLETE(msg_p);
  container = &pdu->choice.successfulOutcome->value.choice.UEContextReleaseComplete;
  /* GNB_CU_UE_F1AP_ID */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextReleaseCompleteIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  complete->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
  /* GNB_DU_UE_F1AP_ID */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextReleaseCompleteIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);
  complete->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;

  /* Optional*/
  /* CriticalityDiagnostics */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextReleaseCompleteIEs_t, ie, container,
                             F1AP_ProtocolIE_ID_id_CriticalityDiagnostics, false);

  if (ie) {
    // ie->value.choice.CriticalityDiagnostics.procedureCode
    // ie->value.choice.CriticalityDiagnostics.triggeringMessage
    // ie->value.choice.CriticalityDiagnostics.procedureCriticality
    // ie->value.choice.CriticalityDiagnostics.transactionID
    // F1AP_CriticalityDiagnostics_IE_List
  }

  itti_send_msg_to_task(TASK_RRC_GNB, instance, msg_p);

  return 0;
}

int CU_send_UE_CONTEXT_MODIFICATION_REQUEST(sctp_assoc_t assoc_id, const f1ap_ue_context_mod_req_t *req)
{
  F1AP_F1AP_PDU_t *pdu = encode_ue_context_mod_req(req);

  uint8_t  *buffer=NULL;
  uint32_t  len=0;
  if (f1ap_encode_pdu(pdu, &buffer, &len) < 0) {
    LOG_E(F1AP, "Failed to encode F1 UE Context Modification Request\n");
    return -1;
  }

  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
  return 0;
}

int CU_handle_UE_CONTEXT_MODIFICATION_RESPONSE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  MessageDef                       *msg_p;
  F1AP_UEContextModificationResponse_t    *container;
  F1AP_UEContextModificationResponseIEs_t *ie;
  DevAssert(pdu);
  msg_p = itti_alloc_new_message(TASK_DU_F1, 0,  F1AP_UE_CONTEXT_MODIFICATION_RESP);
  msg_p->ittiMsgHeader.originInstance = assoc_id;
  f1ap_ue_context_modif_resp_t *f1ap_ue_context_modification_resp = &F1AP_UE_CONTEXT_MODIFICATION_RESP(msg_p);
  container = &pdu->choice.successfulOutcome->value.choice.UEContextModificationResponse;
  int i;

    /* GNB_CU_UE_F1AP_ID */
    F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationResponseIEs_t, ie, container,
                               F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
    f1ap_ue_context_modification_resp->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;

    LOG_D(F1AP, "f1ap_ue_context_modif_resp->gNB_CU_ue_id is: %d \n", f1ap_ue_context_modification_resp->gNB_CU_ue_id);

    /* GNB_DU_UE_F1AP_ID */
    F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationResponseIEs_t, ie, container,
                               F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);
    f1ap_ue_context_modification_resp->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;

    LOG_D(F1AP, "f1ap_ue_context_modification_resp->gNB_DU_ue_id is: %d \n", f1ap_ue_context_modification_resp->gNB_DU_ue_id);

    // DUtoCURRCInformation
    F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationResponseIEs_t, ie, container,
                               F1AP_ProtocolIE_ID_id_DUtoCURRCInformation, false);
    if(ie!=NULL){
      f1ap_ue_context_modification_resp->du_to_cu_rrc_information = (du_to_cu_rrc_information_t *)calloc(1, sizeof(du_to_cu_rrc_information_t));
      f1ap_ue_context_modification_resp->du_to_cu_rrc_information->cellGroupConfig = (uint8_t *)calloc(1,ie->value.choice.DUtoCURRCInformation.cellGroupConfig.size);

      memcpy(f1ap_ue_context_modification_resp->du_to_cu_rrc_information->cellGroupConfig, ie->value.choice.DUtoCURRCInformation.cellGroupConfig.buf, ie->value.choice.DUtoCURRCInformation.cellGroupConfig.size);
      f1ap_ue_context_modification_resp->du_to_cu_rrc_information->cellGroupConfig_length = ie->value.choice.DUtoCURRCInformation.cellGroupConfig.size;
    }

    // DRBs_SetupMod_List
    F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationResponseIEs_t, ie, container,
                               F1AP_ProtocolIE_ID_id_DRBs_SetupMod_List, false);
    if(ie!=NULL){
      f1ap_ue_context_modification_resp->drbs_to_be_setup_length = ie->value.choice.DRBs_SetupMod_List.list.count;
      f1ap_ue_context_modification_resp->drbs_to_be_setup = calloc(f1ap_ue_context_modification_resp->drbs_to_be_setup_length,
          sizeof(f1ap_drb_to_be_setup_t));
      AssertFatal(f1ap_ue_context_modification_resp->drbs_to_be_setup,
                "could not allocate memory for f1ap_ue_context_setup_resp->drbs_setup\n");
      for (i = 0; i < f1ap_ue_context_modification_resp->drbs_to_be_setup_length; ++i) {
        f1ap_drb_to_be_setup_t *drb_p = &f1ap_ue_context_modification_resp->drbs_to_be_setup[i];
        F1AP_DRBs_SetupMod_Item_t *drbs_setupmod_item_p;
        drbs_setupmod_item_p = &((F1AP_DRBs_SetupMod_ItemIEs_t *)ie->value.choice.DRBs_SetupMod_List.list.array[i])->value.choice.DRBs_SetupMod_Item;
        drb_p->drb_id = drbs_setupmod_item_p->dRBID;
        // TODO in the following, assume only one UP UL TNL is present.
         // this matches/assumes OAI CU/DU implementation, can be up to 2!
        drb_p->up_dl_tnl_length = 1;
        AssertFatal(drbs_setupmod_item_p->dLUPTNLInformation_ToBeSetup_List.list.count > 0,
            "no DL UP TNL Information in DRBs to be Setup list\n");
        F1AP_DLUPTNLInformation_ToBeSetup_Item_t *dl_up_tnl_info_p = (F1AP_DLUPTNLInformation_ToBeSetup_Item_t *)drbs_setupmod_item_p->dLUPTNLInformation_ToBeSetup_List.list.array[0];
        F1AP_GTPTunnel_t *dl_up_tnl0 = dl_up_tnl_info_p->dLUPTNLInformation.choice.gTPTunnel;
        BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&dl_up_tnl0->transportLayerAddress, drb_p->up_dl_tnl[0].tl_address);
        OCTET_STRING_TO_UINT32(&dl_up_tnl0->gTP_TEID, drb_p->up_dl_tnl[0].teid);
      }
    }
    // SRBs_FailedToBeSetupMod_List
    F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationResponseIEs_t, ie, container,
                               F1AP_ProtocolIE_ID_id_SRBs_FailedToBeSetupMod_List, false);
    if(ie!=NULL){
      f1ap_ue_context_modification_resp->srbs_failed_to_be_setup_length = ie->value.choice.SRBs_FailedToBeSetupMod_List.list.count;
      f1ap_ue_context_modification_resp->srbs_failed_to_be_setup = calloc(f1ap_ue_context_modification_resp->srbs_failed_to_be_setup_length,
          sizeof(f1ap_rb_failed_to_be_setup_t));
      AssertFatal(f1ap_ue_context_modification_resp->srbs_failed_to_be_setup,
          "could not allocate memory for f1ap_ue_context_setup_resp->srbs_failed_to_be_setup\n");
      for (i = 0; i < f1ap_ue_context_modification_resp->srbs_failed_to_be_setup_length; ++i) {
        f1ap_rb_failed_to_be_setup_t *srb_p = &f1ap_ue_context_modification_resp->srbs_failed_to_be_setup[i];
        srb_p->rb_id = ((F1AP_SRBs_FailedToBeSetupMod_Item_t *)ie->value.choice.SRBs_FailedToBeSetupMod_List.list.array[i])->sRBID;
      }

    }
    // DRBs_FailedToBeSetupMod_List
    F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationResponseIEs_t, ie, container,
                               F1AP_ProtocolIE_ID_id_DRBs_FailedToBeSetupMod_List, false);
    if(ie!=NULL){
      f1ap_ue_context_modification_resp->drbs_failed_to_be_setup_length = ie->value.choice.DRBs_FailedToBeSetupMod_List.list.count;
      f1ap_ue_context_modification_resp->drbs_failed_to_be_setup = calloc(f1ap_ue_context_modification_resp->drbs_failed_to_be_setup_length,
          sizeof(f1ap_rb_failed_to_be_setup_t));
      AssertFatal(f1ap_ue_context_modification_resp->drbs_failed_to_be_setup,
          "could not allocate memory for f1ap_ue_context_setup_resp->drbs_failed_to_be_setup\n");
      for (i = 0; i < f1ap_ue_context_modification_resp->drbs_failed_to_be_setup_length; ++i) {
        f1ap_rb_failed_to_be_setup_t *drb_p = &f1ap_ue_context_modification_resp->drbs_failed_to_be_setup[i];
        drb_p->rb_id = ((F1AP_DRBs_FailedToBeSetupMod_Item_t *)ie->value.choice.DRBs_FailedToBeSetupMod_List.list.array[i])->dRBID;
      }
    }

    // SCell_FailedtoSetupMod_List
    F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationResponseIEs_t, ie, container,
                               F1AP_ProtocolIE_ID_id_SCell_FailedtoSetupMod_List, false);
    if(ie!=NULL){
      LOG_E (F1AP, "Not supporting handling of SCell_FailedtoSetupMod_List \n");
    }

    // SRBs_Setup_List
    F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationResponseIEs_t, ie, container,
        F1AP_ProtocolIE_ID_id_SRBs_SetupMod_List, false);
    if(ie!=NULL){
      f1ap_ue_context_modification_resp->srbs_to_be_setup_length = ie->value.choice.SRBs_SetupMod_List.list.count;
      f1ap_ue_context_modification_resp->srbs_to_be_setup = calloc(f1ap_ue_context_modification_resp->srbs_to_be_setup_length,
          sizeof(f1ap_srb_to_be_setup_t));
      AssertFatal(f1ap_ue_context_modification_resp->srbs_to_be_setup,
          "could not allocate memory for f1ap_ue_context_setup_resp->drbs_setup\n");
      for (i = 0; i < f1ap_ue_context_modification_resp->srbs_to_be_setup_length; ++i) {
        f1ap_srb_to_be_setup_t *srb_p = &f1ap_ue_context_modification_resp->srbs_to_be_setup[i];
        F1AP_SRBs_SetupMod_Item_t *srbs_setup_item_p;
        srbs_setup_item_p = &((F1AP_SRBs_SetupMod_ItemIEs_t *)ie->value.choice.SRBs_SetupMod_List.list.array[i])->value.choice.SRBs_SetupMod_Item;
        srb_p->srb_id = srbs_setup_item_p->sRBID;
        srb_p->lcid = srbs_setup_item_p->lCID;
      }
    }

    itti_send_msg_to_task(TASK_RRC_GNB, instance, msg_p);
    return 0;
}

int CU_handle_UE_CONTEXT_MODIFICATION_FAILURE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
    AssertFatal(1 == 0, "Not implemented yet\n");
}

int CU_handle_UE_CONTEXT_MODIFICATION_REQUIRED(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  DevAssert(pdu != NULL);

  MessageDef *msg_p = itti_alloc_new_message(TASK_DU_F1, 0, F1AP_UE_CONTEXT_MODIFICATION_REQUIRED);
  msg_p->ittiMsgHeader.originInstance = assoc_id;
  f1ap_ue_context_modif_required_t *required = &F1AP_UE_CONTEXT_MODIFICATION_REQUIRED(msg_p);

  F1AP_UEContextModificationRequired_t *container = &pdu->choice.initiatingMessage->value.choice.UEContextModificationRequired;
  F1AP_UEContextModificationRequiredIEs_t *ie = NULL;

  /* required: GNB_CU_UE_F1AP_ID */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t, ie, container, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  required->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;

  /* required: GNB_DU_UE_F1AP_ID */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t, ie, container, F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);
  required->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;

  /* optional: Resource Coordination Transfer Container */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_ResourceCoordinationTransferContainer,
                             false);
  AssertFatal(ie == NULL, "handling of Resource Coordination Transfer Container not implemented\n");

  /* optional: DU to CU RRC Information */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_DUtoCURRCInformation,
                             false);
  if (ie != NULL) {
    F1AP_DUtoCURRCInformation_t *du2cu = &ie->value.choice.DUtoCURRCInformation;
    required->du_to_cu_rrc_information = malloc(sizeof(*required->du_to_cu_rrc_information));
    AssertFatal(required->du_to_cu_rrc_information != NULL, "memory allocation failed\n");

    required->du_to_cu_rrc_information->cellGroupConfig = malloc(du2cu->cellGroupConfig.size);
    AssertFatal(required->du_to_cu_rrc_information->cellGroupConfig != NULL, "memory allocation failed\n");
    memcpy(required->du_to_cu_rrc_information->cellGroupConfig, du2cu->cellGroupConfig.buf, du2cu->cellGroupConfig.size);
    required->du_to_cu_rrc_information->cellGroupConfig_length = du2cu->cellGroupConfig.size;

    AssertFatal(du2cu->measGapConfig == NULL, "handling of measGapConfig not implemented\n");
    AssertFatal(du2cu->requestedP_MaxFR1 == NULL, "handling of requestedP_MaxFR1 not implemented\n");
  }

  /* optional: DRB Required to Be Modified List */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_DRBs_Required_ToBeModified_List,
                             false);
  AssertFatal(ie == NULL, "handling of DRBs Required to be modified list not implemented\n");

  /* optional: SRB Required to be Released List */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_SRBs_Required_ToBeReleased_List,
                             false);
  AssertFatal(ie == NULL, "handling of SRBs Required to be released list not implemented\n");

  /* optional: DRB Required to be Released List */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_DRBs_Required_ToBeReleased_List,
                             false);
  AssertFatal(ie == NULL, "handling of DRBs Required to be released list not implemented\n");

  /* mandatory: Cause */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t, ie, container, F1AP_ProtocolIE_ID_id_Cause, true);
  switch (ie->value.choice.Cause.present) {
    case F1AP_Cause_PR_radioNetwork:
      required->cause = F1AP_CAUSE_RADIO_NETWORK;
      required->cause_value = ie->value.choice.Cause.choice.radioNetwork;
      break;
    case F1AP_Cause_PR_transport:
      required->cause = F1AP_CAUSE_TRANSPORT;
      required->cause_value = ie->value.choice.Cause.choice.transport;
      break;
    case F1AP_Cause_PR_protocol:
      required->cause = F1AP_CAUSE_PROTOCOL;
      required->cause_value = ie->value.choice.Cause.choice.protocol;
      break;
    case F1AP_Cause_PR_misc:
      required->cause = F1AP_CAUSE_MISC;
      required->cause_value = ie->value.choice.Cause.choice.misc;
      break;
    default:
      LOG_W(F1AP, "Unknown cause for UE Context Modification required message\n");
      /* fall through */
    case F1AP_Cause_PR_NOTHING:
      required->cause = F1AP_CAUSE_NOTHING;
      break;
  }

  /* optional: BH RLC Channel Required to be Released List */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_BHChannels_Required_ToBeReleased_List,
                             false);
  AssertFatal(ie == NULL, "handling of BH RLC Channel Required to be Released list not implemented\n");

  /* optional: SL DRB Required to Be Modified List */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_SLDRBs_Required_ToBeModified_List,
                             false);
  AssertFatal(ie == NULL, "handling of SL DRB Required to be modified list not implemented\n");

  /* optional: SL DRB Required to be Released List */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_SLDRBs_Required_ToBeReleased_List,
                             false);
  AssertFatal(ie == NULL, "handling of SL DRBs Required to be released list not implemented\n");

  /* optional: Candidate Cells To Be Cancelled List */
  F1AP_FIND_PROTOCOLIE_BY_ID(F1AP_UEContextModificationRequiredIEs_t,
                             ie,
                             container,
                             F1AP_ProtocolIE_ID_id_Candidate_SpCell_List,
                             false);
  AssertFatal(ie == NULL, "handling of candidate cells to be cancelled list not implemented\n");

  itti_send_msg_to_task(TASK_RRC_GNB, instance, msg_p);
  return 0;
}

int CU_send_UE_CONTEXT_MODIFICATION_CONFIRM(sctp_assoc_t assoc_id, f1ap_ue_context_modif_confirm_t *confirm)
{
  F1AP_F1AP_PDU_t pdu = {0};
  pdu.present = F1AP_F1AP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu.choice.successfulOutcome, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextModificationRequired;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_SuccessfulOutcome__value_PR_UEContextModificationConfirm;
  F1AP_UEContextModificationConfirm_t *out = &tmp->value.choice.UEContextModificationConfirm;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationConfirmIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextModificationConfirmIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = confirm->gNB_CU_ue_id;

  /* mandatory: GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationConfirmIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_UEContextModificationConfirmIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = confirm->gNB_DU_ue_id;

  /* optional: Resource Coordination Transfer Container */
  /* not implemented*/

  /* optional: DRB Modified List */
  /* not implemented*/

  /* optional: RRC Container */
  if (confirm->rrc_container != NULL) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationConfirmIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_RRCContainer;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_UEContextModificationConfirmIEs__value_PR_RRCContainer;
    OCTET_STRING_fromBuf(&ie->value.choice.RRCContainer, (const char *)confirm->rrc_container, confirm->rrc_container_length);
  }

  /* optional: CriticalityDiagnostics */
  /* not implemented*/

  /* optional: Execute Duplication */
  /* not implemented*/

  /* optional: Resource Coordination Transfer Information */
  /* not implemented*/

  /* optional: SL DRB Modified List */
  /* not implemented*/

  /* encode */
  uint8_t *buffer = NULL;
  uint32_t len = 0;
  if (f1ap_encode_pdu(&pdu, &buffer, &len) < 0) {
    LOG_E(F1AP, "Failed to encode F1 UE Context Modification Confirm\n");
    return -1;
  }
  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  return 0;
}

int CU_send_UE_CONTEXT_MODIFICATION_REFUSE(sctp_assoc_t assoc_id, f1ap_ue_context_modif_refuse_t *refuse)
{
  F1AP_F1AP_PDU_t pdu = {0};
  pdu.present = F1AP_F1AP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu.choice.unsuccessfulOutcome, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextModificationRequired;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_UnsuccessfulOutcome__value_PR_UEContextModificationRefuse;
  F1AP_UEContextModificationRefuse_t *out = &tmp->value.choice.UEContextModificationRefuse;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRefuseIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextModificationRefuseIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = refuse->gNB_CU_ue_id;

  /* mandatory: GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRefuseIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_UEContextModificationRefuseIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = refuse->gNB_DU_ue_id;

  /* optional: Cause */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRefuseIEs_t, ie3);
  ie3->id = F1AP_ProtocolIE_ID_id_Cause;
  ie3->criticality = F1AP_Criticality_reject;
  ie3->value.present = F1AP_UEContextModificationRefuseIEs__value_PR_Cause;
  F1AP_Cause_t *cause = &ie3->value.choice.Cause;
  switch (refuse->cause) {
    case F1AP_CAUSE_RADIO_NETWORK:
      cause->present = F1AP_Cause_PR_radioNetwork;
      cause->choice.radioNetwork = refuse->cause_value;
      break;
    case F1AP_CAUSE_TRANSPORT:
      cause->present = F1AP_Cause_PR_transport;
      cause->choice.transport = refuse->cause_value;
      break;
    case F1AP_CAUSE_PROTOCOL:
      cause->present = F1AP_Cause_PR_protocol;
      cause->choice.protocol = refuse->cause_value;
      break;
    case F1AP_CAUSE_MISC:
      cause->present = F1AP_Cause_PR_misc;
      cause->choice.misc = refuse->cause_value;
      break;
    case F1AP_CAUSE_NOTHING:
    default:
      cause->present = F1AP_Cause_PR_NOTHING;
      break;
  } // switch

  /* optional: CriticalityDiagnostics */
  /* not implemented*/

  /* encode */
  uint8_t *buffer = NULL;
  uint32_t len = 0;
  if (f1ap_encode_pdu(&pdu, &buffer, &len) < 0) {
    LOG_E(F1AP, "Failed to encode F1 UE Context Modification Refuse\n");
    return -1;
  }
  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  return 0;
}
