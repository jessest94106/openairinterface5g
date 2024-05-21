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

#include "common/utils/assertions.h"
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"

#include "f1ap_interface_management.h"
#include "f1ap_lib_common.h"
#include "f1ap_lib_includes.h"
#include "f1ap_messages_types.h"
#include "f1ap_lib_extern.h"

static const int nrb_lut[29] = {11,  18,  24,  25,  31,  32,  38,  51,  52,  65,  66,  78,  79,  93, 106,
                                107, 121, 132, 133, 135, 160, 162, 189, 216, 217, 245, 264, 270, 273};

static int to_NRNRB(int nrb)
{
  for (int i = 0; i < sizeofArray(nrb_lut); i++)
    if (nrb_lut[i] == nrb)
      return i;
  AssertFatal(1 == 0, "nrb %d is not in the list of possible NRNRB\n", nrb);
}

static int read_slice_info(const F1AP_ServedPLMNs_Item_t *plmn, nssai_t *nssai, int max_nssai)
{
  if (plmn->iE_Extensions == NULL)
    return 0;

  const F1AP_ProtocolExtensionContainer_10696P34_t *p = (F1AP_ProtocolExtensionContainer_10696P34_t *)plmn->iE_Extensions;
  if (p->list.count == 0)
    return 0;

  const F1AP_ServedPLMNs_ItemExtIEs_t *splmn = p->list.array[0];
  DevAssert(splmn->id == F1AP_ProtocolIE_ID_id_TAISliceSupportList);
  DevAssert(splmn->extensionValue.present == F1AP_ServedPLMNs_ItemExtIEs__extensionValue_PR_SliceSupportList);
  const F1AP_SliceSupportList_t *ssl = &splmn->extensionValue.choice.SliceSupportList;
  AssertFatal(ssl->list.count <= max_nssai, "cannot handle more than 16 slices\n");
  for (int s = 0; s < ssl->list.count; ++s) {
    const F1AP_SliceSupportItem_t *sl = ssl->list.array[s];
    nssai_t *n = &nssai[s];
    OCTET_STRING_TO_INT8(&sl->sNSSAI.sST, n->sst);
    n->sd = 0xffffff;
    if (sl->sNSSAI.sD != NULL)
      OCTET_STRING_TO_INT24(sl->sNSSAI.sD, n->sd);
  }

  return ssl->list.count;
}

/**
 * @brief F1AP Setup Request memory management
 */
static void free_f1ap_cell(const f1ap_served_cell_info_t *info, const f1ap_gnb_du_system_info_t *sys_info)
{
  if (sys_info) {
    free(sys_info->mib);
    free(sys_info->sib1);
    free((void *)sys_info);
  }
  free(info->measurement_timing_config);
  free(info->tac);
}

/**
 * @brief Encoding of Served Cell Information (9.3.1.10 of 3GPP TS 38.473)
 */
