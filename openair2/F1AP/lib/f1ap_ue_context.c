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

#include "f1ap_ue_context.h"

#include "f1ap_lib_common.h"
#include "f1ap_lib_includes.h"
#include "f1ap_messages_types.h"

#include "common/utils/assertions.h"
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"
#include "common/utils/ds/byte_array.h"

static F1AP_CUtoDURRCInformation_t encode_cu_to_du_rrc_info(const f1ap_cu_to_du_rrc_info_t *cu2du)
{
  F1AP_CUtoDURRCInformation_t enc = {0};

  /* optional: cG_ConfigInfo */
  if (cu2du->cg_configinfo) {
    asn1cCalloc(enc.cG_ConfigInfo, cG_ConfigInfo);
    const byte_array_t *ba = cu2du->cg_configinfo;
    OCTET_STRING_fromBuf(cG_ConfigInfo, (const char *)ba->buf, ba->len);
  }

  /* optional: uE_CapabilityRAT_ContainerList */
  if (cu2du->ue_cap) {
    asn1cCalloc(enc.uE_CapabilityRAT_ContainerList, uE_CapabilityRAT_ContainerList);
    const byte_array_t *ba = cu2du->ue_cap;
    OCTET_STRING_fromBuf(uE_CapabilityRAT_ContainerList, (const char *)ba->buf, ba->len);
  }

  /* optional: measConfig */
  if (cu2du->meas_config) {
    asn1cCalloc(enc.measConfig, measConfig);
    const byte_array_t *ba = cu2du->meas_config;
    OCTET_STRING_fromBuf(measConfig, (const char *)ba->buf, ba->len);
  }

  /* optional: HandoverPreparationInformation */
  if (cu2du->ho_prep_info) {
    F1AP_ProtocolExtensionContainer_10696P60_t *p = calloc_or_fail(1, sizeof(*p));
    enc.iE_Extensions = (struct F1AP_ProtocolExtensionContainer *)p;
    asn1cSequenceAdd(p->list, F1AP_CUtoDURRCInformation_ExtIEs_t, ie_ext);
    ie_ext->id = F1AP_ProtocolIE_ID_id_HandoverPreparationInformation;
    ie_ext->criticality = F1AP_Criticality_ignore;
    ie_ext->extensionValue.present = F1AP_CUtoDURRCInformation_ExtIEs__extensionValue_PR_HandoverPreparationInformation;
    const byte_array_t *ba = cu2du->ho_prep_info;
    F1AP_HandoverPreparationInformation_t *hpi = &ie_ext->extensionValue.choice.HandoverPreparationInformation;
    OCTET_STRING_fromBuf(hpi, (const char *)ba->buf, ba->len);
  }
  return enc;
}

static bool decode_cu_to_du_rrc_info(f1ap_cu_to_du_rrc_info_t *dec, const F1AP_CUtoDURRCInformation_t *cu2du)
{
  if (cu2du->cG_ConfigInfo) {
    dec->cg_configinfo = calloc_or_fail(1, sizeof(*dec->cg_configinfo));
    const F1AP_CG_ConfigInfo_t *cgci = cu2du->cG_ConfigInfo;
    *dec->cg_configinfo = create_byte_array(cgci->size, (uint8_t*)cgci->buf);
  }
  if (cu2du->uE_CapabilityRAT_ContainerList) {
    dec->ue_cap = calloc_or_fail(1, sizeof(*dec->ue_cap));
    const F1AP_UE_CapabilityRAT_ContainerList_t *l = cu2du->uE_CapabilityRAT_ContainerList;
    *dec->ue_cap = create_byte_array(l->size, (uint8_t*)l->buf);
  }
  if (cu2du->measConfig) {
    dec->meas_config = calloc_or_fail(1, sizeof(*dec->meas_config));
    const F1AP_MeasConfig_t *mc = cu2du->measConfig;
    *dec->meas_config = create_byte_array(mc->size, (uint8_t*)mc->buf);
  }
  if (cu2du->iE_Extensions) {
    const F1AP_ProtocolExtensionContainer_10696P60_t *ext = (const F1AP_ProtocolExtensionContainer_10696P60_t *)cu2du->iE_Extensions;
    for (int i = 0; i < ext->list.count; ++i) {
      const F1AP_CUtoDURRCInformation_ExtIEs_t *cu2du_info_ext = ext->list.array[i];
      switch (cu2du_info_ext->id) {
        case F1AP_ProtocolIE_ID_id_HandoverPreparationInformation:
          dec->ho_prep_info = calloc_or_fail(1, sizeof(*dec->ho_prep_info));
          const F1AP_HandoverPreparationInformation_t *hopi = &cu2du_info_ext->extensionValue.choice.HandoverPreparationInformation;
          *dec->ho_prep_info = create_byte_array(hopi->size, (uint8_t*) hopi->buf);
          break;
        default:
          PRINT_ERROR("received unsupported F1AP CUtoDURRCInfo extension container %ld\n", cu2du_info_ext->id);
      }
    }
  }
  return true;
}

static f1ap_cu_to_du_rrc_info_t cp_cu_to_du_rrc_info(const f1ap_cu_to_du_rrc_info_t *src)
{
  f1ap_cu_to_du_rrc_info_t dst = {0};
  CP_OPT_BYTE_ARRAY(dst.cg_configinfo, src->cg_configinfo);
  CP_OPT_BYTE_ARRAY(dst.ue_cap, src->ue_cap);
  CP_OPT_BYTE_ARRAY(dst.meas_config, src->meas_config);
  CP_OPT_BYTE_ARRAY(dst.ho_prep_info, src->ho_prep_info);
  return dst;
}

