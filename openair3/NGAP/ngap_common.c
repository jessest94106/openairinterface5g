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

/*! \file ngap_common.c
 * \brief ngap procedures for both gNB and AMF
 * \author Yoshio INOUE, Masayuki HARADA
 * \email yoshio.inoue@fujitsu.com,masayuki.harada@fujitsu.com (yoshio.inoue%40fujitsu.com%2cmasayuki.harada%40fujitsu.com)
 * \date 2020
 * \version 0.1
 */

#include <stdint.h>
#include "conversions.h"
#include "ngap_common.h"

void encode_ngap_cause(NGAP_Cause_t *out, const ngap_cause_t *in)
{
  switch (in->type) {
    case NGAP_CAUSE_RADIO_NETWORK:
      out->present = NGAP_Cause_PR_radioNetwork;
      out->choice.radioNetwork = in->value;
      break;

    case NGAP_CAUSE_TRANSPORT:
      out->present = NGAP_Cause_PR_transport;
      out->choice.transport = in->value;
      break;

    case NGAP_CAUSE_NAS:
      out->present = NGAP_Cause_PR_nas;
      out->choice.nas = in->value;
      break;

    case NGAP_CAUSE_PROTOCOL:
      out->present = NGAP_Cause_PR_protocol;
      out->choice.protocol = in->value;
      break;

    case NGAP_CAUSE_MISC:
      out->present = NGAP_Cause_PR_misc;
      out->choice.misc = in->value;
      break;

    case NGAP_CAUSE_NOTHING:
    default:
      AssertFatal(false, "Unknown failure cause %d\n", in->type);
      break;
  }
}

nr_guami_t decode_ngap_guami(const NGAP_GUAMI_t *in)
{
  nr_guami_t out = {0};
  TBCD_TO_MCC_MNC(&in->pLMNIdentity, out.mcc, out.mnc, out.mnc_len);
  OCTET_STRING_TO_INT8(&in->aMFRegionID, out.amf_region_id);
  OCTET_STRING_TO_INT16(&in->aMFSetID, out.amf_set_id);
  OCTET_STRING_TO_INT8(&in->aMFPointer, out.amf_pointer);
  return out;
}

ngap_ambr_t decode_ngap_UEAggregateMaximumBitRate(const NGAP_UEAggregateMaximumBitRate_t *in)
{
  ngap_ambr_t ambr = {0};
  asn_INTEGER2ulong(&in->uEAggregateMaximumBitRateUL, &ambr.br_ul);
  asn_INTEGER2ulong(&in->uEAggregateMaximumBitRateDL, &ambr.br_dl);
  return ambr;
}

nssai_t decode_ngap_nssai(const NGAP_S_NSSAI_t *in)
{
  nssai_t nssai = {0};
  OCTET_STRING_TO_INT8(&in->sST, nssai.sst);
  if (in->sD != NULL) {
    BUFFER_TO_INT24(in->sD->buf, nssai.sd);
  } else {
    nssai.sd = 0xffffff;
  }
  return nssai;
}

ngap_security_capabilities_t decode_ngap_security_capabilities(const NGAP_UESecurityCapabilities_t *in)
{
  ngap_security_capabilities_t out = {0};
  out.nRencryption_algorithms = BIT_STRING_to_uint16(&in->nRencryptionAlgorithms);
  out.nRintegrity_algorithms = BIT_STRING_to_uint16(&in->nRintegrityProtectionAlgorithms);
  out.eUTRAencryption_algorithms = BIT_STRING_to_uint16(&in->eUTRAencryptionAlgorithms);
  out.eUTRAintegrity_algorithms = BIT_STRING_to_uint16(&in->eUTRAintegrityProtectionAlgorithms);
  return out;
}

ngap_mobility_restriction_t decode_ngap_mobility_restriction(const NGAP_MobilityRestrictionList_t *in)
{
  ngap_mobility_restriction_t out = {0};
  TBCD_TO_MCC_MNC(&in->servingPLMN, out.serving_plmn.mcc, out.serving_plmn.mnc, out.serving_plmn.mnc_digit_length);
  return out;
}

void encode_ngap_target_id(NGAP_HandoverRequiredIEs_t *out, const target_ran_node_id_t *in)
{
  out->id = NGAP_ProtocolIE_ID_id_TargetID;
  out->criticality = NGAP_Criticality_reject;
  out->value.present = NGAP_HandoverRequiredIEs__value_PR_TargetID;
  // CHOICE Target ID: NG-RAN (M)
  out->value.choice.TargetID.present = NGAP_TargetID_PR_targetRANNodeID;
  asn1cCalloc(out->value.choice.TargetID.choice.targetRANNodeID, targetRan);
  // Global RAN Node ID (M)
  targetRan->globalRANNodeID.present = NGAP_GlobalRANNodeID_PR_globalGNB_ID;
  asn1cCalloc(targetRan->globalRANNodeID.choice.globalGNB_ID, globalGnbId);
  globalGnbId->gNB_ID.present = NGAP_GNB_ID_PR_gNB_ID;
  MACRO_GNB_ID_TO_BIT_STRING(in->targetgNBId, &globalGnbId->gNB_ID.choice.gNB_ID);
  MCC_MNC_TO_PLMNID(in->plmn_identity.mcc, in->plmn_identity.mnc, in->plmn_identity.mnc_digit_length, &globalGnbId->pLMNIdentity);
  // Selected TAI (M)
  INT24_TO_OCTET_STRING(in->tac, &targetRan->selectedTAI.tAC);
  MCC_MNC_TO_PLMNID(in->plmn_identity.mcc,
                    in->plmn_identity.mnc,
                    in->plmn_identity.mnc_digit_length,
                    &targetRan->selectedTAI.pLMNIdentity);
}

void encode_ngap_nr_cgi(NGAP_NR_CGI_t *out, const plmn_id_t *plmn, const uint32_t cell_id)
{
  out->iE_Extensions = NULL;
  MCC_MNC_TO_PLMNID(plmn->mcc, plmn->mnc, plmn->mnc_digit_length, &out->pLMNIdentity);
  NR_CELL_ID_TO_BIT_STRING(cell_id, &out->nRCellIdentity);
}