static F1AP_Served_Cell_Information_t encode_served_cell_info(const f1ap_served_cell_info_t *c)
{
  F1AP_Served_Cell_Information_t scell_info = {0};
  // NR CGI
  MCC_MNC_TO_PLMNID(c->plmn.mcc, c->plmn.mnc, c->plmn.mnc_digit_length, &(scell_info.nRCGI.pLMN_Identity));
  NR_CELL_ID_TO_BIT_STRING(c->nr_cellid, &(scell_info.nRCGI.nRCellIdentity));
  // NR PCI
  scell_info.nRPCI = c->nr_pci; // int 0..1007
  // 5GS TAC
  if (c->tac != NULL) {
    uint32_t tac = htonl(*c->tac);
    asn1cCalloc(scell_info.fiveGS_TAC, netOrder);
    OCTET_STRING_fromBuf(netOrder, ((char *)&tac) + 1, 3);
  }
  // Served PLMNs
  asn1cSequenceAdd(scell_info.servedPLMNs.list, F1AP_ServedPLMNs_Item_t, servedPLMN_item);
  MCC_MNC_TO_PLMNID(c->plmn.mcc, c->plmn.mnc, c->plmn.mnc_digit_length, &servedPLMN_item->pLMN_Identity);
  // NR-Mode-Info
  F1AP_NR_Mode_Info_t *nR_Mode_Info = &scell_info.nR_Mode_Info;
  if (c->mode == F1AP_MODE_FDD) { // FDD
    const f1ap_fdd_info_t *fdd = &c->fdd;
    nR_Mode_Info->present = F1AP_NR_Mode_Info_PR_fDD;
    asn1cCalloc(nR_Mode_Info->choice.fDD, fDD_Info);
    /* FDD.1.1 UL NRFreqInfo ARFCN */
    fDD_Info->uL_NRFreqInfo.nRARFCN = fdd->ul_freqinfo.arfcn;
    /* FDD.1.3 freqBandListNr */
    int ul_band = 1;
    for (int j = 0; j < ul_band; j++) {
      asn1cSequenceAdd(fDD_Info->uL_NRFreqInfo.freqBandListNr.list, F1AP_FreqBandNrItem_t, nr_freqBandNrItem);
      /* FDD.1.3.1 freqBandIndicatorNr*/
      nr_freqBandNrItem->freqBandIndicatorNr = fdd->ul_freqinfo.band;
    }
    /* FDD.2.1 DL NRFreqInfo ARFCN */
    fDD_Info->dL_NRFreqInfo.nRARFCN = fdd->dl_freqinfo.arfcn;
    /* FDD.2.3 freqBandListNr */
    int dl_bands = 1;
    for (int j = 0; j < dl_bands; j++) {
      asn1cSequenceAdd(fDD_Info->dL_NRFreqInfo.freqBandListNr.list, F1AP_FreqBandNrItem_t, nr_freqBandNrItem);
      /* FDD.2.3.1 freqBandIndicatorNr*/
      nr_freqBandNrItem->freqBandIndicatorNr = fdd->dl_freqinfo.band;
    } // for FDD : DL freq_Bands
    /* FDD.3 UL Transmission Bandwidth */
    fDD_Info->uL_Transmission_Bandwidth.nRSCS = fdd->ul_tbw.scs;
    fDD_Info->uL_Transmission_Bandwidth.nRNRB = to_NRNRB(fdd->ul_tbw.nrb);
    /* FDD.4 DL Transmission Bandwidth */
    fDD_Info->dL_Transmission_Bandwidth.nRSCS = fdd->dl_tbw.scs;
    fDD_Info->dL_Transmission_Bandwidth.nRNRB = to_NRNRB(fdd->dl_tbw.nrb);
  } else if (c->mode == F1AP_MODE_TDD) {
    const f1ap_tdd_info_t *tdd = &c->tdd;
    nR_Mode_Info->present = F1AP_NR_Mode_Info_PR_tDD;
    asn1cCalloc(nR_Mode_Info->choice.tDD, tDD_Info);
    /* TDD.1.1 nRFreqInfo ARFCN */
    tDD_Info->nRFreqInfo.nRARFCN = tdd->freqinfo.arfcn;
    /* TDD.1.3 freqBandListNr */
    int bands = 1;
    for (int j = 0; j < bands; j++) {
      asn1cSequenceAdd(tDD_Info->nRFreqInfo.freqBandListNr.list, F1AP_FreqBandNrItem_t, nr_freqBandNrItem);
      /* TDD.1.3.1 freqBandIndicatorNr*/
      nr_freqBandNrItem->freqBandIndicatorNr = tdd->freqinfo.band;
    }
    /* TDD.2 transmission_Bandwidth */
    tDD_Info->transmission_Bandwidth.nRSCS = tdd->tbw.scs;
    tDD_Info->transmission_Bandwidth.nRNRB = to_NRNRB(tdd->tbw.nrb);
  } else {
    AssertFatal(false, "unknown duplex mode %d\n", c->mode);
  }
  // Measurement Timing Configuration
  OCTET_STRING_fromBuf(&scell_info.measurementTimingConfiguration,
                       (const char *)c->measurement_timing_config,
                       c->measurement_timing_config_len);

  return scell_info;
}