static bool eq_ba(const byte_array_t a, const byte_array_t b)
{
  return eq_byte_array(&a, &b);
}

static bool eq_cu_to_du_rrc_info(const f1ap_cu_to_du_rrc_info_t *a, const f1ap_cu_to_du_rrc_info_t *b)
{
  _F1_EQ_CHECK_OPTIONAL_IE(a, b, cg_configinfo, eq_ba);
  _F1_EQ_CHECK_OPTIONAL_IE(a, b, ue_cap, eq_ba);
  _F1_EQ_CHECK_OPTIONAL_IE(a, b, meas_config, eq_ba);
  _F1_EQ_CHECK_OPTIONAL_IE(a, b, ho_prep_info, eq_ba);
  return true;
}

static void free_cu_to_du_rrc_info(f1ap_cu_to_du_rrc_info_t *cu2du)
{
  FREE_OPT_BYTE_ARRAY(cu2du->cg_configinfo);
  FREE_OPT_BYTE_ARRAY(cu2du->ue_cap);
  FREE_OPT_BYTE_ARRAY(cu2du->meas_config);
  FREE_OPT_BYTE_ARRAY(cu2du->ho_prep_info);
}

/* \brief Encode SRB-ToBeSetup List, for UE Context Setup Request, from
 * f1ap_srb_to_setup_t. */
static F1AP_SRBs_ToBeSetup_List_t encode_srbs_to_setup(int n, const f1ap_srb_to_setup_t *srbs)
{
  F1AP_SRBs_ToBeSetup_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    asn1cSequenceAdd(list, F1AP_SRBs_ToBeSetup_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_SRBs_ToBeSetup_Item;
    itie->criticality = F1AP_Criticality_reject;
    itie->value.present = F1AP_SRBs_ToBeSetup_ItemIEs__value_PR_SRBs_ToBeSetup_Item;
    itie->value.choice.SRBs_ToBeSetup_Item.sRBID = srbs[i].id;
  }
  return list;
}

/* \brief Decode SRB-ToBeSetup List, from UE Context Setup Request, into
 * f1ap_srb_to_setup_t. */
static bool decode_srbs_to_setup(const F1AP_SRBs_ToBeSetup_List_t *f1ap, int *n, f1ap_srb_to_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_srb_to_setup_t *srbs = *out = calloc_or_fail(count, sizeof(*srbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_SRBs_ToBeSetup_ItemIEs_t *itie = (F1AP_SRBs_ToBeSetup_ItemIEs_t *)f1ap->list.array[i];
    _F1_EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_SRBs_ToBeSetup_Item);
    _F1_EQ_CHECK_INT(itie->value.present, F1AP_SRBs_ToBeSetup_ItemIEs__value_PR_SRBs_ToBeSetup_Item);
    srbs[i].id = itie->value.choice.SRBs_ToBeSetup_Item.sRBID;
  }

  return true;
}

static f1ap_srb_to_setup_t cp_srb_to_setup(const f1ap_srb_to_setup_t *orig)
{
  f1ap_srb_to_setup_t cp = {
    .id = orig->id,
  };
  return cp;
}

static bool eq_srb_to_setup(const f1ap_srb_to_setup_t *a, const f1ap_srb_to_setup_t *b)
{
  _F1_EQ_CHECK_INT(a->id, b->id);
  return true;
}

static void free_srb_to_setup(f1ap_srb_to_setup_t *srbs)
{
  // nothing to free
}

static F1AP_QoSFlowLevelQoSParameters_t encode_qos_flow_param(const f1ap_qos_flow_param_t *p)
{
  F1AP_QoSFlowLevelQoSParameters_t f1ap = {0};

  if (p->qos_type == NON_DYNAMIC) {
    f1ap.qoS_Characteristics.present = F1AP_QoS_Characteristics_PR_non_Dynamic_5QI;
    asn1cCalloc(f1ap.qoS_Characteristics.choice.non_Dynamic_5QI, nd);
    nd->fiveQI = p->nondyn.fiveQI;
  } else {
    DevAssert(p->qos_type == DYNAMIC);
    f1ap.qoS_Characteristics.present = F1AP_QoS_Characteristics_PR_dynamic_5QI;
    asn1cCalloc(f1ap.qoS_Characteristics.choice.dynamic_5QI, d);

    d->qoSPriorityLevel = p->dyn.prio;
    d->packetDelayBudget = p->dyn.pdb;
    d->packetErrorRate.pER_Scalar = p->dyn.per.scalar;
    d->packetErrorRate.pER_Exponent = p->dyn.per.exponent;

    if (p->dyn.delay_critical) {
      e_F1AP_Dynamic5QIDescriptor__delayCritical v = *p->dyn.delay_critical
                                                         ? F1AP_Dynamic5QIDescriptor__delayCritical_delay_critical
                                                         : F1AP_Dynamic5QIDescriptor__delayCritical_non_delay_critical;
      asn1cCallocOne(d->delayCritical, v);
    }

    if (p->dyn.avg_win)
      asn1cCallocOne(d->averagingWindow, *p->dyn.avg_win);
  }

  F1AP_NGRANAllocationAndRetentionPriority_t *arp = &f1ap.nGRANallocationRetentionPriority;
  arp->priorityLevel = p->arp.prio;
  arp->pre_emptionCapability = p->arp.preempt_cap;
  arp->pre_emptionVulnerability = p->arp.preempt_vuln;
  return f1ap;
}

