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

#include <string.h>
#include <stdlib.h>

#include "common/utils/assertions.h"
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"

#include "e1ap_lib_common.h"
#include "e1ap_lib_includes.h"
#include "e1ap_messages_types.h"
#include "e1ap_interface_management.h"

/* ====================================
 *      GNB-CU-UP E1 SETUP REQUEST
 * ==================================== */

/**
 * @brief Encode GNB-CU-UP E1 Setup Request
 */
E1AP_E1AP_PDU_t *encode_e1ap_cuup_setup_request(const e1ap_setup_req_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  // Message Type (M)
  pdu->present = E1AP_E1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, m);
  m->procedureCode = E1AP_ProcedureCode_id_gNB_CU_UP_E1Setup;
  m->criticality = E1AP_Criticality_reject;
  m->value.present = E1AP_InitiatingMessage__value_PR_GNB_CU_UP_E1SetupRequest;
  E1AP_GNB_CU_UP_E1SetupRequest_t *e1SetupUP = &m->value.choice.GNB_CU_UP_E1SetupRequest;
  // Transaction ID (M)
  asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC1);
  ieC1->id = E1AP_ProtocolIE_ID_id_TransactionID;
  ieC1->criticality = E1AP_Criticality_reject;
  ieC1->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_TransactionID;
  ieC1->value.choice.TransactionID = msg->transac_id;
  // gNB-CU-UP ID (M)
  asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC2);
  ieC2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_ID;
  ieC2->criticality = E1AP_Criticality_reject;
  ieC2->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_GNB_CU_UP_ID;
  asn_int642INTEGER(&ieC2->value.choice.GNB_CU_UP_ID, msg->gNB_cu_up_id);
  // gNB-CU-UP Name (O)
  if (msg->gNB_cu_up_name) {
    asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC3);
    ieC3->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_Name;
    ieC3->criticality = E1AP_Criticality_ignore;
    ieC3->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_GNB_CU_UP_Name;
    OCTET_STRING_fromBuf(&ieC3->value.choice.GNB_CU_UP_Name, msg->gNB_cu_up_name, strlen(msg->gNB_cu_up_name));
  }
  // CN Support (M)
  asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC4);
  ieC4->id = E1AP_ProtocolIE_ID_id_CNSupport;
  ieC4->criticality = E1AP_Criticality_reject;
  ieC4->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_CNSupport;
  DevAssert(msg->cn_support == cn_support_5GC); /* only 5GC supported */
  ieC4->value.choice.CNSupport = msg->cn_support == cn_support_5GC ? E1AP_CNSupport_c_5gc : E1AP_CNSupport_c_epc;
  // Supported PLMNs (1..<maxnoofSPLMNs>)
  asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC5);
  ieC5->id = E1AP_ProtocolIE_ID_id_SupportedPLMNs;
  ieC5->criticality = E1AP_Criticality_reject;
  ieC5->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_SupportedPLMNs_List;
  for (int i = 0; i < msg->supported_plmns; i++) {
    asn1cSequenceAdd(ieC5->value.choice.SupportedPLMNs_List.list, E1AP_SupportedPLMNs_Item_t, supportedPLMN);
    // PLMN Identity (M)
    const PLMN_ID_t *id = &msg->plmn[i].id;
    MCC_MNC_TO_PLMNID(id->mcc, id->mnc, id->mnc_digit_length, &supportedPLMN->pLMN_Identity);
    // Slice Support List (O)
    int n = msg->plmn[i].supported_slices;
    if (msg->plmn[i].slice != NULL && n > 0) {
      supportedPLMN->slice_Support_List = calloc_or_fail(1, sizeof(*supportedPLMN->slice_Support_List));
      for (int s = 0; s < n; ++s) {
        asn1cSequenceAdd(supportedPLMN->slice_Support_List->list, E1AP_Slice_Support_Item_t, slice);
        e1ap_nssai_t *nssai = &msg->plmn[i].slice[s];
        INT8_TO_OCTET_STRING(nssai->sst, &slice->sNSSAI.sST);
        if (nssai->sd != 0xffffff) {
          slice->sNSSAI.sD = malloc_or_fail(sizeof(*slice->sNSSAI.sD));
          INT24_TO_OCTET_STRING(nssai->sd, slice->sNSSAI.sD);
        }
      }
    }
  }
  return pdu;
}