static bool decode_served_cell_info(const F1AP_Served_Cell_Information_t *in, f1ap_served_cell_info_t *info)
{
  AssertError(in != NULL, return false, "Input message pointer is NULL");
  // 5GS TAC (O)
  if (in->fiveGS_TAC) {
    info->tac = malloc_or_fail(sizeof(*info->tac));
    OCTET_STRING_TO_INT24(in->fiveGS_TAC, *info->tac);
  }
  // NR CGI (M)
  TBCD_TO_MCC_MNC(&(in->nRCGI.pLMN_Identity), info->plmn.mcc, info->plmn.mnc, info->plmn.mnc_digit_length);
  // NR Cell Identity (M)
  BIT_STRING_TO_NR_CELL_IDENTITY(&in->nRCGI.nRCellIdentity, info->nr_cellid);
  // NR PCI (M)
  info->nr_pci = in->nRPCI;
  // Served PLMNs (>= 1)
  AssertError(in->servedPLMNs.list.count == 1, return false, "at least and only 1 PLMN must be present");
  info->num_ssi = read_slice_info(in->servedPLMNs.list.array[0], info->nssai, 16);
  // FDD Info
  if (in->nR_Mode_Info.present == F1AP_NR_Mode_Info_PR_fDD) {
    info->mode = F1AP_MODE_FDD;
    f1ap_fdd_info_t *FDDs = &info->fdd;
    F1AP_FDD_Info_t *fDD_Info = in->nR_Mode_Info.choice.fDD;
    FDDs->ul_freqinfo.arfcn = fDD_Info->uL_NRFreqInfo.nRARFCN;
    // Note: cannot handle more than one UL frequency band
    int num_ulBands = fDD_Info->uL_NRFreqInfo.freqBandListNr.list.count;
    AssertError(num_ulBands == 1, return false, "1 FDD UL frequency band must be present, more is not supported");
    for (int f = 0; f < num_ulBands && f < 1; f++) {
      F1AP_FreqBandNrItem_t *FreqItem = fDD_Info->uL_NRFreqInfo.freqBandListNr.list.array[f];
      FDDs->ul_freqinfo.band = FreqItem->freqBandIndicatorNr;
      AssertError(FreqItem->supportedSULBandList.list.count == 0, return false, "cannot handle FDD SUL bands");
    }
    // Note: cannot handle more than one DL frequency band
    FDDs->dl_freqinfo.arfcn = fDD_Info->dL_NRFreqInfo.nRARFCN;
    int num_dlBands = fDD_Info->dL_NRFreqInfo.freqBandListNr.list.count;
    AssertError(num_dlBands == 1, return false, "1 FDD DL frequency band must be present, more is not supported");
    for (int dlB = 0; dlB < num_dlBands && dlB < 1; dlB++) {
      F1AP_FreqBandNrItem_t *FreqItem = fDD_Info->dL_NRFreqInfo.freqBandListNr.list.array[dlB];
      FDDs->dl_freqinfo.band = FreqItem->freqBandIndicatorNr;
      AssertError(FreqItem->supportedSULBandList.list.count == 0, return false, "cannot handle FDD SUL bands");
    }
    FDDs->ul_tbw.scs = fDD_Info->uL_Transmission_Bandwidth.nRSCS;
    FDDs->ul_tbw.nrb = nrb_lut[fDD_Info->uL_Transmission_Bandwidth.nRNRB];
    FDDs->dl_tbw.scs = fDD_Info->dL_Transmission_Bandwidth.nRSCS;
    FDDs->dl_tbw.nrb = nrb_lut[fDD_Info->dL_Transmission_Bandwidth.nRNRB];
  } else if (in->nR_Mode_Info.present == F1AP_NR_Mode_Info_PR_tDD) {
    info->mode = F1AP_MODE_TDD;
    f1ap_tdd_info_t *TDDs = &info->tdd;
    F1AP_TDD_Info_t *tDD_Info = in->nR_Mode_Info.choice.tDD;
    TDDs->freqinfo.arfcn = tDD_Info->nRFreqInfo.nRARFCN;
    // Handle frequency bands
    int num_tddBands = tDD_Info->nRFreqInfo.freqBandListNr.list.count;
    AssertError(num_tddBands == 1, return false, "1 TDD DL frequency band must be present, more is not supported");
    for (int f = 0; f < num_tddBands && f < 1; f++) {
      struct F1AP_FreqBandNrItem *FreqItem = tDD_Info->nRFreqInfo.freqBandListNr.list.array[f];
      TDDs->freqinfo.band = FreqItem->freqBandIndicatorNr;
      AssertError(FreqItem->supportedSULBandList.list.count == 0, return false, "cannot handle TDD SUL bands");
    }
    TDDs->tbw.scs = tDD_Info->transmission_Bandwidth.nRSCS;
    TDDs->tbw.nrb = nrb_lut[tDD_Info->transmission_Bandwidth.nRNRB];
  } else {
    AssertError(1 == 0, return false, "unknown or missing NR Mode info %d\n", in->nR_Mode_Info.present);
  }
  // MeasurementConfig (M)
  AssertError(in->measurementTimingConfiguration.size > 0, return false, "measurementTimingConfigurationc size is 0");
  info->measurement_timing_config = cp_octet_string(&in->measurementTimingConfiguration, &info->measurement_timing_config_len);
  return true;
}