static bool decode_qos_flow_param(const F1AP_QoSFlowLevelQoSParameters_t *f1ap, f1ap_qos_flow_param_t *out)
{
  if (f1ap->qoS_Characteristics.present == F1AP_QoS_Characteristics_PR_non_Dynamic_5QI) {
    out->qos_type = NON_DYNAMIC;
    const F1AP_NonDynamic5QIDescriptor_t *nondyn = f1ap->qoS_Characteristics.choice.non_Dynamic_5QI;
    out->nondyn.fiveQI = nondyn->fiveQI;
  } else {
    _F1_EQ_CHECK_INT(f1ap->qoS_Characteristics.present, F1AP_QoS_Characteristics_PR_dynamic_5QI);
    const F1AP_Dynamic5QIDescriptor_t *d = f1ap->qoS_Characteristics.choice.dynamic_5QI;
    out->qos_type = DYNAMIC;
    out->dyn.prio = d->qoSPriorityLevel;
    out->dyn.pdb = d->packetDelayBudget;
    out->dyn.per.scalar = d->packetErrorRate.pER_Scalar;
    out->dyn.per.exponent = d->packetErrorRate.pER_Exponent;

    if (d->delayCritical) {
      bool v = *d->delayCritical == F1AP_Dynamic5QIDescriptor__delayCritical_delay_critical;
      _F1_MALLOC(out->dyn.delay_critical, v);
    }

    if (d->averagingWindow)
      _F1_MALLOC(out->dyn.avg_win, *d->averagingWindow);
  }

  const F1AP_NGRANAllocationAndRetentionPriority_t *arp = &f1ap->nGRANallocationRetentionPriority;
  out->arp.prio = arp->priorityLevel;
  out->arp.preempt_cap = arp->pre_emptionCapability;
  out->arp.preempt_vuln = arp->pre_emptionVulnerability;
  return true;
}

static f1ap_qos_flow_param_t cp_qos_flow_param(const f1ap_qos_flow_param_t *orig)
{
  f1ap_qos_flow_param_t cp = {
    .qos_type = orig->qos_type,
    // dyn/nondyn below
    .arp = orig->arp,
  };
  if (cp.qos_type == NON_DYNAMIC) {
    cp.nondyn.fiveQI = orig->nondyn.fiveQI;
  } else {
    DevAssert(cp.qos_type == DYNAMIC);
    f1ap_dynamic_5qi_t *cpdyn = &cp.dyn;
    const f1ap_dynamic_5qi_t *odyn = &orig->dyn;
    cpdyn->prio = odyn->prio;
    cpdyn->pdb = odyn->pdb;
    cpdyn->per.scalar = odyn->per.scalar;
    cpdyn->per.exponent = odyn->per.exponent;
    if (odyn->delay_critical)
      _F1_MALLOC(cpdyn->delay_critical, *odyn->delay_critical);
    if (odyn->avg_win)
      _F1_MALLOC(cpdyn->avg_win, *odyn->avg_win);
  }
  return cp;
}

static bool eq_qos_flow_param(const f1ap_qos_flow_param_t *a, const f1ap_qos_flow_param_t *b)
{
  _F1_EQ_CHECK_INT(a->qos_type, b->qos_type);
  if (a->qos_type == NON_DYNAMIC) {
    _F1_EQ_CHECK_INT(a->nondyn.fiveQI, b->nondyn.fiveQI);
  } else {
    DevAssert(a->qos_type == DYNAMIC);
    DevAssert(b->qos_type == DYNAMIC);
    const f1ap_dynamic_5qi_t *adyn = &a->dyn, *bdyn = &b->dyn;
    _F1_EQ_CHECK_INT(adyn->prio, bdyn->prio);
    _F1_EQ_CHECK_INT(adyn->pdb, bdyn->pdb);
    _F1_EQ_CHECK_INT(adyn->per.scalar, bdyn->per.scalar);
    _F1_EQ_CHECK_INT(adyn->per.exponent, bdyn->per.exponent);
    _F1_EQ_CHECK_OPTIONAL_IE(adyn, bdyn, delay_critical, _F1_EQ_CHECK_INT);
    _F1_EQ_CHECK_OPTIONAL_IE(adyn, bdyn, avg_win, _F1_EQ_CHECK_INT);
  }
  _F1_EQ_CHECK_INT(a->arp.prio, b->arp.prio);
  _F1_EQ_CHECK_INT(a->arp.preempt_cap, b->arp.preempt_cap);
  _F1_EQ_CHECK_INT(a->arp.preempt_vuln, b->arp.preempt_vuln);
  return true;
}

static void free_qos_flow_param(f1ap_qos_flow_param_t *p)
{
  if (p->qos_type == NON_DYNAMIC) {
    // nothing to free
  } else {
    DevAssert(p->qos_type == DYNAMIC);
    free(p->dyn.delay_critical);
    free(p->dyn.avg_win);
  }
  // arp: nothing to free
}

static F1AP_DRB_Information_t encode_drb_info_nr(const f1ap_drb_info_nr_t *drb)
{
  F1AP_DRB_Information_t f1ap = {0};

  f1ap.dRB_QoS = encode_qos_flow_param(&drb->drb_qos);
  f1ap.sNSSAI = encode_nssai(&drb->nssai);

  for (int i = 0; i < drb->flows_len; ++i) {
    asn1cSequenceAdd(f1ap.flows_Mapped_To_DRB_List.list, F1AP_Flows_Mapped_To_DRB_Item_t, it);
    it->qoSFlowIdentifier = drb->flows[i].qfi;
    it->qoSFlowLevelQoSParameters = encode_qos_flow_param(&drb->flows[i].param);
  }

  return f1ap;
}