bool decode_e1ap_cuup_setup_request(const E1AP_E1AP_PDU_t *pdu, e1ap_setup_req_t *out)
{
  E1AP_GNB_CU_UP_E1SetupRequestIEs_t *ie;
  E1AP_GNB_CU_UP_E1SetupRequest_t *in = &pdu->choice.initiatingMessage->value.choice.GNB_CU_UP_E1SetupRequest;
  // Transaction ID (M)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_TransactionID, true);
  // gNB-CU-UP ID (M)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_ID, true);
  // CN Support (M)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_CNSupport, true);
  // Supported PLMNs (1..<maxnoofSPLMNs>)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_SupportedPLMNs, true);
  // Loop over all IEs
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_TransactionID:
        _E1_EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_TransactionID);
        out->transac_id = ie->value.choice.TransactionID;
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_ID:
        _E1_EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_GNB_CU_UP_ID);
        asn_INTEGER2ulong(&ie->value.choice.GNB_CU_UP_ID, &out->gNB_cu_up_id);
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_Name:
        _E1_EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_GNB_CU_UP_Name);
        // gNB-CU-UP Name (O)
        E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_Name, false);
        if (ie != NULL) {
          out->gNB_cu_up_name = calloc_or_fail(ie->value.choice.GNB_CU_UP_Name.size + 1, sizeof(char));
          memcpy(out->gNB_cu_up_name, ie->value.choice.GNB_CU_UP_Name.buf, ie->value.choice.GNB_CU_UP_Name.size);
        }
        break;
      case E1AP_ProtocolIE_ID_id_CNSupport:
        _E1_EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_CNSupport);
        _E1_EQ_CHECK_INT(ie->value.choice.CNSupport, E1AP_CNSupport_c_5gc); // only 5GC CN Support supported
        out->cn_support = ie->value.choice.CNSupport;
        break;
      case E1AP_ProtocolIE_ID_id_SupportedPLMNs:
        _E1_EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_SupportedPLMNs_List);
        out->supported_plmns = ie->value.choice.SupportedPLMNs_List.list.count;

        for (int i = 0; i < out->supported_plmns; i++) {
          E1AP_SupportedPLMNs_Item_t *supported_plmn_item =
              (E1AP_SupportedPLMNs_Item_t *)(ie->value.choice.SupportedPLMNs_List.list.array[i]);

          /* PLMN Identity */
          PLMN_ID_t *id = &out->plmn[i].id;
          PLMNID_TO_MCC_MNC(&supported_plmn_item->pLMN_Identity, id->mcc, id->mnc, id->mnc_digit_length);

          /* NSSAI */
          if (supported_plmn_item->slice_Support_List) {
            int n = supported_plmn_item->slice_Support_List->list.count;
            out->plmn[i].supported_slices = n;
            out->plmn[i].slice = calloc_or_fail(n, sizeof(*out->plmn[i].slice));
            for (int s = 0; s < n; ++s) {
              e1ap_nssai_t *slice = &out->plmn[i].slice[s];
              const E1AP_SNSSAI_t *es = &supported_plmn_item->slice_Support_List->list.array[s]->sNSSAI;
              OCTET_STRING_TO_INT8(&es->sST, slice->sst);
              slice->sd = 0xffffff;
              if (es->sD != NULL)
                OCTET_STRING_TO_INT24(es->sD, slice->sd);
            }
          }
        }
        break;
      default:
        PRINT_ERROR("Handle for this IE %ld is not implemented (or) invalid IE detected\n", ie->id);
        break;
    }
  }
  return true;
}

/**
 * @brief Deep copy function for E1 CUUP Setup Request
 */
e1ap_setup_req_t cp_e1ap_cuup_setup_request(const e1ap_setup_req_t *msg)
{
  e1ap_setup_req_t cp = {0};
  cp = *msg;
  if (msg->gNB_cu_up_name) {
    cp.gNB_cu_up_name = strdup(msg->gNB_cu_up_name);
  }
  for (int i = 0; i < msg->supported_plmns; i++) {
    if (msg->plmn[i].slice && msg->plmn[i].supported_slices > 0) {
      cp.plmn[i].slice = calloc_or_fail(msg->plmn[i].supported_slices, sizeof(*cp.plmn[i].slice));
      for (int s = 0; s < msg->plmn[i].supported_slices; ++s) {
        cp.plmn[i].slice[s] = msg->plmn[i].slice[s];
      }
    }
  }
  return cp;
}

/**
 * @brief Free memory allocated for E1 CUUP Setup Request
 */
void free_e1ap_cuup_setup_request(e1ap_setup_req_t *msg)
{
  // gNB-CU-UP Name
  free(msg->gNB_cu_up_name);
  // Slice support lists within PLMNs
  for (int i = 0; i < msg->supported_plmns; i++) {
    free(msg->plmn[i].slice);
  }
}

/**
 * @brief Equality check function for E1 CUUP Setup Request
 */
bool eq_e1ap_cuup_setup_request(const e1ap_setup_req_t *a, const e1ap_setup_req_t *b)
{
  _E1_EQ_CHECK_INT(a->transac_id, b->transac_id);
  _E1_EQ_CHECK_INT(a->gNB_cu_up_id, b->gNB_cu_up_id);
  if (a->gNB_cu_up_name && b->gNB_cu_up_name)
    _E1_EQ_CHECK_STR(a->gNB_cu_up_name, b->gNB_cu_up_name)
  _E1_EQ_CHECK_INT(a->supported_plmns, b->supported_plmns);
  for (int i = 0; i < a->supported_plmns; i++) {
    _E1_EQ_CHECK_INT(a->plmn[i].id.mcc, b->plmn[i].id.mcc);
    _E1_EQ_CHECK_INT(a->plmn[i].id.mnc, b->plmn[i].id.mnc);
    _E1_EQ_CHECK_INT(a->plmn[i].id.mnc_digit_length, b->plmn[i].id.mnc_digit_length);
    _E1_EQ_CHECK_INT(a->plmn[i].supported_slices, b->plmn[i].supported_slices);
    for (int s = 0; s < a->plmn[i].supported_slices; ++s) {
      _E1_EQ_CHECK_INT(a->plmn[i].slice[s].sst, b->plmn[i].slice[s].sst);
      _E1_EQ_CHECK_INT(a->plmn[i].slice[s].sd, b->plmn[i].slice[s].sd);
    }
  }
  return true;
}