static F1AP_GNB_DU_System_Information_t *encode_system_info(const f1ap_gnb_du_system_info_t *sys_info)
{
  if (sys_info == NULL)
    return NULL; /* optional: can be NULL */

  F1AP_GNB_DU_System_Information_t *enc_sys_info = calloc_or_fail(1, sizeof(*enc_sys_info));

  AssertFatal(sys_info->mib != NULL, "MIB must be present in DU sys info\n");
  OCTET_STRING_fromBuf(&enc_sys_info->mIB_message, (const char *)sys_info->mib, sys_info->mib_length);

  AssertFatal(sys_info->sib1 != NULL, "SIB1 must be present in DU sys info\n");
  OCTET_STRING_fromBuf(&enc_sys_info->sIB1_message, (const char *)sys_info->sib1, sys_info->sib1_length);

  return enc_sys_info;
}

static void decode_system_info(struct F1AP_GNB_DU_System_Information *DUsi, f1ap_gnb_du_system_info_t *sys_info)
{
  /* mib */
  sys_info->mib = calloc_or_fail(DUsi->mIB_message.size, sizeof(*sys_info->mib));
  memcpy(sys_info->mib, DUsi->mIB_message.buf, DUsi->mIB_message.size);
  sys_info->mib_length = DUsi->mIB_message.size;
  /* sib1 */
  sys_info->sib1 = calloc_or_fail(DUsi->sIB1_message.size, sizeof(*sys_info->sib1));
  memcpy(sys_info->sib1, DUsi->sIB1_message.buf, DUsi->sIB1_message.size);
  sys_info->sib1_length = DUsi->sIB1_message.size;
}

/* ====================================
 *          F1 SETUP REQUEST
 * ==================================== */

/**
 * @brief F1 SETUP REQUEST encoding (9.2.1.4 of 3GPP TS 38.473)
 *        gNB-DU → gNB-CU
 */