static bool decode_drb_info_nr(const F1AP_DRB_Information_t *f1ap, f1ap_drb_info_nr_t *out)
{
  _F1_CHECK_EXP(decode_qos_flow_param(&f1ap->dRB_QoS, &out->drb_qos));
  out->nssai = decode_nssai(&f1ap->sNSSAI);
  out->flows_len = f1ap->flows_Mapped_To_DRB_List.list.count;
  out->flows = calloc_or_fail(out->flows_len, sizeof(*out->flows));
  for (int i = 0; i < out->flows_len; ++i) {
    const F1AP_Flows_Mapped_To_DRB_Item_t *it = f1ap->flows_Mapped_To_DRB_List.list.array[i];
    out->flows[i].qfi = it->qoSFlowIdentifier;
    _F1_CHECK_EXP(decode_qos_flow_param(&it->qoSFlowLevelQoSParameters, &out->flows[i].param));
  }
  return true;
}

static f1ap_drb_info_nr_t cp_drb_info_nr(const f1ap_drb_info_nr_t *orig)
{
  f1ap_drb_info_nr_t cp = {
    .drb_qos = cp_qos_flow_param(&orig->drb_qos),
    .nssai = orig->nssai,
    .flows_len = orig->flows_len,
    // flows below
  };
  cp.flows = calloc_or_fail(cp.flows_len, sizeof(*cp.flows));
  for (int i = 0; i < cp.flows_len; ++i) {
    cp.flows[i].qfi = orig->flows[i].qfi;
    cp.flows[i].param = cp_qos_flow_param(&orig->flows[i].param);
  }
  return cp;
}

static bool eq_drb_info_nr(const f1ap_drb_info_nr_t *a, const f1ap_drb_info_nr_t *b)
{
  _F1_CHECK_EXP(eq_qos_flow_param(&a->drb_qos, &b->drb_qos));
  _F1_EQ_CHECK_INT(a->nssai.sst, b->nssai.sst);
  _F1_EQ_CHECK_INT(a->nssai.sd, b->nssai.sd);
  _F1_EQ_CHECK_INT(a->flows_len, b->flows_len);
  for (int i = 0; i < a->flows_len; ++i) {
    _F1_EQ_CHECK_INT(a->flows[i].qfi, b->flows[i].qfi);
    _F1_CHECK_EXP(eq_qos_flow_param(&a->flows[i].param, &b->flows[i].param));
  }
  return true;
}

static void free_drb_info_nr(f1ap_drb_info_nr_t *p)
{
  free_qos_flow_param(&p->drb_qos);
  // nssai: nothing
  for (int i = 0; i < p->flows_len; ++i)
    free_qos_flow_param(&p->flows[i].param);
  free(p->flows);
}

static F1AP_RLCMode_t rlc_mode_to_asn1(f1ap_rlc_mode_t mode)
{
  switch (mode) {
    case F1AP_RLC_MODE_AM:
      return F1AP_RLCMode_rlc_am;
    case F1AP_RLC_MODE_UM_BIDIR:
      return F1AP_RLCMode_rlc_um_bidirectional;
    default:
      break;
  }
  AssertFatal(false, "unsupported RLC Mode %d received\n", mode);
  return F1AP_RLCMode_rlc_am; // won't be reached
}

static bool rlc_mode_from_asn1(F1AP_RLCMode_t mode, f1ap_rlc_mode_t *out)
{
  switch (mode) {
    case F1AP_RLCMode_rlc_am:
      *out = F1AP_RLC_MODE_AM;
      return true;
    case F1AP_RLCMode_rlc_um_bidirectional:
      *out = F1AP_RLC_MODE_UM_BIDIR;
      return true;
    default:
      PRINT_ERROR("unsupported RLC Mode %ld received: setting AM\n", mode);
      break;
  }
  return false;
}

static F1AP_UPTransportLayerInformation_t encode_up_tnl(const f1ap_up_tnl_t *tnl)
{
  F1AP_UPTransportLayerInformation_t tnl_info = { .present = F1AP_UPTransportLayerInformation_PR_gTPTunnel, };
  asn1cCalloc(tnl_info.choice.gTPTunnel, gtp_tnl);
  TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(tnl->tl_address, &gtp_tnl->transportLayerAddress);
  INT32_TO_OCTET_STRING(tnl->teid, &gtp_tnl->gTP_TEID);
  return tnl_info;
}

static bool decode_up_tnl(const F1AP_UPTransportLayerInformation_t *tnl_info, f1ap_up_tnl_t *out)
{
  _F1_EQ_CHECK_INT(tnl_info->present, F1AP_UPTransportLayerInformation_PR_gTPTunnel);
  const F1AP_GTPTunnel_t *gtp_tnl = tnl_info->choice.gTPTunnel;
  BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&gtp_tnl->transportLayerAddress, out->tl_address);
  OCTET_STRING_TO_UINT32(&gtp_tnl->gTP_TEID, out->teid);
  return true;
}

static f1ap_up_tnl_t cp_up_tnl(const f1ap_up_tnl_t *orig)
{
  f1ap_up_tnl_t cp = { .teid = orig->teid, };
  memcpy(&cp.tl_address, &orig->tl_address, sizeof(orig->tl_address));
  return cp;
}

static bool eq_up_tnl(const f1ap_up_tnl_t *a, const f1ap_up_tnl_t *b)
{
  if (memcmp(&a->tl_address, &b->tl_address, sizeof(a->tl_address)) != 0) {
    PRINT_ERROR("tl_address mismatch\n");
    return false;
  }
  _F1_EQ_CHECK_INT(a->teid, b->teid);
  return true;
}

static void free_up_tnl(const f1ap_up_tnl_t *tnl)
{
  // nothing to free
}

