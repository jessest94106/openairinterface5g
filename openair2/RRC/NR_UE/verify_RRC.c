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
 
 #include "rrc_proto.h"
 
static bool check_resourcesForInterference(const NR_CSI_MeasConfig_t *meas, NR_CSI_ResourceConfigId_t res_id, bool is_CSIIM)
{
  NR_CSI_ResourceConfig_t *res = NULL;
  if (meas->csi_ResourceConfigToAddModList) {
    for (int i = 0; i < meas->csi_ResourceConfigToAddModList->list.count; i++) {
      if (meas->csi_ResourceConfigToAddModList->list.array[i]->csi_ResourceConfigId == res_id)
        res = meas->csi_ResourceConfigToAddModList->list.array[i];
    }
  }
  if (!res) {
    LOG_E(NR_RRC, "No CSI-Resource matching with resourcesForInterference set in report config\n");
    return false;
  } else {
    if (is_CSIIM) {
      if (res->csi_RS_ResourceSetList.present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_csi_IM_ResourceSetList) {
        LOG_E(NR_RRC, "resourcesForInterference doesn't point to CSI-IM resource\n");
        return false;
      }
    } else {
      if (res->csi_RS_ResourceSetList.present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_nzp_CSI_RS_SSB
          || !res->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList) {
        LOG_E(NR_RRC, "NZP CSI-RS resourcesForInterference doesn't point to NZP CSI-RS resource\n");
        return false;
      }
    }
  }
  return true;
}

static bool check_resourcesForChannelMeasurement(const NR_CSI_MeasConfig_t *meas,
                                                 NR_CSI_ResourceConfigId_t res_id,
                                                 struct NR_CSI_ReportConfig__reportQuantity quantity)
{
  NR_CSI_ResourceConfig_t *res = NULL;
  if (meas->csi_ResourceConfigToAddModList) {
    for (int i = 0; i < meas->csi_ResourceConfigToAddModList->list.count; i++) {
      if (meas->csi_ResourceConfigToAddModList->list.array[i]->csi_ResourceConfigId == res_id)
        res = meas->csi_ResourceConfigToAddModList->list.array[i];
    }
  }
  if (!res) {
    LOG_E(NR_RRC, "No CSI-Resource matching with resourcesForChannelMeasurement set in report config\n");
    return false;
  } else {
    if (res->csi_RS_ResourceSetList.present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_nzp_CSI_RS_SSB) {
      LOG_E(NR_RRC, "resourcesForChannelMeasurement doesn't point to CSI-RS-SSB resource\n");
      return false;
    } else {
      if (!res->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList
          && (quantity.present != NR_CSI_ReportConfig__reportQuantity_PR_ssb_Index_RSRP
          && quantity.present != NR_CSI_ReportConfig__reportQuantity_PR_NOTHING
          && quantity.present != NR_CSI_ReportConfig__reportQuantity_PR_none)) {
        LOG_E(NR_RRC, "resourcesForChannelMeasurement is for CRI measurement but doen't point to an CRI-RS resource\n");
        return false;
      }
    }
  }
  return true;
}

static bool check_csi_report_consistency(const NR_CSI_MeasConfig_t *meas)
{
  if (!meas->csi_ReportConfigToAddModList)
    return true;
  for (int i = 0; i < meas->csi_ReportConfigToAddModList->list.count; i++) {
    NR_CSI_ReportConfig_t *csirep = meas->csi_ReportConfigToAddModList->list.array[i];
    if (!check_resourcesForChannelMeasurement(meas, csirep->resourcesForChannelMeasurement, csirep->reportQuantity))
      return false;
    if (csirep->csi_IM_ResourcesForInterference) {
      if (!check_resourcesForInterference(meas, *csirep->csi_IM_ResourcesForInterference, true))
        return false;
    }
    if (csirep->nzp_CSI_RS_ResourcesForInterference) {
      if (!check_resourcesForInterference(meas, *csirep->nzp_CSI_RS_ResourcesForInterference, false))
        return false;
    }
  }
  return true;
}

bool check_cellgroup_config(const NR_CellGroupConfig_t *cgConfig)
{
  if (cgConfig->spCellConfig &&
      cgConfig->spCellConfig->spCellConfigDedicated &&
      cgConfig->spCellConfig->spCellConfigDedicated->csi_MeasConfig) {
    if (!check_csi_report_consistency(cgConfig->spCellConfig->spCellConfigDedicated->csi_MeasConfig->choice.setup))
      return false;
  }

  if (cgConfig->ext1 && cgConfig->ext1->reportUplinkTxDirectCurrent) {
    LOG_E(NR_RRC, "Reporting of UplinkTxDirectCurrent not implemented\n");
    return false;
  }
  if (cgConfig->sCellToReleaseList) {
    LOG_E(NR_RRC, "Secondary serving cell release not implemented\n");
    return false;
  }
  if (cgConfig->sCellToAddModList) {
    LOG_E(NR_RRC, "Secondary serving cell addition not implemented\n");
    return false;
  }
  return true;
}