F1AP_F1AP_PDU_t *encode_f1ap_setup_request(const f1ap_setup_req_t *msg)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Create */
  /* 0. pdu Type */
  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = F1AP_ProcedureCode_id_F1Setup;
  initMsg->criticality = F1AP_Criticality_reject;
  initMsg->value.present = F1AP_InitiatingMessage__value_PR_F1SetupRequest;
  F1AP_F1SetupRequest_t *f1Setup = &initMsg->value.choice.F1SetupRequest;
  // Transaction ID (M)
  asn1cSequenceAdd(f1Setup->protocolIEs.list, F1AP_F1SetupRequestIEs_t, ieC1);
  ieC1->id = F1AP_ProtocolIE_ID_id_TransactionID;
  ieC1->criticality = F1AP_Criticality_reject;
  ieC1->value.present = F1AP_F1SetupRequestIEs__value_PR_TransactionID;
  ieC1->value.choice.TransactionID = msg->transaction_id;
  // gNB-DU ID (M)
  asn1cSequenceAdd(f1Setup->protocolIEs.list, F1AP_F1SetupRequestIEs_t, ieC2);
  ieC2->id = F1AP_ProtocolIE_ID_id_gNB_DU_ID;
  ieC2->criticality = F1AP_Criticality_reject;
  ieC2->value.present = F1AP_F1SetupRequestIEs__value_PR_GNB_DU_ID;
  asn_int642INTEGER(&ieC2->value.choice.GNB_DU_ID, msg->gNB_DU_id);
  // gNB-DU Name (O)
  if (msg->gNB_DU_name) {
    asn1cSequenceAdd(f1Setup->protocolIEs.list, F1AP_F1SetupRequestIEs_t, ieC3);
    ieC3->id = F1AP_ProtocolIE_ID_id_gNB_DU_Name;
    ieC3->criticality = F1AP_Criticality_ignore;
    ieC3->value.present = F1AP_F1SetupRequestIEs__value_PR_GNB_DU_Name;
    OCTET_STRING_fromBuf(&ieC3->value.choice.GNB_DU_Name, msg->gNB_DU_name, strlen(msg->gNB_DU_name));
  }
  /// gNB-DU Served Cells List (0..1)
  asn1cSequenceAdd(f1Setup->protocolIEs.list, F1AP_F1SetupRequestIEs_t, ieCells);
  ieCells->id = F1AP_ProtocolIE_ID_id_gNB_DU_Served_Cells_List;
  ieCells->criticality = F1AP_Criticality_reject;
  ieCells->value.present = F1AP_F1SetupRequestIEs__value_PR_GNB_DU_Served_Cells_List;
  for (int i = 0; i < msg->num_cells_available; i++) {
    // gNB-DU Served Cells Item (M)
    const f1ap_served_cell_info_t *cell = &msg->cell[i].info;
    const f1ap_gnb_du_system_info_t *sys_info = msg->cell[i].sys_info;
    asn1cSequenceAdd(ieCells->value.choice.GNB_DU_Served_Cells_List.list, F1AP_GNB_DU_Served_Cells_ItemIEs_t, duServedCell);
    duServedCell->id = F1AP_ProtocolIE_ID_id_GNB_DU_Served_Cells_Item;
    duServedCell->criticality = F1AP_Criticality_reject;
    duServedCell->value.present = F1AP_GNB_DU_Served_Cells_ItemIEs__value_PR_GNB_DU_Served_Cells_Item;
    F1AP_GNB_DU_Served_Cells_Item_t *scell_item = &duServedCell->value.choice.GNB_DU_Served_Cells_Item;
    scell_item->served_Cell_Information = encode_served_cell_info(cell);
    scell_item->gNB_DU_System_Information = encode_system_info(sys_info);
  }
  // gNB-DU RRC version (M)
  asn1cSequenceAdd(f1Setup->protocolIEs.list, F1AP_F1SetupRequestIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_GNB_DU_RRC_Version;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_F1SetupRequestIEs__value_PR_RRC_Version;
  // RRC Version: "This IE is not used in this release."
  // we put one bit for each byte in rrc_ver that is != 0
  uint8_t bits = 0;
  for (int i = 0; i < sizeofArray(msg->rrc_ver); ++i)
    bits |= (msg->rrc_ver[i] != 0) << i;
  BIT_STRING_t *bs = &ie2->value.choice.RRC_Version.latest_RRC_Version;
  bs->buf = calloc_or_fail(1, sizeof(*bs->buf));
  bs->buf[0] = bits;
  bs->size = 1;
  bs->bits_unused = 5;

  F1AP_ProtocolExtensionContainer_10696P228_t *p = calloc_or_fail(1, sizeof(*p));
  asn1cSequenceAdd(p->list, F1AP_RRC_Version_ExtIEs_t, rrcv_ext);
  rrcv_ext->id = F1AP_ProtocolIE_ID_id_latest_RRC_Version_Enhanced;
  rrcv_ext->criticality = F1AP_Criticality_ignore;
  rrcv_ext->extensionValue.present = F1AP_RRC_Version_ExtIEs__extensionValue_PR_OCTET_STRING_SIZE_3_;
  OCTET_STRING_t *os = &rrcv_ext->extensionValue.choice.OCTET_STRING_SIZE_3_;
  os->size = sizeofArray(msg->rrc_ver);
  os->buf = malloc_or_fail(sizeof(msg->rrc_ver));
  for (int i = 0; i < sizeofArray(msg->rrc_ver); ++i)
    os->buf[i] = msg->rrc_ver[i];
  ie2->value.choice.RRC_Version.iE_Extensions = (struct F1AP_ProtocolExtensionContainer *)p;
  return pdu;
}

/**
 * @brief F1AP Setup Request decoding
 */