/* Encode a DRBs_ToSetup list for UE Context Setup Request, from f1ap_drb_to_setup_t. */
static F1AP_DRBs_ToBeSetup_List_t encode_drbs_to_setup(int n, const f1ap_drb_to_setup_t *drbs)
{
  F1AP_DRBs_ToBeSetup_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    const f1ap_drb_to_setup_t *drb = &drbs[i];
    asn1cSequenceAdd(list, F1AP_DRBs_ToBeSetup_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_DRBs_ToBeSetup_Item;
    itie->criticality = F1AP_Criticality_reject;
    itie->value.present = F1AP_DRBs_ToBeSetup_ItemIEs__value_PR_DRBs_ToBeSetup_Item;

    F1AP_DRBs_ToBeSetup_Item_t *it = &itie->value.choice.DRBs_ToBeSetup_Item;
    it->dRBID = drb->id;
    AssertFatal(drb->qos_choice == F1AP_QOS_CHOICE_NR, "only NR QoS choice implemented\n");
    it->qoSInformation.present = F1AP_QoSInformation_PR_choice_extension;
    F1AP_QoSInformation_ExtIEs_t *qos_ext_ie = calloc_or_fail(1, sizeof(*qos_ext_ie));
    it->qoSInformation.choice.choice_extension = (struct F1AP_ProtocolIE_SingleContainer *)qos_ext_ie;
    qos_ext_ie->id = F1AP_ProtocolIE_ID_id_DRB_Information;
    qos_ext_ie->criticality = F1AP_Criticality_reject;
    qos_ext_ie->value.present = F1AP_QoSInformation_ExtIEs__value_PR_DRB_Information;
    qos_ext_ie->value.choice.DRB_Information = encode_drb_info_nr(&drb->nr);

    /* 12.1.3 uLUPTNLInformation_ToBeSetup_List */
    for (int j = 0; j < drb->up_ul_tnl_len; j++) {
      DevAssert(drb->up_ul_tnl[j].teid > 0);
      asn1cSequenceAdd(it->uLUPTNLInformation_ToBeSetup_List.list, F1AP_ULUPTNLInformation_ToBeSetup_Item_t, tnl_it);
      tnl_it->uLUPTNLInformation = encode_up_tnl(&drb->up_ul_tnl[j]);
    }

    it->rLCMode = rlc_mode_to_asn1(drb->rlc_mode);

    F1AP_ProtocolExtensionContainer_10696P82_t *ext = calloc_or_fail(1, sizeof(*ext));
    it->iE_Extensions = (struct F1AP_ProtocolExtensionContainer *)ext;
    asn1cSequenceAdd(ext->list, F1AP_DRBs_ToBeSetup_ItemExtIEs_t, ext_ie);
    ext_ie->id = F1AP_ProtocolIE_ID_id_DLPDCPSNLength;
    ext_ie->criticality = F1AP_Criticality_ignore;
    ext_ie->extensionValue.present = F1AP_DRBs_ToBeSetup_ItemExtIEs__extensionValue_PR_PDCPSNLength;
    AssertFatal(drb->dl_pdcp_sn_len != NULL, "dl_pdcp_sn_len required for setup\n");
    ext_ie->extensionValue.choice.PDCPSNLength = *drb->dl_pdcp_sn_len;

    if (drb->ul_pdcp_sn_len) {
      asn1cSequenceAdd(ext->list, F1AP_DRBs_ToBeSetup_ItemExtIEs_t, ext_ie);
      ext_ie->id = F1AP_ProtocolIE_ID_id_ULPDCPSNLength;
      ext_ie->criticality = F1AP_Criticality_ignore;
      ext_ie->extensionValue.present = F1AP_DRBs_ToBeSetup_ItemExtIEs__extensionValue_PR_PDCPSNLength_1;
      ext_ie->extensionValue.choice.PDCPSNLength_1 = *drb->ul_pdcp_sn_len;
    }
  }
  return list;
}

/* \brief Decode DRBS-ToBeSetup list from UE Context Setup request, into
 * f1ap_drb_to_setup_t. */
static bool decode_drbs_to_setup(const F1AP_DRBs_ToBeSetup_List_t *f1ap, int *n, f1ap_drb_to_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_drb_to_setup_t *drbs = *out = calloc_or_fail(count, sizeof(*drbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_DRBs_ToBeSetup_ItemIEs_t *itie = (const F1AP_DRBs_ToBeSetup_ItemIEs_t *)f1ap->list.array[i];
    _F1_EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_DRBs_ToBeSetup_Item);
    _F1_EQ_CHECK_INT(itie->value.present, F1AP_DRBs_ToBeSetup_ItemIEs__value_PR_DRBs_ToBeSetup_Item);
    const F1AP_DRBs_ToBeSetup_Item_t *it = &itie->value.choice.DRBs_ToBeSetup_Item;

    f1ap_drb_to_setup_t *drb = &drbs[i];
    drb->id = it->dRBID;

    _F1_EQ_CHECK_INT(it->qoSInformation.present, F1AP_QoSInformation_PR_choice_extension);
    const F1AP_QoSInformation_ExtIEs_t *qos_ext = (F1AP_QoSInformation_ExtIEs_t *)it->qoSInformation.choice.choice_extension;
    _F1_EQ_CHECK_LONG(qos_ext->id, F1AP_ProtocolIE_ID_id_DRB_Information);
    _F1_EQ_CHECK_INT(qos_ext->value.present, F1AP_QoSInformation_ExtIEs__value_PR_DRB_Information);
    drb->qos_choice = F1AP_QOS_CHOICE_NR;
    _F1_CHECK_EXP(decode_drb_info_nr(&qos_ext->value.choice.DRB_Information, &drb->nr));

    drb->up_ul_tnl_len = it->uLUPTNLInformation_ToBeSetup_List.list.count;
    _F1_EQ_CHECK_GENERIC(drb->up_ul_tnl_len > 0 && drb->up_ul_tnl_len <= 2, "%d", drb->up_ul_tnl_len, 1);
    for (int j = 0; j < drb->up_ul_tnl_len; ++j) {
      const F1AP_ULUPTNLInformation_ToBeSetup_Item_t *it_tnl = it->uLUPTNLInformation_ToBeSetup_List.list.array[j];
      _F1_CHECK_EXP(decode_up_tnl(&it_tnl->uLUPTNLInformation, &drb->up_ul_tnl[j]));
    }

    _F1_CHECK_EXP(rlc_mode_from_asn1(it->rLCMode, &drb->rlc_mode));

    _F1_CHECK_EXP(it->iE_Extensions); // PDCP SN length is under extension, is mandatory
    const F1AP_ProtocolExtensionContainer_10696P82_t *ext = (const F1AP_ProtocolExtensionContainer_10696P82_t *)it->iE_Extensions;
    const F1AP_DRBs_ToBeSetup_ItemExtIEs_t *ie;
    F1AP_LIB_FIND_IE(F1AP_DRBs_ToBeSetup_ItemExtIEs_t, ie, &ext->list, F1AP_ProtocolIE_ID_id_DLPDCPSNLength, true);

    for (int j = 0; j < ext->list.count; ++j) {
      ie = ext->list.array[j];
      AssertError(ie != NULL, return false, "ext->list.array[j] is NULL");
      switch (ie->id) {
        case F1AP_ProtocolIE_ID_id_DLPDCPSNLength:
          _F1_EQ_CHECK_INT(ie->extensionValue.present, F1AP_DRBs_ToBeSetup_ItemExtIEs__extensionValue_PR_PDCPSNLength);
          _F1_MALLOC(drb->dl_pdcp_sn_len, ie->extensionValue.choice.PDCPSNLength);
          break;
        case F1AP_ProtocolIE_ID_id_ULPDCPSNLength:
          _F1_EQ_CHECK_INT(ie->extensionValue.present, F1AP_DRBs_ToBeSetup_ItemExtIEs__extensionValue_PR_PDCPSNLength_1);
          _F1_MALLOC(drb->ul_pdcp_sn_len, ie->extensionValue.choice.PDCPSNLength_1);
          break;
        default:
          PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
          break;
      }
    }
  }

  return true;
}

static f1ap_drb_to_setup_t cp_drb_to_setup(const f1ap_drb_to_setup_t *orig)
{
  f1ap_drb_to_setup_t cp = {
    .id = orig->id,
    .qos_choice = orig->qos_choice,
    // nr below
    .up_ul_tnl_len = orig->up_ul_tnl_len,
    .up_ul_tnl[0] = cp_up_tnl(&orig->up_ul_tnl[0]),
    .up_ul_tnl[1] = cp_up_tnl(&orig->up_ul_tnl[1]),
    .rlc_mode = orig->rlc_mode,
    // dl_pdcp_sn_len below
    // ul_pdcp_sn_len below
  };
  AssertFatal(cp.qos_choice == F1AP_QOS_CHOICE_NR, "only NR QoS choice implemented\n");
  if (orig->qos_choice == F1AP_QOS_CHOICE_NR)
    cp.nr = cp_drb_info_nr(&orig->nr);
  if (orig->dl_pdcp_sn_len) {
    _F1_MALLOC(cp.dl_pdcp_sn_len, *orig->dl_pdcp_sn_len);
  }
  if (orig->ul_pdcp_sn_len) {
    _F1_MALLOC(cp.ul_pdcp_sn_len, *orig->ul_pdcp_sn_len);
  }
  return cp;
}

static bool eq_drb_to_setup(const f1ap_drb_to_setup_t *a, const f1ap_drb_to_setup_t *b)
{
  _F1_EQ_CHECK_INT(a->id, b->id);
  _F1_EQ_CHECK_INT(a->qos_choice, b->qos_choice);
  AssertFatal(a->qos_choice == F1AP_QOS_CHOICE_NR, "only NR QoS choice implemented\n");
  if (a->qos_choice == F1AP_QOS_CHOICE_NR)
    eq_drb_info_nr(&a->nr, &b->nr);
  _F1_EQ_CHECK_INT(a->up_ul_tnl_len, b->up_ul_tnl_len);
  for (int i = 0; i < a->up_ul_tnl_len; ++i) {
    _F1_CHECK_EXP(eq_up_tnl(&a->up_ul_tnl[i], &b->up_ul_tnl[i]));
  }
  _F1_EQ_CHECK_INT(a->rlc_mode, b->rlc_mode);
  _F1_EQ_CHECK_OPTIONAL_IE(a, b, dl_pdcp_sn_len, _F1_EQ_CHECK_INT);
  _F1_EQ_CHECK_OPTIONAL_IE(a, b, ul_pdcp_sn_len, _F1_EQ_CHECK_INT);
  return true;
}

static void free_drb_to_setup(f1ap_drb_to_setup_t *drb)
{
  if (drb->qos_choice == F1AP_QOS_CHOICE_NR)
    free_drb_info_nr(&drb->nr);
  for (int i = 0; i < drb->up_ul_tnl_len; ++i)
    free_up_tnl(&drb->up_ul_tnl[i]);
  free(drb->dl_pdcp_sn_len);
  free(drb->ul_pdcp_sn_len);
}

/**
 * @brief Encode F1 UE context setup request to ASN.1
 */