bool decode_f1ap_setup_request(const F1AP_F1AP_PDU_t *pdu, f1ap_setup_req_t *out)
{
  /* Check presence of message type */
  _F1_EQ_CHECK_INT(pdu->present, F1AP_F1AP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "pdu->choice.initiatingMessage is NULL");
  _F1_EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, F1AP_ProcedureCode_id_F1Setup);
  _F1_EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, F1AP_InitiatingMessage__value_PR_F1SetupRequest);
  /* Check presence of mandatory IEs */
  F1AP_F1SetupRequest_t *in = &pdu->choice.initiatingMessage->value.choice.F1SetupRequest;
  F1AP_F1SetupRequestIEs_t *ie;
  F1AP_LIB_FIND_IE(F1AP_F1SetupRequestIEs_t, ie, in, F1AP_ProtocolIE_ID_id_TransactionID, true);
  F1AP_LIB_FIND_IE(F1AP_F1SetupRequestIEs_t, ie, in, F1AP_ProtocolIE_ID_id_gNB_DU_ID, true);
  F1AP_LIB_FIND_IE(F1AP_F1SetupRequestIEs_t, ie, in, F1AP_ProtocolIE_ID_id_GNB_DU_RRC_Version, true);
  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    AssertError(in->protocolIEs.list.array[i] != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    ie = in->protocolIEs.list.array[i];
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_TransactionID:
        // Transaction ID (M)
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_F1SetupRequestIEs__value_PR_TransactionID);
        AssertError(ie->value.choice.TransactionID != -1, return false, "ie->value.choice.TransactionID is -1");
        out->transaction_id = ie->value.choice.TransactionID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_ID:
        // gNB-DU ID (M)
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_F1SetupRequestIEs__value_PR_GNB_DU_ID);
        asn_INTEGER2ulong(&ie->value.choice.GNB_DU_ID, &out->gNB_DU_id);
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_Name: {
        const F1AP_GNB_DU_Name_t *du_name = &ie->value.choice.GNB_DU_Name;
        out->gNB_DU_name = calloc_or_fail(du_name->size + 1, sizeof(*out->gNB_DU_name));
        strncpy(out->gNB_DU_name, (char *)du_name->buf, du_name->size);
        break;
      }
      case F1AP_ProtocolIE_ID_id_gNB_DU_Served_Cells_List:
        /* GNB_DU_Served_Cells_List */
        out->num_cells_available = ie->value.choice.GNB_DU_Served_Cells_List.list.count;
        AssertError(out->num_cells_available > 0, return false, "at least 1 cell must be present");
        /* Loop over gNB-DU Served Cells Items */
        for (int i = 0; i < out->num_cells_available; i++) {
          F1AP_GNB_DU_Served_Cells_Item_t *served_cells_item =
              &(((F1AP_GNB_DU_Served_Cells_ItemIEs_t *)ie->value.choice.GNB_DU_Served_Cells_List.list.array[i])
                    ->value.choice.GNB_DU_Served_Cells_Item);
          // gNB-DU System Information (O)
          struct F1AP_GNB_DU_System_Information *DUsi = served_cells_item->gNB_DU_System_Information;
          if (DUsi != NULL) {
            // System Information
            out->cell[i].sys_info = calloc_or_fail(1, sizeof(*out->cell[i].sys_info));
            f1ap_gnb_du_system_info_t *sys_info = out->cell[i].sys_info;
            /* mib */
            sys_info->mib = cp_octet_string(&DUsi->mIB_message, &sys_info->mib_length);
            /* sib1 */
            sys_info->sib1 = cp_octet_string(&DUsi->sIB1_message, &sys_info->sib1_length);
          }
          /* Served Cell Information (M) */
          if (!decode_served_cell_info(&served_cells_item->served_Cell_Information, &out->cell[i].info))
            return false;
        }
        break;
      case F1AP_ProtocolIE_ID_id_GNB_DU_RRC_Version:
        /* gNB-DU RRC version (M) */
        if (ie->value.choice.RRC_Version.iE_Extensions) {
          F1AP_ProtocolExtensionContainer_10696P228_t *ext =
              (F1AP_ProtocolExtensionContainer_10696P228_t *)ie->value.choice.RRC_Version.iE_Extensions;
          if (ext->list.count > 0) {
            F1AP_RRC_Version_ExtIEs_t *rrcext = ext->list.array[0];
            OCTET_STRING_t *os = &rrcext->extensionValue.choice.OCTET_STRING_SIZE_3_;
            DevAssert(os->size == sizeofArray(out->rrc_ver));
            for (int i = 0; i < os->size; ++i)
              out->rrc_ver[i] = os->buf[i];
          }
        }
        break;
      default:
        AssertError(1 == 0, return false, "F1AP_ProtocolIE_ID_id %ld unknown\n", ie->id);
        break;
    }
  }
  return true;
}