F1AP_F1AP_PDU_t *encode_ue_context_setup_req(const f1ap_ue_context_setup_req_t *req)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextSetup;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_InitiatingMessage__value_PR_UEContextSetupRequest;
  F1AP_UEContextSetupRequest_t *out = &tmp->value.choice.UEContextSetupRequest;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextSetupRequestIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = req->gNB_CU_ue_id;

  /* optional: GNB_DU_UE_F1AP_ID */
  if (req->gNB_DU_ue_id) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_GNB_DU_UE_F1AP_ID;
    ie->value.choice.GNB_DU_UE_F1AP_ID = *req->gNB_DU_ue_id;
  }

  /* mandatory: SpCell ID/NRCGI */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie3);
  ie3->id = F1AP_ProtocolIE_ID_id_SpCell_ID;
  ie3->criticality = F1AP_Criticality_reject;
  ie3->value.present = F1AP_UEContextSetupRequestIEs__value_PR_NRCGI;
  F1AP_NRCGI_t *nrcgi = &ie3->value.choice.NRCGI;
  const plmn_id_t *plmn = &req->plmn;
  MCC_MNC_TO_PLMNID(plmn->mcc,plmn->mnc,plmn->mnc_digit_length, &nrcgi->pLMN_Identity);
  NR_CELL_ID_TO_BIT_STRING(req->nr_cellid, &nrcgi->nRCellIdentity);

  /* mandatory: ServCellIndox */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie4);
  ie4->id = F1AP_ProtocolIE_ID_id_ServCellIndex;
  ie4->criticality = F1AP_Criticality_reject;
  ie4->value.present = F1AP_UEContextSetupRequestIEs__value_PR_ServCellIndex;
  ie4->value.choice.ServCellIndex = req->servCellIndex;

  /* mandatory: CUtoDURRCInformation */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie6);
  ie6->id = F1AP_ProtocolIE_ID_id_CUtoDURRCInformation;
  ie6->criticality = F1AP_Criticality_reject;
  ie6->value.present = F1AP_UEContextSetupRequestIEs__value_PR_CUtoDURRCInformation;
  ie6->value.choice.CUtoDURRCInformation = encode_cu_to_du_rrc_info(&req->cu_to_du_rrc_info);

  /* optional: SRB to be setup list */
  if (req->srbs_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_SRBs_ToBeSetup_List;
    ie->criticality = F1AP_Criticality_reject;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_SRBs_ToBeSetup_List;
    ie->value.choice.SRBs_ToBeSetup_List = encode_srbs_to_setup(req->srbs_len, req->srbs);
  }

  /* optional: DRB to be setup list */
  if (req->drbs_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_DRBs_ToBeSetup_List;
    ie->criticality = F1AP_Criticality_reject;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_DRBs_ToBeSetup_List;
    ie->value.choice.DRBs_ToBeSetup_List = encode_drbs_to_setup(req->drbs_len, req->drbs);
  }

  /* optional: RRC container */
  if (req->rrc_container) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_RRCContainer;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_RRCContainer;
    OCTET_STRING_fromBuf(&ie->value.choice.RRCContainer, (const char *)req->rrc_container->buf, req->rrc_container->len);
  }

  /* conditional if DRB setup list: gNB-DU UE Aggregate Maximum Bit Rate Uplink */
  if (req->gnb_du_ue_agg_mbr_ul) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_GNB_DU_UE_AMBR_UL;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_BitRate;
    asn_long2INTEGER(&ie->value.choice.BitRate, *req->gnb_du_ue_agg_mbr_ul);
  }

  return pdu;
}

/**
 * @brief Decode F1 UE Context Setup Request
 */
bool decode_ue_context_setup_req(const F1AP_F1AP_PDU_t *pdu, f1ap_ue_context_setup_req_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_UEContextSetupRequest_t *in = &pdu->choice.initiatingMessage->value.choice.UEContextSetupRequest;
  F1AP_UEContextSetupRequestIEs_t *ie;

  F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_SpCell_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_ServCellIndex, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_CUtoDURRCInformation, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_GNB_DU_UE_F1AP_ID);
        _F1_MALLOC(out->gNB_DU_ue_id, ie->value.choice.GNB_DU_UE_F1AP_ID);
        break;
      case F1AP_ProtocolIE_ID_id_SpCell_ID: {
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_NRCGI);
        const F1AP_NRCGI_t *nrcgi = &ie->value.choice.NRCGI;
        PLMNID_TO_MCC_MNC(&nrcgi->pLMN_Identity, out->plmn.mcc, out->plmn.mnc, out->plmn.mnc_digit_length);
        BIT_STRING_TO_NR_CELL_IDENTITY(&nrcgi->nRCellIdentity, out->nr_cellid);
        } break;
      case F1AP_ProtocolIE_ID_id_ServCellIndex:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_ServCellIndex);
        out->servCellIndex = ie->value.choice.ServCellIndex;
        break;
      case F1AP_ProtocolIE_ID_id_CUtoDURRCInformation:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_CUtoDURRCInformation);
        _F1_CHECK_EXP(decode_cu_to_du_rrc_info(&out->cu_to_du_rrc_info, &ie->value.choice.CUtoDURRCInformation));
        break;
      case F1AP_ProtocolIE_ID_id_SRBs_ToBeSetup_List:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_SRBs_ToBeSetup_List);
        _F1_CHECK_EXP(decode_srbs_to_setup(&ie->value.choice.SRBs_ToBeSetup_List, &out->srbs_len, &out->srbs));
        break;
      case F1AP_ProtocolIE_ID_id_DRBs_ToBeSetup_List:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_DRBs_ToBeSetup_List);
        {
          // gNB-DU UE Aggregate Maximum Bit Rate Uplink is C-ifDRBSetup
          // below, check that gNB-DU UE AMBR UL IE is present if we have DRB
          // setup, as required by the spec, or return false otherwise (like in
          // the beginning of the function)
          F1AP_UEContextSetupRequestIEs_t *check_ie;
          F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, check_ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_GNB_DU_UE_AMBR_UL, true);
        }
        _F1_CHECK_EXP(decode_drbs_to_setup(&ie->value.choice.DRBs_ToBeSetup_List, &out->drbs_len, &out->drbs));
        break;
      case F1AP_ProtocolIE_ID_id_RRCContainer:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_RRCContainer);
        out->rrc_container = malloc_or_fail(sizeof(*out->rrc_container));
        {
          OCTET_STRING_t *os = &ie->value.choice.RRCContainer;
          *out->rrc_container = create_byte_array(os->size, os->buf);
        }
        break;
      case F1AP_ProtocolIE_ID_id_GNB_DU_UE_AMBR_UL:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_BitRate);
        out->gnb_du_ue_agg_mbr_ul = malloc_or_fail(sizeof(*out->gnb_du_ue_agg_mbr_ul));
        asn_INTEGER2uint64(&ie->value.choice.BitRate, out->gnb_du_ue_agg_mbr_ul);
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 UE Context Setup Request deep copy
 */
f1ap_ue_context_setup_req_t cp_ue_context_setup_req(const f1ap_ue_context_setup_req_t *orig)
{
  /* copy all mandatory fields that are not dynamic memory */
  f1ap_ue_context_setup_req_t cp = {
    .gNB_CU_ue_id = orig->gNB_CU_ue_id,
    .plmn = orig->plmn,
    .nr_cellid = orig->nr_cellid,
    .servCellIndex = orig->servCellIndex,
    .cu_to_du_rrc_info = cp_cu_to_du_rrc_info(&orig->cu_to_du_rrc_info),
  };
  if (orig->gNB_DU_ue_id)
    _F1_MALLOC(cp.gNB_DU_ue_id, *orig->gNB_DU_ue_id);
  if (orig->srbs_len > 0 && orig->srbs) {
    cp.srbs = calloc_or_fail(orig->srbs_len, sizeof(*cp.srbs));
    cp.srbs_len = orig->srbs_len;
    for (int i = 0; i < cp.srbs_len; ++i)
      cp.srbs[i] = cp_srb_to_setup(&orig->srbs[i]);
  }
  if (orig->drbs_len > 0 && orig->drbs) {
    cp.drbs = calloc_or_fail(orig->drbs_len, sizeof(*cp.drbs));
    cp.drbs_len = orig->drbs_len;
    for (int i = 0; i < cp.drbs_len; ++i)
      cp.drbs[i] = cp_drb_to_setup(&orig->drbs[i]);
  }
  CP_OPT_BYTE_ARRAY(cp.rrc_container, orig->rrc_container);
  if (orig->gnb_du_ue_agg_mbr_ul)
    _F1_MALLOC(cp.gnb_du_ue_agg_mbr_ul, *orig->gnb_du_ue_agg_mbr_ul);
  return cp;
}

/**
 * @brief F1 UE Context Setup Request equality check
 */
bool eq_ue_context_setup_req(const f1ap_ue_context_setup_req_t *a, const f1ap_ue_context_setup_req_t *b)
{
  _F1_EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _F1_EQ_CHECK_OPTIONAL_IE(a, b, gNB_DU_ue_id, _F1_EQ_CHECK_INT);
  _F1_CHECK_EXP(eq_f1ap_plmn(&a->plmn, &b->plmn));
  _F1_EQ_CHECK_LONG(a->nr_cellid, b->nr_cellid);
  _F1_EQ_CHECK_INT(a->servCellIndex, b->servCellIndex);
  _F1_CHECK_EXP(eq_cu_to_du_rrc_info(&a->cu_to_du_rrc_info, &b->cu_to_du_rrc_info));

  _F1_EQ_CHECK_INT(a->srbs_len, b->srbs_len);
  _F1_CHECK_EXP(a->srbs_len == 0 || (a->srbs && b->srbs));
  for (int i = 0; i < a->srbs_len; ++i)
    _F1_CHECK_EXP(eq_srb_to_setup(&a->srbs[i], &b->srbs[i]));

  _F1_EQ_CHECK_INT(a->drbs_len, b->drbs_len);
  _F1_CHECK_EXP(a->drbs_len == 0 || (a->drbs && b->drbs));
  for (int i = 0; i < a->drbs_len; ++i)
    _F1_CHECK_EXP(eq_drb_to_setup(&a->drbs[i], &b->drbs[i]));

  _F1_EQ_CHECK_OPTIONAL_IE(a, b, rrc_container, eq_ba);

  _F1_EQ_CHECK_OPTIONAL_IE(a, b, gnb_du_ue_agg_mbr_ul, _F1_EQ_CHECK_LONG);
  return true;
}

/**
 * @brief Free Allocated F1 UE Context Setup Request
 */
void free_ue_context_setup_req(f1ap_ue_context_setup_req_t *req)
{
  free(req->gNB_DU_ue_id);
  free_cu_to_du_rrc_info(&req->cu_to_du_rrc_info);
  for (int i = 0; i < req->srbs_len; ++i)
    free_srb_to_setup(&req->srbs[i]);
  free(req->srbs);
  for (int i = 0; i < req->drbs_len; ++i)
    free_drb_to_setup(&req->drbs[i]);
  free(req->drbs);
  FREE_OPT_BYTE_ARRAY(req->rrc_container);
  free(req->gnb_du_ue_agg_mbr_ul);
}