static void copy_f1ap_served_cell_info(f1ap_served_cell_info_t *dest, const f1ap_served_cell_info_t *src) {
  *dest = *src;
  dest->mode = src->mode;
  dest->tdd = src->tdd;
  dest->fdd = src->fdd;
  dest->plmn = src->plmn;
  if (src->tac) {
    dest->tac = malloc_or_fail(sizeof(*dest->tac));
    *dest->tac = *src->tac;
  }
  if (src->measurement_timing_config_len) {
    dest->measurement_timing_config = calloc_or_fail(src->measurement_timing_config_len, sizeof(*dest->measurement_timing_config));
    memcpy(dest->measurement_timing_config, src->measurement_timing_config, src->measurement_timing_config_len);
  }
}

/**
 * @brief F1AP Setup Request deep copy
 */
f1ap_setup_req_t cp_f1ap_setup_request(const f1ap_setup_req_t *msg)
{
  f1ap_setup_req_t cp = {0};
  /* gNB_DU_id */
  cp.gNB_DU_id = msg->gNB_DU_id;
  /* gNB_DU_name */
  if (msg->gNB_DU_name != NULL)
    cp.gNB_DU_name = strdup(msg->gNB_DU_name);
  /* transaction_id */
  cp.transaction_id = msg->transaction_id;
  /* num_cells_available */
  cp.num_cells_available = msg->num_cells_available;
  for (int n = 0; n < msg->num_cells_available; n++) {
    /* cell.info */
    f1ap_served_cell_info_t *sci = &cp.cell[n].info;
    const f1ap_served_cell_info_t *msg_sci = &msg->cell[n].info;
    copy_f1ap_served_cell_info(sci, msg_sci);
    /* cell.sys_info */
    if (msg->cell[n].sys_info) {
      f1ap_gnb_du_system_info_t *orig_sys_info = msg->cell[n].sys_info;
      f1ap_gnb_du_system_info_t *copy_sys_info = calloc_or_fail(1, sizeof(*copy_sys_info));
      cp.cell[n].sys_info = copy_sys_info;
      if (orig_sys_info->mib_length > 0) {
        copy_sys_info->mib = calloc_or_fail(orig_sys_info->mib_length, sizeof(*copy_sys_info->mib));
        copy_sys_info->mib_length = orig_sys_info->mib_length;
        memcpy(copy_sys_info->mib, orig_sys_info->mib, copy_sys_info->mib_length);
      }
      if (orig_sys_info->sib1_length > 0) {
        copy_sys_info->sib1 = calloc_or_fail(orig_sys_info->sib1_length, sizeof(*copy_sys_info->sib1));
        copy_sys_info->sib1_length = orig_sys_info->sib1_length;
        memcpy(copy_sys_info->sib1, orig_sys_info->sib1, copy_sys_info->sib1_length);
      }
    }
  }
  for (int i = 0; i < sizeofArray(msg->rrc_ver); i++)
    cp.rrc_ver[i] = msg->rrc_ver[i];
  return cp;
}

/**
 * @brief F1AP Setup Request equality check
 */
bool eq_f1ap_setup_request(const f1ap_setup_req_t *a, const f1ap_setup_req_t *b)
{
  _F1_EQ_CHECK_LONG(a->gNB_DU_id, b->gNB_DU_id);
  _F1_EQ_CHECK_STR(a->gNB_DU_name, b->gNB_DU_name);
  _F1_EQ_CHECK_LONG(a->transaction_id, b->transaction_id);
  _F1_EQ_CHECK_INT(a->num_cells_available, b->num_cells_available);
  for (int i = 0; i < a->num_cells_available; i++) {
    if (!eq_f1ap_cell_info(&a->cell[i].info, &b->cell[i].info))
      return false;
    if (!eq_f1ap_sys_info(a->cell[i].sys_info, b->cell[i].sys_info))
      return false;
  }
  _F1_EQ_CHECK_LONG(sizeofArray(a->rrc_ver), sizeofArray(b->rrc_ver));
  for (int i = 0; i < sizeofArray(a->rrc_ver); i++) {
    _F1_EQ_CHECK_INT(a->rrc_ver[i], b->rrc_ver[i]);
  }
  return true;
}

/**
 * @brief F1AP Setup Request memory management
 */
void free_f1ap_setup_request(const f1ap_setup_req_t *msg)
{
  DevAssert(msg != NULL);
  free(msg->gNB_DU_name);
  for (int i = 0; i < msg->num_cells_available; i++) {
    free_f1ap_cell(&msg->cell[i].info, msg->cell[i].sys_info);
  }
}
