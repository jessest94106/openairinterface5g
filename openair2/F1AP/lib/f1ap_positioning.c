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

static F1AP_SRSConfig_t encode_srs_config(const f1ap_srs_config_t *in_config)
{
  F1AP_SRSConfig_t out_config = {0};
  // optional: srs_resource_list
  if (in_config->srs_resource_list) {
    f1ap_srs_resource_list_t *srs_resource_list = in_config->srs_resource_list;
    uint32_t srs_resource_list_length = srs_resource_list->srs_resource_list_length;
    asn1cCalloc(out_config.sRSResource_List, f1_sRSResource_List);
    for (int i = 0; i < srs_resource_list_length; i++) {
      asn1cSequenceAdd(f1_sRSResource_List->list, F1AP_SRSResource_t, f1_srs_resource);
      f1ap_srs_resource_t *srs_resource = &srs_resource_list->srs_resource[i];
      f1_srs_resource->sRSResourceID = srs_resource->srs_resource_id;

      switch (srs_resource->nr_of_srs_ports) {
        case F1AP_SRS_NUMBER_OF_PORTS_N1:
          f1_srs_resource->nrofSRS_Ports = F1AP_SRSResource__nrofSRS_Ports_port1;
          break;
        case F1AP_SRS_NUMBER_OF_PORTS_N2:
          f1_srs_resource->nrofSRS_Ports = F1AP_SRSResource__nrofSRS_Ports_ports2;
          break;
        case F1AP_SRS_NUMBER_OF_PORTS_N4:
          f1_srs_resource->nrofSRS_Ports = F1AP_SRSResource__nrofSRS_Ports_ports4;
          break;
        default:
          AssertFatal(false, "illegal number of srs ports %d\n", srs_resource->nr_of_srs_ports);
          break;
      }

      f1ap_transmission_comb_t *srs_res_tx_comb = &srs_resource->transmission_comb;
      switch (srs_res_tx_comb->present) {
        case F1AP_TRANSMISSION_COMB_PR_NOTHING:
          f1_srs_resource->transmissionComb.present = F1AP_TransmissionComb_PR_NOTHING;
          break;
        case F1AP_TRANSMISSION_COMB_PR_N2:
          f1_srs_resource->transmissionComb.present = F1AP_TransmissionComb_PR_n2;
          asn1cCalloc(f1_srs_resource->transmissionComb.choice.n2, f1_n2);
          f1_n2->combOffset_n2 = srs_res_tx_comb->choice.n2.comb_offset_n2;
          f1_n2->cyclicShift_n2 = srs_res_tx_comb->choice.n2.cyclic_shift_n2;
          break;
        case F1AP_TRANSMISSION_COMB_PR_N4:
          f1_srs_resource->transmissionComb.present = F1AP_TransmissionComb_PR_n4;
          asn1cCalloc(f1_srs_resource->transmissionComb.choice.n4, f1_n4);
          f1_n4->combOffset_n4 = srs_res_tx_comb->choice.n4.comb_offset_n4;
          f1_n4->cyclicShift_n4 = srs_res_tx_comb->choice.n4.cyclic_shift_n4;
          break;
        default:
          AssertFatal(false, "illegal transmissionComb %d\n", srs_res_tx_comb->present);
          break;
      }

      f1_srs_resource->startPosition = srs_resource->start_position;

      switch (srs_resource->nr_of_symbols) {
        case F1AP_SRS_NUMBER_OF_SYMBOLS_N1:
          f1_srs_resource->nrofSymbols = F1AP_SRSResource__nrofSymbols_n1;
          break;
        case F1AP_SRS_NUMBER_OF_SYMBOLS_N2:
          f1_srs_resource->nrofSymbols = F1AP_SRSResource__nrofSymbols_n2;
          break;
        case F1AP_SRS_NUMBER_OF_SYMBOLS_N4:
          f1_srs_resource->nrofSymbols = F1AP_SRSResource__nrofSymbols_n4;
          break;
        default:
          AssertFatal(false, "illegal number of symbols %d\n", srs_resource->nr_of_symbols);
          break;
      }

      switch (srs_resource->repetition_factor) {
        case F1AP_SRS_REPETITION_FACTOR_RF1:
          f1_srs_resource->repetitionFactor = F1AP_SRSResource__repetitionFactor_n1;
          break;
        case F1AP_SRS_REPETITION_FACTOR_RF2:
          f1_srs_resource->repetitionFactor = F1AP_SRSResource__repetitionFactor_n2;
          break;
        case F1AP_SRS_REPETITION_FACTOR_RF4:
          f1_srs_resource->repetitionFactor = F1AP_SRSResource__repetitionFactor_n4;
          break;
        default:
          AssertFatal(false, "illegal repetition factor %d\n", srs_resource->repetition_factor);
          break;
      }

      f1_srs_resource->freqDomainPosition = srs_resource->freq_domain_position;
      f1_srs_resource->freqDomainShift = srs_resource->freq_domain_shift;
      f1_srs_resource->c_SRS = srs_resource->c_srs;
      f1_srs_resource->b_SRS = srs_resource->b_srs;
      f1_srs_resource->b_hop = srs_resource->b_hop;

      switch (srs_resource->group_or_sequence_hopping) {
        case F1AP_GROUPORSEQUENCEHOPPING_NOTHING:
          f1_srs_resource->groupOrSequenceHopping = F1AP_SRSResource__groupOrSequenceHopping_neither;
          break;
        case F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING:
          f1_srs_resource->groupOrSequenceHopping = F1AP_SRSResource__groupOrSequenceHopping_groupHopping;
          break;
        case F1AP_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING:
          f1_srs_resource->groupOrSequenceHopping = F1AP_SRSResource__groupOrSequenceHopping_sequenceHopping;
          break;
        default:
          AssertFatal(false, "illegal groupOrSequenceHopping %d\n", srs_resource->group_or_sequence_hopping);
          break;
      }

      F1AP_ResourceType_t *f1_resourceType = &f1_srs_resource->resourceType;
      f1ap_resource_type_t *resource_type = &srs_resource->resource_type;
      if (resource_type->present == F1AP_RESOURCE_TYPE_PR_NOTHING) {
        f1_resourceType->present = F1AP_ResourceType_PR_NOTHING;
      } else if (resource_type->present == F1AP_RESOURCE_TYPE_PR_PERIODIC) {
        f1_resourceType->present = F1AP_ResourceType_PR_periodic;
        asn1cCalloc(f1_resourceType->choice.periodic, f1_periodic);
        switch (resource_type->choice.periodic.periodicity) {
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT1:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot1;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT2:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot2;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT4:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot4;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT5:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot5;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT8:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot8;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT10:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot10;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT16:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot16;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT20:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot20;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT32:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot32;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT40:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot40;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT64:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot64;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT80:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot80;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT160:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot160;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT320:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot320;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT640:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot640;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT1280:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot1280;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT2560:
            f1_periodic->periodicity = F1AP_ResourceTypePeriodic__periodicity_slot2560;
            break;
          default:
            AssertFatal(false, "illegal periodicity %d\n", resource_type->choice.periodic.periodicity);
            break;
        }
        f1_periodic->offset = resource_type->choice.periodic.offset;
      } else if (resource_type->present == F1AP_RESOURCE_TYPE_PR_SEMI_PERSISTENT) {
        f1_resourceType->present = F1AP_ResourceType_PR_semi_persistent;
        asn1cCalloc(f1_resourceType->choice.semi_persistent, f1_semi_persistent);
        switch (resource_type->choice.semi_persistent.periodicity) {
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT1:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot1;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT2:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot2;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT4:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot4;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT5:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot5;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT8:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot8;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT10:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot10;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT16:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot16;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT20:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot20;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT32:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot32;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT40:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot40;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT64:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot64;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT80:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot80;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT160:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot160;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT320:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot320;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT640:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot640;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT1280:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot1280;
            break;
          case F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT2560:
            f1_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistent__periodicity_slot2560;
            break;
          default:
            AssertFatal(false, "illegal periodicity %d\n", resource_type->choice.semi_persistent.periodicity);
            break;
        }
        f1_semi_persistent->offset = resource_type->choice.semi_persistent.offset;
      } else if (resource_type->present == F1AP_RESOURCE_TYPE_PR_APERIODIC) {
        f1_resourceType->present = F1AP_ResourceType_PR_aperiodic;
        asn1cCalloc(f1_resourceType->choice.aperiodic, f1_aperiodic);
        f1_aperiodic->aperiodicResourceType = resource_type->choice.aperiodic;
      } else {
        AssertFatal(false, "illegal resourceType %d\n", resource_type->present);
      }

      f1_srs_resource->sequenceId = srs_resource->sequence_id;
    }
  }

  // optional: pos_srs_resource_list
  if (in_config->pos_srs_resource_list) {
    f1ap_pos_srs_resource_list_t *pos_srs_resource_list = in_config->pos_srs_resource_list;
    uint32_t pos_srs_resource_list_length = pos_srs_resource_list->pos_srs_resource_list_length;
    asn1cCalloc(out_config.posSRSResource_List, f1_posSRSResource_List);
    for (int i = 0; i < pos_srs_resource_list_length; i++) {
      asn1cSequenceAdd(f1_posSRSResource_List->list, F1AP_PosSRSResource_Item_t, f1_pos_srs_resource);
      f1ap_pos_srs_resource_item_t *pos_srs_resource = &pos_srs_resource_list->pos_srs_resource_item[i];
      f1_pos_srs_resource->srs_PosResourceId = pos_srs_resource->srs_pos_resource_id;

      f1ap_transmission_comb_pos_t *pos_srs_res_tx_comb = &pos_srs_resource->transmission_comb_pos;
      switch (pos_srs_res_tx_comb->present) {
        case F1AP_TRANSMISSION_COMB_POS_PR_NOTHING:
          f1_pos_srs_resource->transmissionCombPos.present = F1AP_TransmissionCombPos_PR_NOTHING;
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N2:
          f1_pos_srs_resource->transmissionCombPos.present = F1AP_TransmissionCombPos_PR_n2;
          asn1cCalloc(f1_pos_srs_resource->transmissionCombPos.choice.n2, f1_pos_srs_resource_n2);
          f1ap_transmission_comb_pos_n2_t *pos_srs_resource_n2 = &pos_srs_res_tx_comb->choice.n2;
          f1_pos_srs_resource_n2->combOffset_n2 = pos_srs_resource_n2->comb_offset_n2;
          f1_pos_srs_resource_n2->cyclicShift_n2 = pos_srs_resource_n2->cyclic_shift_n2;
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N4:
          f1_pos_srs_resource->transmissionCombPos.present = F1AP_TransmissionCombPos_PR_n4;
          asn1cCalloc(f1_pos_srs_resource->transmissionCombPos.choice.n4, f1_pos_srs_resource_n4);
          f1ap_transmission_comb_pos_n4_t *pos_srs_resource_n4 = &pos_srs_res_tx_comb->choice.n4;
          f1_pos_srs_resource_n4->combOffset_n4 = pos_srs_resource_n4->comb_offset_n4;
          f1_pos_srs_resource_n4->cyclicShift_n4 = pos_srs_resource_n4->cyclic_shift_n4;
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N8:
          f1_pos_srs_resource->transmissionCombPos.present = F1AP_TransmissionCombPos_PR_n8;
          asn1cCalloc(f1_pos_srs_resource->transmissionCombPos.choice.n8, f1_pos_srs_resource_n8);
          f1ap_transmission_comb_pos_n8_t *pos_srs_resource_n8 = &pos_srs_res_tx_comb->choice.n8;
          f1_pos_srs_resource_n8->combOffset_n8 = pos_srs_resource_n8->comb_offset_n8;
          f1_pos_srs_resource_n8->cyclicShift_n8 = pos_srs_resource_n8->cyclic_shift_n8;
          break;
        default:
          AssertFatal(false, "illegal transmissionComb %d\n", pos_srs_res_tx_comb->present);
          break;
      }

      f1_pos_srs_resource->startPosition = pos_srs_resource->start_position;

      switch (pos_srs_resource->nr_of_symbols) {
        case F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N1:
          f1_pos_srs_resource->nrofSymbols = F1AP_PosSRSResource_Item__nrofSymbols_n1;
          break;
        case F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N2:
          f1_pos_srs_resource->nrofSymbols = F1AP_PosSRSResource_Item__nrofSymbols_n2;
          break;
        case F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N4:
          f1_pos_srs_resource->nrofSymbols = F1AP_PosSRSResource_Item__nrofSymbols_n4;
          break;
        case F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N8:
          f1_pos_srs_resource->nrofSymbols = F1AP_PosSRSResource_Item__nrofSymbols_n8;
          break;
        case F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N12:
          f1_pos_srs_resource->nrofSymbols = F1AP_PosSRSResource_Item__nrofSymbols_n12;
          break;
        default:
          AssertFatal(false, "illegal number of symbols %d\n", pos_srs_resource->nr_of_symbols);
          break;
      }

      f1_pos_srs_resource->freqDomainShift = pos_srs_resource->freq_domain_shift;
      f1_pos_srs_resource->c_SRS = pos_srs_resource->c_srs;

      switch (pos_srs_resource->group_or_sequence_hopping) {
        case F1AP_GROUPORSEQUENCEHOPPING_NOTHING:
          f1_pos_srs_resource->groupOrSequenceHopping = F1AP_PosSRSResource_Item__groupOrSequenceHopping_neither;
          break;
        case F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING:
          f1_pos_srs_resource->groupOrSequenceHopping = F1AP_PosSRSResource_Item__groupOrSequenceHopping_groupHopping;
          break;
        case F1AP_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING:
          f1_pos_srs_resource->groupOrSequenceHopping = F1AP_PosSRSResource_Item__groupOrSequenceHopping_sequenceHopping;
          break;
        default:
          AssertFatal(false, "illegal groupOrSequenceHopping %d\n", pos_srs_resource->group_or_sequence_hopping);
          break;
      }

      f1ap_resource_type_pos_t *resource_type_pos = &pos_srs_resource->resource_type_pos;
      F1AP_ResourceTypePos_t *f1_resourceTypePos = &f1_pos_srs_resource->resourceTypePos;
      if (resource_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_NOTHING) {
        f1_resourceTypePos->present = F1AP_ResourceTypePos_PR_NOTHING;
      } else if (resource_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_PERIODIC) {
        f1_resourceTypePos->present = F1AP_ResourceTypePos_PR_periodic;
        asn1cCalloc(f1_resourceTypePos->choice.periodic, f1_pos_periodic);
        switch (resource_type_pos->choice.periodic.periodicity) {
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT1:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot1;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT2:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot2;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT4:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot4;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot5;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT8:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot8;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT10:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot10;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT16:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot16;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT20:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot20;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT32:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot32;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT40:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot40;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT64:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot64;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT80:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot80;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT160:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot160;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT320:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot320;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT640:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot640;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT1280:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot1280;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT2560:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot2560;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5120:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot5120;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT10240:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot10240;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT20480:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot20480;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT40960:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot40960;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT81920:
            f1_pos_periodic->periodicity = F1AP_ResourceTypePeriodicPos__periodicity_slot81920;
            break;
          default:
            AssertFatal(false, "illegal periodicity %d\n", resource_type_pos->choice.periodic.periodicity);
            break;
        }

        f1_pos_periodic->offset = resource_type_pos->choice.periodic.offset;
      } else if (resource_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_SEMI_PERSISTENT) {
        f1_resourceTypePos->present = F1AP_ResourceTypePos_PR_semi_persistent;
        asn1cCalloc(f1_resourceTypePos->choice.semi_persistent, f1_pos_semi_persistent);
        switch (resource_type_pos->choice.semi_persistent.periodicity) {
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT1:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot1;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT2:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot2;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT4:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot4;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot5;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT8:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot8;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT10:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot10;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT16:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot16;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT20:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot20;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT32:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot32;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT40:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot40;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT64:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot64;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT80:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot80;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT160:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot160;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT320:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot320;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT640:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot640;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT1280:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot1280;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT2560:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot2560;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5120:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot5120;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT10240:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot10240;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT20480:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot20480;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT40960:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot40960;
            break;
          case F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT81920:
            f1_pos_semi_persistent->periodicity = F1AP_ResourceTypeSemi_persistentPos__periodicity_slot81920;
            break;
          default:
            AssertFatal(false, "illegal periodicity %d\n", resource_type_pos->choice.semi_persistent.periodicity);
            break;
        }
        f1_pos_semi_persistent->offset = resource_type_pos->choice.semi_persistent.offset;
      } else if (resource_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_APERIODIC) {
        f1_resourceTypePos->present = F1AP_ResourceTypePos_PR_aperiodic;
        asn1cCalloc(f1_resourceTypePos->choice.aperiodic, f1_pos_aperiodic);
        f1_pos_aperiodic->slotOffset = resource_type_pos->choice.aperiodic.slot_offset;
      } else {
        AssertFatal(false, "illegal resourceType %d\n", resource_type_pos->present);
      }

      f1_pos_srs_resource->sequenceId = pos_srs_resource->sequence_id;
    }
  }

  // optional: srs_resource_set_list
  if (in_config->srs_resource_set_list) {
    f1ap_srs_resource_set_list_t *srs_resource_set_list = in_config->srs_resource_set_list;
    uint32_t srs_resource_set_list_length = srs_resource_set_list->srs_resource_set_list_length;
    asn1cCalloc(out_config.sRSResourceSet_List, f1_sRSResourceSet_List);
    for (int i = 0; i < srs_resource_set_list_length; i++) {
      asn1cSequenceAdd(f1_sRSResourceSet_List->list, F1AP_SRSResourceSet_t, f1_srs_resource_set);
      f1ap_srs_resource_set_t *srs_resource_set = &srs_resource_set_list->srs_resource_set[i];
      f1_srs_resource_set->sRSResourceSetID = srs_resource_set->srs_resource_set_id;
      uint8_t srs_resource_id_list_length = srs_resource_set->srs_resource_id_list.srs_resource_id_list_length;
      for (int j = 0; j < srs_resource_id_list_length; j++) {
        asn1cSequenceAdd(f1_srs_resource_set->sRSResourceID_List.list, F1AP_SRSResourceID_t, f1_srs_resource_id);
        *f1_srs_resource_id = srs_resource_set->srs_resource_id_list.srs_resource_id[j];
      }

      F1AP_ResourceSetType_t *f1_resourceSetType = &f1_srs_resource_set->resourceSetType;
      f1ap_resource_set_type_t *resource_set_type = &srs_resource_set->resource_set_type;
      switch (resource_set_type->present) {
        case F1AP_RESOURCE_SET_TYPE_PR_NOTHING:
          f1_resourceSetType->present = F1AP_ResourceSetType_PR_NOTHING;
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_PERIODIC:
          f1_resourceSetType->present = F1AP_ResourceSetType_PR_periodic;
          asn1cCalloc(f1_resourceSetType->choice.periodic, f1_periodic_set_type);
          f1_periodic_set_type->periodicSet = resource_set_type->choice.periodic;
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT:
          f1_resourceSetType->present = F1AP_ResourceSetType_PR_semi_persistent;
          asn1cCalloc(f1_resourceSetType->choice.semi_persistent, f1_semi_persistent_set_type);
          f1_semi_persistent_set_type->semi_persistentSet = resource_set_type->choice.semi_persistent;
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_APERIODIC:
          f1_resourceSetType->present = F1AP_ResourceSetType_PR_aperiodic;
          asn1cCalloc(f1_resourceSetType->choice.aperiodic, f1_aperiodic_set_type);
          f1_aperiodic_set_type->sRSResourceTrigger_List = resource_set_type->choice.aperiodic.srs_resource_trigger;
          f1_aperiodic_set_type->slotoffset = resource_set_type->choice.aperiodic.slot_offset;
          break;
        default:
          AssertFatal(false, "illegal resource set type %d\n", resource_set_type->present);
          break;
      }
    }
  }

  // optional: pos_srs_resource_set_list
  if (in_config->pos_srs_resource_set_list) {
    f1ap_pos_srs_resource_set_list_t *pos_srs_resource_set_list = in_config->pos_srs_resource_set_list;
    uint32_t pos_srs_resource_set_list_length = pos_srs_resource_set_list->pos_srs_resource_set_list_length;
    asn1cCalloc(out_config.posSRSResourceSet_List, f1_posSRSResourceSet_List);
    for (int i = 0; i < pos_srs_resource_set_list_length; i++) {
      asn1cSequenceAdd(f1_posSRSResourceSet_List->list, F1AP_PosSRSResourceSet_Item_t, f1_pos_srs_resource_set);
      f1ap_pos_srs_resource_set_item_t *pos_srs_resource_set = &pos_srs_resource_set_list->pos_srs_resource_set_item[i];
      f1_pos_srs_resource_set->possrsResourceSetID = pos_srs_resource_set->pos_srs_resource_set_id;
      uint8_t pos_srs_resource_id_list_length = pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length;
      for (int j = 0; j < pos_srs_resource_id_list_length; j++) {
        asn1cSequenceAdd(f1_pos_srs_resource_set->possRSResourceID_List.list, F1AP_SRSPosResourceID_t, f1_pos_srs_resource_id);
        *f1_pos_srs_resource_id = pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[j];
      }

      F1AP_PosResourceSetType_t *f1_posresourceSetType = &f1_pos_srs_resource_set->posresourceSetType;
      f1ap_pos_resource_set_type_t *pos_resource_set_type = &pos_srs_resource_set->pos_resource_set_type;
      switch (pos_resource_set_type->present) {
        case F1AP_POS_RESOURCE_SET_TYPE_PR_NOTHING:
          f1_posresourceSetType->present = F1AP_PosResourceSetType_PR_NOTHING;
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_PERIODIC:
          f1_posresourceSetType->present = F1AP_PosResourceSetType_PR_periodic;
          asn1cCalloc(f1_posresourceSetType->choice.periodic, f1_pos_periodic_set_type);
          f1_pos_periodic_set_type->posperiodicSet = pos_resource_set_type->choice.periodic;
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT:
          f1_posresourceSetType->present = F1AP_PosResourceSetType_PR_semi_persistent;
          asn1cCalloc(f1_posresourceSetType->choice.semi_persistent, f1_pos_semi_persistent_set_type);
          f1_pos_semi_persistent_set_type->possemi_persistentSet = pos_resource_set_type->choice.semi_persistent;
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_APERIODIC:
          f1_posresourceSetType->present = F1AP_PosResourceSetType_PR_aperiodic;
          asn1cCalloc(f1_posresourceSetType->choice.aperiodic, f1_pos_aperiodic_set_type);
          f1_pos_aperiodic_set_type->sRSResourceTrigger_List = pos_resource_set_type->choice.srs_resource;
          break;
        default:
          AssertFatal(false, "illegal resource set type pos %d\n", pos_resource_set_type->present);
          break;
      }
    }
  }
  return out_config;
}

static F1AP_SRSCarrier_List_Item_t encode_srs_carrier_list_item(const f1ap_srs_carrier_list_item_t *in_item)
{
  F1AP_SRSCarrier_List_Item_t out_item = {0};
  // pointA
  out_item.pointA = in_item->pointA;

  // Uplink Channel BW-PerSCS-List
  const f1ap_uplink_channel_bw_per_scs_list_t *uplink_channel_bw_per_scs_list = &in_item->uplink_channel_bw_per_scs_list;
  F1AP_UplinkChannelBW_PerSCS_List_t *f1_uplink_channel_bw_per_scs_list = &out_item.uplinkChannelBW_PerSCS_List;

  uint32_t scs_specific_carrier_list_length = uplink_channel_bw_per_scs_list->scs_specific_carrier_list_length;
  for (int i = 0; i < scs_specific_carrier_list_length; i++) {
    asn1cSequenceAdd(f1_uplink_channel_bw_per_scs_list->list, F1AP_SCS_SpecificCarrier_t, f1_scs_specific_carrier);
    f1ap_scs_specific_carrier_t *scs_specific_carrier = &uplink_channel_bw_per_scs_list->scs_specific_carrier[i];
    // offset to carrier
    f1_scs_specific_carrier->offsetToCarrier = scs_specific_carrier->offset_to_carrier;
    // subcarrier spacing
    switch (scs_specific_carrier->subcarrier_spacing) {
      case F1AP_SUBCARRIER_SPACING_15KHZ:
        f1_scs_specific_carrier->subcarrierSpacing = F1AP_SCS_SpecificCarrier__subcarrierSpacing_kHz15;
        break;
      case F1AP_SUBCARRIER_SPACING_30KHZ:
        f1_scs_specific_carrier->subcarrierSpacing = F1AP_SCS_SpecificCarrier__subcarrierSpacing_kHz30;
        break;
      case F1AP_SUBCARRIER_SPACING_60KHZ:
        f1_scs_specific_carrier->subcarrierSpacing = F1AP_SCS_SpecificCarrier__subcarrierSpacing_kHz60;
        break;
      case F1AP_SUBCARRIER_SPACING_120KHZ:
        f1_scs_specific_carrier->subcarrierSpacing = F1AP_SCS_SpecificCarrier__subcarrierSpacing_kHz120;
        break;
      default:
        AssertFatal(false, "illegal subcarrier spacing %d\n", scs_specific_carrier->subcarrier_spacing);
        break;
    }
    // carrier bandwidth
    f1_scs_specific_carrier->carrierBandwidth = scs_specific_carrier->carrier_bandwidth;
  }

  // Active UL BWP
  F1AP_ActiveULBWP_t *f1_active_ul_bwp = &out_item.activeULBWP;
  const f1ap_active_ul_bwp_t *active_ul_bwp = &in_item->active_ul_bwp;

  // location and bandwidth
  f1_active_ul_bwp->locationAndBandwidth = active_ul_bwp->location_and_bandwidth;
  // subcarrier spacing
  switch (active_ul_bwp->subcarrier_spacing) {
    case F1AP_SUBCARRIER_SPACING_15KHZ:
      f1_active_ul_bwp->subcarrierSpacing = F1AP_ActiveULBWP__subcarrierSpacing_kHz15;
      break;
    case F1AP_SUBCARRIER_SPACING_30KHZ:
      f1_active_ul_bwp->subcarrierSpacing = F1AP_ActiveULBWP__subcarrierSpacing_kHz30;
      break;
    case F1AP_SUBCARRIER_SPACING_60KHZ:
      f1_active_ul_bwp->subcarrierSpacing = F1AP_ActiveULBWP__subcarrierSpacing_kHz60;
      break;
    case F1AP_SUBCARRIER_SPACING_120KHZ:
      f1_active_ul_bwp->subcarrierSpacing = F1AP_ActiveULBWP__subcarrierSpacing_kHz120;
      break;
    default:
      AssertFatal(false, "illegal subcarrier spacing %d\n", active_ul_bwp->subcarrier_spacing);
      break;
  }

  // cyclic prefix
  if (active_ul_bwp->cyclic_prefix == F1AP_CP_TYPE_NORMAL)
    f1_active_ul_bwp->cyclicPrefix = F1AP_ActiveULBWP__cyclicPrefix_normal;
  else
    f1_active_ul_bwp->cyclicPrefix = F1AP_ActiveULBWP__cyclicPrefix_extended;

  // Tx Direct Current Location
  f1_active_ul_bwp->txDirectCurrentLocation = active_ul_bwp->tx_direct_current_location;

  // SRS Config
  const f1ap_srs_config_t *sRSConfig = &active_ul_bwp->srs_config;
  f1_active_ul_bwp->sRSConfig = encode_srs_config(sRSConfig);
  return out_item;
}

static F1AP_SRSCarrier_List_t encode_srs_carrier_list(const f1ap_srs_carrier_list_t *in_list)
{
  F1AP_SRSCarrier_List_t out_list = {0};
  uint32_t list_len = in_list->srs_carrier_list_length;
  for (int i = 0; i < list_len; i++) {
    asn1cSequenceAdd(out_list.list, F1AP_SRSCarrier_List_Item_t, out_item);
    f1ap_srs_carrier_list_item_t *in_item = &in_list->srs_carrier_list_item[i];
    *out_item = encode_srs_carrier_list_item(in_item);
  }
  return out_list;
}

static void decode_srs_config(const F1AP_SRSConfig_t *in_config, f1ap_srs_config_t *out_config)
{
  // optional: sRSResource_List
  if (in_config->sRSResource_List) {
    out_config->srs_resource_list = calloc_or_fail(1, sizeof(*out_config->srs_resource_list));
    f1ap_srs_resource_list_t *srs_resource_list = out_config->srs_resource_list;
    uint32_t srs_resource_list_length = in_config->sRSResource_List->list.count;
    srs_resource_list->srs_resource_list_length = srs_resource_list_length;
    srs_resource_list->srs_resource = calloc_or_fail(srs_resource_list_length, sizeof(*srs_resource_list->srs_resource));
    for (int i = 0; i < srs_resource_list_length; i++) {
      f1ap_srs_resource_t *srs_resource = &srs_resource_list->srs_resource[i];
      F1AP_SRSResource_t *f1_srs_resource = in_config->sRSResource_List->list.array[i];
      srs_resource->srs_resource_id = f1_srs_resource->sRSResourceID;
      switch (f1_srs_resource->nrofSRS_Ports) {
        case F1AP_SRSResource__nrofSRS_Ports_port1:
          srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N1;
          break;
        case F1AP_SRSResource__nrofSRS_Ports_ports2:
          srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N2;
          break;
        case F1AP_SRSResource__nrofSRS_Ports_ports4:
          srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N4;
          break;
        default:
          AssertFatal(false, "illegal number of srs ports %ld\n", f1_srs_resource->nrofSRS_Ports);
          break;
      }

      f1ap_transmission_comb_t *srs_tx_comb = &srs_resource->transmission_comb;
      F1AP_TransmissionComb_t *f1_srs_tx_comb = &f1_srs_resource->transmissionComb;
      switch (f1_srs_tx_comb->present) {
        case F1AP_TransmissionComb_PR_NOTHING:
          srs_tx_comb->present = F1AP_TRANSMISSION_COMB_PR_NOTHING;
          break;
        case F1AP_TransmissionComb_PR_n2:
          srs_tx_comb->present = F1AP_TRANSMISSION_COMB_PR_N2;
          srs_tx_comb->choice.n2.comb_offset_n2 = f1_srs_tx_comb->choice.n2->combOffset_n2;
          srs_tx_comb->choice.n2.cyclic_shift_n2 = f1_srs_tx_comb->choice.n2->cyclicShift_n2;
          break;
        case F1AP_TransmissionComb_PR_n4:
          srs_tx_comb->present = F1AP_TRANSMISSION_COMB_PR_N4;
          srs_tx_comb->choice.n4.comb_offset_n4 = f1_srs_tx_comb->choice.n4->combOffset_n4;
          srs_tx_comb->choice.n4.cyclic_shift_n4 = f1_srs_tx_comb->choice.n4->cyclicShift_n4;
          break;
        default:
          AssertFatal(false, "illegal transmissionComb %d\n", f1_srs_tx_comb->present);
          break;
      }

      srs_resource->start_position = f1_srs_resource->startPosition;

      switch (f1_srs_resource->nrofSymbols) {
        case F1AP_SRSResource__nrofSymbols_n1:
          srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N1;
          break;
        case F1AP_SRSResource__nrofSymbols_n2:
          srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N2;
          break;
        case F1AP_SRSResource__nrofSymbols_n4:
          srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N4;
          break;
        default:
          AssertFatal(false, "illegal number of symbols %ld\n", f1_srs_resource->nrofSymbols);
          break;
      }

      switch (f1_srs_resource->repetitionFactor) {
        case F1AP_SRSResource__repetitionFactor_n1:
          srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF1;
          break;
        case F1AP_SRSResource__repetitionFactor_n2:
          srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF2;
          break;
        case F1AP_SRSResource__repetitionFactor_n4:
          srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF4;
          break;
        default:
          AssertFatal(false, "illegal repetition factor %ld\n", f1_srs_resource->repetitionFactor);
          break;
      }

      srs_resource->freq_domain_position = f1_srs_resource->freqDomainPosition;
      srs_resource->freq_domain_shift = f1_srs_resource->freqDomainShift;
      srs_resource->c_srs = f1_srs_resource->c_SRS;
      srs_resource->b_srs = f1_srs_resource->b_SRS;
      srs_resource->b_hop = f1_srs_resource->b_hop;

      switch (f1_srs_resource->groupOrSequenceHopping) {
        case F1AP_SRSResource__groupOrSequenceHopping_neither:
          srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_NOTHING;
          break;
        case F1AP_SRSResource__groupOrSequenceHopping_groupHopping:
          srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING;
          break;
        case F1AP_SRSResource__groupOrSequenceHopping_sequenceHopping:
          srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING;
          break;
        default:
          AssertFatal(false, "illegal groupOrSequenceHopping %ld\n", f1_srs_resource->groupOrSequenceHopping);
          break;
      }

      F1AP_ResourceType_t *f1_resourceType = &f1_srs_resource->resourceType;
      f1ap_resource_type_t *resource_type = &srs_resource->resource_type;
      if (f1_resourceType->present == F1AP_ResourceType_PR_NOTHING) {
        resource_type->present = F1AP_RESOURCE_TYPE_PR_NOTHING;
      } else if (f1_resourceType->present == F1AP_ResourceType_PR_periodic) {
        resource_type->present = F1AP_RESOURCE_TYPE_PR_PERIODIC;
        f1ap_resource_type_periodic_t *periodic = &resource_type->choice.periodic;
        switch (f1_resourceType->choice.periodic->periodicity) {
          case F1AP_ResourceTypePeriodic__periodicity_slot1:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT1;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot2:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT2;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot4:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT4;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot5:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT5;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot8:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT8;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot10:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT10;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot16:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT16;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot20:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT20;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot32:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT32;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot40:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT40;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot64:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT64;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot80:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT80;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot160:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT160;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot320:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT320;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot640:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT640;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot1280:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT1280;
            break;
          case F1AP_ResourceTypePeriodic__periodicity_slot2560:
            periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT2560;
            break;
          default:
            AssertFatal(false, "illegal periodicity %ld\n", f1_resourceType->choice.periodic->periodicity);
            break;
        }
        periodic->offset = f1_resourceType->choice.periodic->offset;
      } else if (f1_resourceType->present == F1AP_ResourceType_PR_semi_persistent) {
        resource_type->present = F1AP_RESOURCE_TYPE_PR_SEMI_PERSISTENT;
        f1ap_resource_type_semi_persistent_t *semi_persistent = &resource_type->choice.semi_persistent;
        switch (f1_resourceType->choice.semi_persistent->periodicity) {
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot1:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT1;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot2:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT2;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot4:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT4;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot5:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT5;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot8:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT8;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot10:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT10;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot16:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT16;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot20:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT20;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot32:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT32;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot40:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT40;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot64:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT64;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot80:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT80;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot160:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT160;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot320:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT320;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot640:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT640;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot1280:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT1280;
            break;
          case F1AP_ResourceTypeSemi_persistent__periodicity_slot2560:
            semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT2560;
            break;
          default:
            AssertFatal(false, "illegal periodicity %ld\n", f1_resourceType->choice.semi_persistent->periodicity);
            break;
        }
        semi_persistent->offset = f1_resourceType->choice.semi_persistent->offset;
      } else if (f1_resourceType->present == F1AP_ResourceType_PR_aperiodic) {
        resource_type->present = F1AP_RESOURCE_TYPE_PR_APERIODIC;
        resource_type->choice.aperiodic = f1_resourceType->choice.aperiodic->aperiodicResourceType;
      } else {
        AssertFatal(false, "illegal resourceType %d\n", f1_resourceType->present);
      }

      srs_resource->sequence_id = f1_srs_resource->sequenceId;
    }
  }

  // optional: posSRSResource_List
  if (in_config->posSRSResource_List) {
    out_config->pos_srs_resource_list = calloc_or_fail(1, sizeof(*out_config->pos_srs_resource_list));
    f1ap_pos_srs_resource_list_t *pos_srs_resource_list = out_config->pos_srs_resource_list;
    uint32_t pos_srs_resource_list_length = in_config->posSRSResource_List->list.count;
    pos_srs_resource_list->pos_srs_resource_list_length = pos_srs_resource_list_length;
    pos_srs_resource_list->pos_srs_resource_item =
        calloc_or_fail(pos_srs_resource_list_length, sizeof(*pos_srs_resource_list->pos_srs_resource_item));
    for (int i = 0; i < pos_srs_resource_list_length; i++) {
      F1AP_PosSRSResource_Item_t *f1_pos_srs_resource = in_config->posSRSResource_List->list.array[i];
      f1ap_pos_srs_resource_item_t *pos_srs_resource = &pos_srs_resource_list->pos_srs_resource_item[i];
      pos_srs_resource->srs_pos_resource_id = f1_pos_srs_resource->srs_PosResourceId;

      F1AP_TransmissionCombPos_t *f1_tx_comb_pos = &f1_pos_srs_resource->transmissionCombPos;
      f1ap_transmission_comb_pos_t *tx_comb_pos = &pos_srs_resource->transmission_comb_pos;
      switch (f1_tx_comb_pos->present) {
        case F1AP_TransmissionCombPos_PR_NOTHING:
          tx_comb_pos->present = F1AP_TRANSMISSION_COMB_POS_PR_NOTHING;
          break;
        case F1AP_TransmissionCombPos_PR_n2:
          tx_comb_pos->present = F1AP_TRANSMISSION_COMB_POS_PR_N2;
          tx_comb_pos->choice.n2.comb_offset_n2 = f1_tx_comb_pos->choice.n2->combOffset_n2;
          tx_comb_pos->choice.n2.cyclic_shift_n2 = f1_tx_comb_pos->choice.n2->cyclicShift_n2;
          break;
        case F1AP_TransmissionCombPos_PR_n4:
          tx_comb_pos->present = F1AP_TRANSMISSION_COMB_POS_PR_N4;
          tx_comb_pos->choice.n4.comb_offset_n4 = f1_tx_comb_pos->choice.n4->combOffset_n4;
          tx_comb_pos->choice.n4.cyclic_shift_n4 = f1_tx_comb_pos->choice.n4->cyclicShift_n4;
          break;
        case F1AP_TransmissionCombPos_PR_n8:
          tx_comb_pos->present = F1AP_TRANSMISSION_COMB_POS_PR_N8;
          tx_comb_pos->choice.n8.comb_offset_n8 = f1_tx_comb_pos->choice.n8->combOffset_n8;
          tx_comb_pos->choice.n8.cyclic_shift_n8 = f1_tx_comb_pos->choice.n8->cyclicShift_n8;
          break;
        default:
          AssertFatal(false, "illegal transmissionComb %d\n", f1_tx_comb_pos->present);
          break;
      }

      pos_srs_resource->start_position = f1_pos_srs_resource->startPosition;

      switch (f1_pos_srs_resource->nrofSymbols) {
        case F1AP_PosSRSResource_Item__nrofSymbols_n1:
          pos_srs_resource->nr_of_symbols = F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N1;
          break;
        case F1AP_PosSRSResource_Item__nrofSymbols_n2:
          pos_srs_resource->nr_of_symbols = F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N2;
          break;
        case F1AP_PosSRSResource_Item__nrofSymbols_n4:
          pos_srs_resource->nr_of_symbols = F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N4;
          break;
        case F1AP_PosSRSResource_Item__nrofSymbols_n8:
          pos_srs_resource->nr_of_symbols = F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N8;
          break;
        case F1AP_PosSRSResource_Item__nrofSymbols_n12:
          pos_srs_resource->nr_of_symbols = F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N12;
          break;
        default:
          AssertFatal(false, "illegal number of symbols %ld\n", f1_pos_srs_resource->nrofSymbols);
          break;
      }

      pos_srs_resource->freq_domain_shift = f1_pos_srs_resource->freqDomainShift;
      pos_srs_resource->c_srs = f1_pos_srs_resource->c_SRS;

      switch (f1_pos_srs_resource->groupOrSequenceHopping) {
        case F1AP_PosSRSResource_Item__groupOrSequenceHopping_neither:
          pos_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_NOTHING;
          break;
        case F1AP_PosSRSResource_Item__groupOrSequenceHopping_groupHopping:
          pos_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING;
          break;
        case F1AP_PosSRSResource_Item__groupOrSequenceHopping_sequenceHopping:
          pos_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING;
          break;
        default:
          AssertFatal(false, "illegal groupOrSequenceHopping %ld\n", f1_pos_srs_resource->groupOrSequenceHopping);
          break;
      }

      F1AP_ResourceTypePos_t *f1_res_type_pos = &f1_pos_srs_resource->resourceTypePos;
      f1ap_resource_type_pos_t *res_type_pos = &pos_srs_resource->resource_type_pos;
      if (f1_res_type_pos->present == F1AP_ResourceTypePos_PR_NOTHING) {
        res_type_pos->present = F1AP_RESOURCE_TYPE_POS_PR_NOTHING;
      } else if (f1_res_type_pos->present == F1AP_ResourceTypePos_PR_periodic) {
        res_type_pos->present = F1AP_RESOURCE_TYPE_POS_PR_PERIODIC;
        f1ap_resource_type_periodic_pos_t *pos_periodic = &res_type_pos->choice.periodic;
        switch (f1_res_type_pos->choice.periodic->periodicity) {
          case F1AP_ResourceTypePeriodicPos__periodicity_slot1:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT1;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot2:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT2;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot4:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT4;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot5:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot8:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT8;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot10:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT10;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot16:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT16;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot20:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT20;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot32:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT32;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot40:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT40;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot64:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT64;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot80:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT80;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot160:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT160;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot320:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT320;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot640:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT640;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot1280:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT1280;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot2560:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT2560;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot5120:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5120;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot10240:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT10240;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot20480:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT20480;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot40960:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT40960;
            break;
          case F1AP_ResourceTypePeriodicPos__periodicity_slot81920:
            pos_periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT81920;
            break;
          default:
            AssertFatal(false, "illegal periodicity %ld\n", f1_res_type_pos->choice.periodic->periodicity);
            break;
        }
        pos_periodic->offset = f1_res_type_pos->choice.periodic->offset;
      } else if (f1_res_type_pos->present == F1AP_ResourceTypePos_PR_semi_persistent) {
        res_type_pos->present = F1AP_RESOURCE_TYPE_POS_PR_SEMI_PERSISTENT;
        f1ap_resource_type_semi_persistent_pos_t *pos_semi_persistent = &res_type_pos->choice.semi_persistent;
        switch (f1_res_type_pos->choice.semi_persistent->periodicity) {
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot1:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT1;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot2:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT2;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot4:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT4;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot5:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot8:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT8;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot10:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT10;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot16:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT16;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot20:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT20;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot32:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT32;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot40:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT40;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot64:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT64;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot80:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT80;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot160:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT160;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot320:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT320;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot640:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT640;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot1280:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT1280;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot2560:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT2560;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot5120:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5120;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot10240:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT10240;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot20480:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT20480;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot40960:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT40960;
            break;
          case F1AP_ResourceTypeSemi_persistentPos__periodicity_slot81920:
            pos_semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT81920;
            break;
          default:
            AssertFatal(false, "illegal periodicity %ld\n", f1_res_type_pos->choice.semi_persistent->periodicity);
            break;
        }
        pos_semi_persistent->offset = f1_res_type_pos->choice.semi_persistent->offset;
      } else if (f1_res_type_pos->present == F1AP_ResourceTypePos_PR_aperiodic) {
        res_type_pos->present = F1AP_RESOURCE_TYPE_POS_PR_APERIODIC;
        res_type_pos->choice.aperiodic.slot_offset = f1_res_type_pos->choice.aperiodic->slotOffset;
      } else {
        AssertFatal(false, "illegal resourceType %d\n", f1_res_type_pos->present);
      }

      pos_srs_resource->sequence_id = f1_pos_srs_resource->sequenceId;
    }
  }

  // optional: sRSResourceSet_List
  if (in_config->sRSResourceSet_List) {
    out_config->srs_resource_set_list = calloc_or_fail(1, sizeof(*out_config->srs_resource_set_list));
    f1ap_srs_resource_set_list_t *srs_resource_set_list = out_config->srs_resource_set_list;
    uint32_t srs_resource_set_list_length = in_config->sRSResourceSet_List->list.count;
    srs_resource_set_list->srs_resource_set_list_length = srs_resource_set_list_length;
    srs_resource_set_list->srs_resource_set =
        calloc_or_fail(srs_resource_set_list_length, sizeof(*srs_resource_set_list->srs_resource_set));
    for (int i = 0; i < srs_resource_set_list_length; i++) {
      F1AP_SRSResourceSet_t *f1_srs_resource_set = in_config->sRSResourceSet_List->list.array[i];
      f1ap_srs_resource_set_t *srs_resource_set = &srs_resource_set_list->srs_resource_set[i];
      srs_resource_set->srs_resource_set_id = f1_srs_resource_set->sRSResourceSetID;
      uint8_t srs_resource_id_list_length = f1_srs_resource_set->sRSResourceID_List.list.count;
      srs_resource_set->srs_resource_id_list.srs_resource_id_list_length = srs_resource_id_list_length;
      srs_resource_set->srs_resource_id_list.srs_resource_id =
          calloc_or_fail(srs_resource_id_list_length, sizeof(*srs_resource_set->srs_resource_id_list.srs_resource_id));
      for (int j = 0; j < srs_resource_id_list_length; j++) {
        F1AP_SRSResourceID_t *f1_srs_resource_id = f1_srs_resource_set->sRSResourceID_List.list.array[j];
        srs_resource_set->srs_resource_id_list.srs_resource_id[j] = *f1_srs_resource_id;
      }

      F1AP_ResourceSetType_t *f1_res_set_type = &f1_srs_resource_set->resourceSetType;
      f1ap_resource_set_type_t *res_set_type = &srs_resource_set->resource_set_type;
      switch (f1_res_set_type->present) {
        case F1AP_ResourceSetType_PR_NOTHING:
          res_set_type->present = F1AP_RESOURCE_SET_TYPE_PR_NOTHING;
          break;
        case F1AP_ResourceSetType_PR_periodic:
          res_set_type->present = F1AP_RESOURCE_SET_TYPE_PR_PERIODIC;
          res_set_type->choice.periodic = f1_res_set_type->choice.periodic->periodicSet;
          break;
        case F1AP_ResourceSetType_PR_semi_persistent:
          res_set_type->present = F1AP_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT;
          res_set_type->choice.semi_persistent = f1_res_set_type->choice.semi_persistent->semi_persistentSet;
          break;
        case F1AP_ResourceSetType_PR_aperiodic:
          res_set_type->present = F1AP_RESOURCE_SET_TYPE_PR_APERIODIC;
          res_set_type->choice.aperiodic.srs_resource_trigger = f1_res_set_type->choice.aperiodic->sRSResourceTrigger_List;
          res_set_type->choice.aperiodic.slot_offset = f1_res_set_type->choice.aperiodic->slotoffset;
          break;
        default:
          AssertFatal(false, "illegal resource set type %d\n", f1_res_set_type->present);
          break;
      }
    }
  }

  // optional: posSRSResourceSet_List
  if (in_config->posSRSResourceSet_List) {
    out_config->pos_srs_resource_set_list = calloc_or_fail(1, sizeof(*out_config->pos_srs_resource_set_list));
    f1ap_pos_srs_resource_set_list_t *pos_srs_resource_set_list = out_config->pos_srs_resource_set_list;
    uint32_t pos_srs_resource_set_list_length = in_config->posSRSResourceSet_List->list.count;
    pos_srs_resource_set_list->pos_srs_resource_set_list_length = pos_srs_resource_set_list_length;
    pos_srs_resource_set_list->pos_srs_resource_set_item =
        calloc_or_fail(pos_srs_resource_set_list_length, sizeof(*pos_srs_resource_set_list->pos_srs_resource_set_item));
    for (int i = 0; i < pos_srs_resource_set_list_length; i++) {
      F1AP_PosSRSResourceSet_Item_t *f1_pos_srs_resource_set = in_config->posSRSResourceSet_List->list.array[i];
      f1ap_pos_srs_resource_set_item_t *pos_srs_resource_set = &pos_srs_resource_set_list->pos_srs_resource_set_item[i];
      pos_srs_resource_set->pos_srs_resource_set_id = f1_pos_srs_resource_set->possrsResourceSetID;
      uint8_t pos_srs_resource_id_list_length = f1_pos_srs_resource_set->possRSResourceID_List.list.count;
      pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length = pos_srs_resource_id_list_length;
      pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id =
          calloc_or_fail(pos_srs_resource_id_list_length,
                         sizeof(*pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id));
      for (int j = 0; j < pos_srs_resource_id_list_length; j++) {
        F1AP_SRSPosResourceID_t *f1_pos_srs_resource_id = f1_pos_srs_resource_set->possRSResourceID_List.list.array[j];
        pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[j] = *f1_pos_srs_resource_id;
      }

      F1AP_PosResourceSetType_t *f1_pos_res_set_type = &f1_pos_srs_resource_set->posresourceSetType;
      f1ap_pos_resource_set_type_t *pos_res_set_type = &pos_srs_resource_set->pos_resource_set_type;
      switch (f1_pos_res_set_type->present) {
        case F1AP_PosResourceSetType_PR_NOTHING:
          pos_res_set_type->present = F1AP_POS_RESOURCE_SET_TYPE_PR_NOTHING;
          break;
        case F1AP_PosResourceSetType_PR_periodic:
          pos_res_set_type->present = F1AP_POS_RESOURCE_SET_TYPE_PR_PERIODIC;
          pos_res_set_type->choice.periodic = f1_pos_res_set_type->choice.periodic->posperiodicSet;
          break;
        case F1AP_PosResourceSetType_PR_semi_persistent:
          pos_res_set_type->present = F1AP_POS_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT;
          pos_res_set_type->choice.semi_persistent = f1_pos_res_set_type->choice.semi_persistent->possemi_persistentSet;
          break;
        case F1AP_PosResourceSetType_PR_aperiodic:
          pos_res_set_type->present = F1AP_POS_RESOURCE_SET_TYPE_PR_APERIODIC;
          pos_res_set_type->choice.srs_resource = f1_pos_res_set_type->choice.aperiodic->sRSResourceTrigger_List;
          break;
        default:
          AssertFatal(false, "illegal resource set type pos %d\n", f1_pos_res_set_type->present);
          break;
      }
    }
  }
}

static void decode_srs_carrier_list_item(const F1AP_SRSCarrier_List_Item_t *in_item, f1ap_srs_carrier_list_item_t *out_item)
{
  // pointA
  out_item->pointA = in_item->pointA;

  // Uplink Channel BW-PerSCS-List
  f1ap_uplink_channel_bw_per_scs_list_t *uplink_channel_bw_per_scs_list = &out_item->uplink_channel_bw_per_scs_list;
  const F1AP_UplinkChannelBW_PerSCS_List_t *f1_uplink_channel_bw_per_scs_list = &in_item->uplinkChannelBW_PerSCS_List;

  uint32_t scs_specific_carrier_list_length = f1_uplink_channel_bw_per_scs_list->list.count;
  AssertFatal(scs_specific_carrier_list_length > 0, "Atleast 1 uplink channel bw per scs list should be present\n");
  uplink_channel_bw_per_scs_list->scs_specific_carrier_list_length = scs_specific_carrier_list_length;
  uplink_channel_bw_per_scs_list->scs_specific_carrier =
      calloc_or_fail(scs_specific_carrier_list_length, sizeof(*uplink_channel_bw_per_scs_list->scs_specific_carrier));
  for (int i = 0; i < scs_specific_carrier_list_length; i++) {
    F1AP_SCS_SpecificCarrier_t *f1_scs_specific_carrier = f1_uplink_channel_bw_per_scs_list->list.array[i];
    f1ap_scs_specific_carrier_t *scs_specific_carrier = &uplink_channel_bw_per_scs_list->scs_specific_carrier[i];
    // offset to carrier
    scs_specific_carrier->offset_to_carrier = f1_scs_specific_carrier->offsetToCarrier;
    // subcarrier spacing
    switch (f1_scs_specific_carrier->subcarrierSpacing) {
      case F1AP_SCS_SpecificCarrier__subcarrierSpacing_kHz15:
        scs_specific_carrier->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_15KHZ;
        break;
      case F1AP_SCS_SpecificCarrier__subcarrierSpacing_kHz30:
        scs_specific_carrier->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_30KHZ;
        break;
      case F1AP_SCS_SpecificCarrier__subcarrierSpacing_kHz60:
        scs_specific_carrier->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_60KHZ;
        break;
      case F1AP_SCS_SpecificCarrier__subcarrierSpacing_kHz120:
        scs_specific_carrier->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_120KHZ;
        break;
      default:
        AssertFatal(false, "illegal subcarrier spacing %ld\n", f1_scs_specific_carrier->subcarrierSpacing);
        break;
    }
    // carrier bandwidth
    scs_specific_carrier->carrier_bandwidth = f1_scs_specific_carrier->carrierBandwidth;
  }

  // Active UL BWP
  const F1AP_ActiveULBWP_t *f1_active_ul_bwp = &in_item->activeULBWP;
  f1ap_active_ul_bwp_t *active_ul_bwp = &out_item->active_ul_bwp;

  // location and bandwidth
  active_ul_bwp->location_and_bandwidth = f1_active_ul_bwp->locationAndBandwidth;
  // subcarrier spacing
  switch (f1_active_ul_bwp->subcarrierSpacing) {
    case F1AP_ActiveULBWP__subcarrierSpacing_kHz15:
      active_ul_bwp->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_15KHZ;
      break;
    case F1AP_ActiveULBWP__subcarrierSpacing_kHz30:
      active_ul_bwp->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_30KHZ;
      break;
    case F1AP_ActiveULBWP__subcarrierSpacing_kHz60:
      active_ul_bwp->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_60KHZ;
      break;
    case F1AP_ActiveULBWP__subcarrierSpacing_kHz120:
      active_ul_bwp->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_120KHZ;
      break;
    default:
      AssertFatal(false, "illegal subcarrier spacing %ld\n", f1_active_ul_bwp->subcarrierSpacing);
      break;
  }

  // cyclic prefix
  if (f1_active_ul_bwp->cyclicPrefix == F1AP_ActiveULBWP__cyclicPrefix_normal)
    active_ul_bwp->cyclic_prefix = F1AP_CP_TYPE_NORMAL;
  else
    active_ul_bwp->cyclic_prefix = F1AP_CP_TYPE_EXTENDED;

  // Tx Direct Current Location
  active_ul_bwp->tx_direct_current_location = f1_active_ul_bwp->txDirectCurrentLocation;

  // SRS Config
  const F1AP_SRSConfig_t *f1_sRSConfig = &f1_active_ul_bwp->sRSConfig;
  f1ap_srs_config_t *sRSConfig = &active_ul_bwp->srs_config;
  decode_srs_config(f1_sRSConfig, sRSConfig);
}

static void decode_srs_carrier_list(f1ap_srs_carrier_list_t *out_list, const F1AP_SRSCarrier_List_t *in_list)
{
  uint32_t list_len = in_list->list.count;
  AssertFatal(list_len > 0, "atleast 1 SRS carrier list must be present");
  out_list->srs_carrier_list_length = list_len;
  out_list->srs_carrier_list_item = calloc_or_fail(list_len, sizeof(*out_list->srs_carrier_list_item));
  for (int i = 0; i < list_len; i++) {
    F1AP_SRSCarrier_List_Item_t *in_item = in_list->list.array[i];
    f1ap_srs_carrier_list_item_t *out_item = &out_list->srs_carrier_list_item[i];
    decode_srs_carrier_list_item(in_item, out_item);
  }
}

static f1ap_srs_config_t cp_srs_config(const f1ap_srs_config_t *in_config)
{
  f1ap_srs_config_t out_config = {0};
  // optional: srs_resource_list
  if (in_config->srs_resource_list) {
    f1ap_srs_resource_list_t *srs_resource_list = in_config->srs_resource_list;
    out_config.srs_resource_list = calloc_or_fail(1, sizeof(*out_config.srs_resource_list));
    f1ap_srs_resource_list_t *f1_srs_resource_list = out_config.srs_resource_list;
    uint32_t srs_resource_list_length = srs_resource_list->srs_resource_list_length;
    f1_srs_resource_list->srs_resource_list_length = srs_resource_list_length;
    f1_srs_resource_list->srs_resource = calloc_or_fail(srs_resource_list_length, sizeof(*f1_srs_resource_list->srs_resource));
    for (int i = 0; i < srs_resource_list_length; i++) {
      f1ap_srs_resource_t *srs_resource = &srs_resource_list->srs_resource[i];
      f1ap_srs_resource_t *f1_srs_resource = &f1_srs_resource_list->srs_resource[i];
      f1_srs_resource->srs_resource_id = srs_resource->srs_resource_id;
      f1_srs_resource->nr_of_srs_ports = srs_resource->nr_of_srs_ports;

      f1ap_transmission_comb_t *f1_srs_tx_comb = &f1_srs_resource->transmission_comb;
      f1ap_transmission_comb_t *srs_tx_comb = &srs_resource->transmission_comb;
      f1_srs_tx_comb->present = srs_tx_comb->present;
      switch (srs_tx_comb->present) {
        case F1AP_TRANSMISSION_COMB_PR_NOTHING:
          // nothing to copy
          break;
        case F1AP_TRANSMISSION_COMB_PR_N2:
          f1_srs_tx_comb->choice.n2.comb_offset_n2 = srs_tx_comb->choice.n2.comb_offset_n2;
          f1_srs_tx_comb->choice.n2.cyclic_shift_n2 = srs_tx_comb->choice.n2.cyclic_shift_n2;
          break;
        case F1AP_TRANSMISSION_COMB_PR_N4:
          f1_srs_tx_comb->choice.n4.comb_offset_n4 = srs_tx_comb->choice.n4.comb_offset_n4;
          f1_srs_tx_comb->choice.n4.cyclic_shift_n4 = srs_tx_comb->choice.n4.cyclic_shift_n4;
          break;
        default:
          AssertFatal(false, "illegal transmissionComb %d\n", srs_tx_comb->present);
          break;
      }

      f1_srs_resource->start_position = srs_resource->start_position;
      f1_srs_resource->nr_of_symbols = srs_resource->nr_of_symbols;
      f1_srs_resource->repetition_factor = srs_resource->repetition_factor;
      f1_srs_resource->freq_domain_position = srs_resource->freq_domain_position;
      f1_srs_resource->freq_domain_shift = srs_resource->freq_domain_shift;
      f1_srs_resource->c_srs = srs_resource->c_srs;
      f1_srs_resource->b_srs = srs_resource->b_srs;
      f1_srs_resource->b_hop = srs_resource->b_hop;
      f1_srs_resource->group_or_sequence_hopping = srs_resource->group_or_sequence_hopping;
      f1_srs_resource->resource_type.present = srs_resource->resource_type.present;

      f1ap_resource_type_t *f1_res_type = &f1_srs_resource->resource_type;
      f1ap_resource_type_t *res_type = &srs_resource->resource_type;
      if (res_type->present == F1AP_RESOURCE_TYPE_PR_NOTHING) {
        // nothing to copy
      } else if (res_type->present == F1AP_RESOURCE_TYPE_PR_PERIODIC) {
        f1_res_type->choice.periodic.periodicity = res_type->choice.periodic.periodicity;
        f1_res_type->choice.periodic.offset = res_type->choice.periodic.offset;
      } else if (res_type->present == F1AP_RESOURCE_TYPE_PR_SEMI_PERSISTENT) {
        f1_res_type->choice.semi_persistent.periodicity = res_type->choice.semi_persistent.periodicity;
        f1_res_type->choice.semi_persistent.offset = res_type->choice.semi_persistent.offset;
      } else if (res_type->present == F1AP_RESOURCE_TYPE_PR_APERIODIC) {
        f1_res_type->choice.aperiodic = res_type->choice.aperiodic;
      } else {
        AssertFatal(false, "illegal resourceType %d\n", res_type->present);
      }

      f1_srs_resource->sequence_id = srs_resource->sequence_id;
    }
  }

  // optional: pos_srs_resource_list
  if (in_config->pos_srs_resource_list) {
    f1ap_pos_srs_resource_list_t *pos_srs_resource_list = in_config->pos_srs_resource_list;
    out_config.pos_srs_resource_list = calloc_or_fail(1, sizeof(*out_config.pos_srs_resource_list));
    f1ap_pos_srs_resource_list_t *f1_pos_srs_resource_list = out_config.pos_srs_resource_list;
    uint32_t pos_srs_resource_list_length = pos_srs_resource_list->pos_srs_resource_list_length;
    f1_pos_srs_resource_list->pos_srs_resource_list_length = pos_srs_resource_list_length;
    f1_pos_srs_resource_list->pos_srs_resource_item =
        calloc_or_fail(pos_srs_resource_list_length, sizeof(*f1_pos_srs_resource_list->pos_srs_resource_item));
    for (int i = 0; i < pos_srs_resource_list_length; i++) {
      f1ap_pos_srs_resource_item_t *pos_srs_resource = &pos_srs_resource_list->pos_srs_resource_item[i];
      f1ap_pos_srs_resource_item_t *f1_pos_srs_resource = &f1_pos_srs_resource_list->pos_srs_resource_item[i];
      f1_pos_srs_resource->srs_pos_resource_id = pos_srs_resource->srs_pos_resource_id;
      f1_pos_srs_resource->transmission_comb_pos.present = pos_srs_resource->transmission_comb_pos.present;

      f1ap_transmission_comb_pos_t *f1_tx_comb_pos = &f1_pos_srs_resource->transmission_comb_pos;
      f1ap_transmission_comb_pos_t *tx_comb_pos = &pos_srs_resource->transmission_comb_pos;
      switch (tx_comb_pos->present) {
        case F1AP_TRANSMISSION_COMB_POS_PR_NOTHING:
          // nothing to copy
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N2:
          f1_tx_comb_pos->choice.n2.comb_offset_n2 = tx_comb_pos->choice.n2.comb_offset_n2;
          f1_tx_comb_pos->choice.n2.cyclic_shift_n2 = tx_comb_pos->choice.n2.cyclic_shift_n2;
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N4:
          f1_tx_comb_pos->choice.n4.comb_offset_n4 = tx_comb_pos->choice.n4.comb_offset_n4;
          f1_tx_comb_pos->choice.n4.cyclic_shift_n4 = tx_comb_pos->choice.n4.cyclic_shift_n4;
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N8:
          f1_tx_comb_pos->choice.n8.comb_offset_n8 = tx_comb_pos->choice.n8.comb_offset_n8;
          f1_tx_comb_pos->choice.n8.cyclic_shift_n8 = tx_comb_pos->choice.n8.cyclic_shift_n8;
          break;
        default:
          AssertFatal(false, "illegal transmissionComb %d\n", tx_comb_pos->present);
          break;
      }

      f1_pos_srs_resource->start_position = pos_srs_resource->start_position;
      f1_pos_srs_resource->nr_of_symbols = pos_srs_resource->nr_of_symbols;
      f1_pos_srs_resource->freq_domain_shift = pos_srs_resource->freq_domain_shift;
      f1_pos_srs_resource->c_srs = pos_srs_resource->c_srs;
      f1_pos_srs_resource->group_or_sequence_hopping = pos_srs_resource->group_or_sequence_hopping;

      f1ap_resource_type_pos_t *f1_res_type_pos = &f1_pos_srs_resource->resource_type_pos;
      f1ap_resource_type_pos_t *res_type_pos = &pos_srs_resource->resource_type_pos;
      f1_res_type_pos->present = res_type_pos->present;
      if (res_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_NOTHING) {
        // nothing to copy
      } else if (res_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_PERIODIC) {
        f1_res_type_pos->choice.periodic.periodicity = res_type_pos->choice.periodic.periodicity;
        f1_res_type_pos->choice.periodic.offset = res_type_pos->choice.periodic.offset;
      } else if (pos_srs_resource->resource_type_pos.present == F1AP_RESOURCE_TYPE_POS_PR_SEMI_PERSISTENT) {
        f1_res_type_pos->choice.semi_persistent.periodicity = res_type_pos->choice.semi_persistent.periodicity;
        f1_res_type_pos->choice.semi_persistent.offset = res_type_pos->choice.semi_persistent.offset;
      } else if (res_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_APERIODIC) {
        f1_res_type_pos->choice.aperiodic.slot_offset = res_type_pos->choice.aperiodic.slot_offset;
      } else {
        AssertFatal(false, "illegal resourceType %d\n", res_type_pos->present);
      }

      f1_pos_srs_resource->sequence_id = pos_srs_resource->sequence_id;
    }
  }

  // optional: srs_resource_set_list
  if (in_config->srs_resource_set_list) {
    f1ap_srs_resource_set_list_t *srs_resource_set_list = in_config->srs_resource_set_list;
    out_config.srs_resource_set_list = calloc_or_fail(1, sizeof(*out_config.srs_resource_set_list));
    f1ap_srs_resource_set_list_t *f1_srs_resource_set_list = out_config.srs_resource_set_list;
    uint32_t srs_resource_set_list_length = srs_resource_set_list->srs_resource_set_list_length;
    f1_srs_resource_set_list->srs_resource_set_list_length = srs_resource_set_list_length;
    f1_srs_resource_set_list->srs_resource_set =
        calloc_or_fail(srs_resource_set_list_length, sizeof(*f1_srs_resource_set_list->srs_resource_set));
    for (int i = 0; i < srs_resource_set_list_length; i++) {
      f1ap_srs_resource_set_t *srs_resource_set = &srs_resource_set_list->srs_resource_set[i];
      f1ap_srs_resource_set_t *f1_srs_resource_set = &f1_srs_resource_set_list->srs_resource_set[i];
      f1_srs_resource_set->srs_resource_set_id = srs_resource_set->srs_resource_set_id;
      uint8_t srs_resource_id_list_length = srs_resource_set->srs_resource_id_list.srs_resource_id_list_length;
      f1_srs_resource_set->srs_resource_id_list.srs_resource_id_list_length = srs_resource_id_list_length;
      f1_srs_resource_set->srs_resource_id_list.srs_resource_id =
          calloc_or_fail(srs_resource_id_list_length, sizeof(*f1_srs_resource_set->srs_resource_id_list.srs_resource_id));
      for (int j = 0; j < srs_resource_id_list_length; j++) {
        f1_srs_resource_set->srs_resource_id_list.srs_resource_id[j] = srs_resource_set->srs_resource_id_list.srs_resource_id[j];
      }

      f1ap_resource_set_type_t *f1_res_set_type = &f1_srs_resource_set->resource_set_type;
      f1ap_resource_set_type_t *res_set_type = &srs_resource_set->resource_set_type;
      f1_res_set_type->present = res_set_type->present;
      switch (res_set_type->present) {
        case F1AP_RESOURCE_SET_TYPE_PR_NOTHING:
          // nothing to copy
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_PERIODIC:
          f1_res_set_type->choice.periodic = res_set_type->choice.periodic;
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT:
          f1_res_set_type->choice.semi_persistent = res_set_type->choice.semi_persistent;
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_APERIODIC:
          f1_res_set_type->choice.aperiodic.srs_resource_trigger = res_set_type->choice.aperiodic.srs_resource_trigger;
          f1_res_set_type->choice.aperiodic.slot_offset = res_set_type->choice.aperiodic.slot_offset;
          break;
        default:
          AssertFatal(false, "illegal resource set type %d\n", res_set_type->present);
          break;
      }
    }
  }

  // optional: pos_srs_resource_set_list
  if (in_config->pos_srs_resource_set_list) {
    f1ap_pos_srs_resource_set_list_t *pos_srs_resource_set_list = in_config->pos_srs_resource_set_list;
    out_config.pos_srs_resource_set_list = calloc_or_fail(1, sizeof(*out_config.pos_srs_resource_set_list));
    f1ap_pos_srs_resource_set_list_t *f1_pos_srs_resource_set_list = out_config.pos_srs_resource_set_list;
    uint32_t pos_srs_resource_set_list_length = pos_srs_resource_set_list->pos_srs_resource_set_list_length;
    f1_pos_srs_resource_set_list->pos_srs_resource_set_list_length = pos_srs_resource_set_list_length;
    f1_pos_srs_resource_set_list->pos_srs_resource_set_item =
        calloc_or_fail(pos_srs_resource_set_list_length, sizeof(*f1_pos_srs_resource_set_list->pos_srs_resource_set_item));
    for (int i = 0; i < pos_srs_resource_set_list_length; i++) {
      f1ap_pos_srs_resource_set_item_t *pos_srs_resource_set = &pos_srs_resource_set_list->pos_srs_resource_set_item[i];
      f1ap_pos_srs_resource_set_item_t *f1_pos_srs_resource_set = &f1_pos_srs_resource_set_list->pos_srs_resource_set_item[i];
      f1_pos_srs_resource_set->pos_srs_resource_set_id = pos_srs_resource_set->pos_srs_resource_set_id;
      uint8_t pos_srs_resource_id_list_length = pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length;
      f1_pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length = pos_srs_resource_id_list_length;
      f1_pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id =
          calloc_or_fail(pos_srs_resource_id_list_length,
                         sizeof(*f1_pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id));
      for (int j = 0; j < pos_srs_resource_id_list_length; j++) {
        f1_pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[j] =
            pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[j];
      }

      f1ap_pos_resource_set_type_t *f1_pos_res_set_type = &f1_pos_srs_resource_set->pos_resource_set_type;
      f1ap_pos_resource_set_type_t *pos_res_set_type = &pos_srs_resource_set->pos_resource_set_type;
      f1_pos_res_set_type->present = pos_res_set_type->present;
      switch (pos_res_set_type->present) {
        case F1AP_POS_RESOURCE_SET_TYPE_PR_NOTHING:
          // nothing to copy
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_PERIODIC:
          f1_pos_res_set_type->choice.periodic = pos_res_set_type->choice.periodic;
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT:
          f1_pos_res_set_type->choice.semi_persistent = pos_res_set_type->choice.semi_persistent;
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_APERIODIC:
          f1_pos_res_set_type->choice.srs_resource = pos_res_set_type->choice.srs_resource;
          break;
        default:
          AssertFatal(false, "illegal resource set type pos %d\n", pos_res_set_type->present);
          break;
      }
    }
  }
  return out_config;
}

static f1ap_srs_carrier_list_item_t cp_srs_carrier_list_item(const f1ap_srs_carrier_list_item_t *in_item)
{
  f1ap_srs_carrier_list_item_t out_item = {0};
  // pointA
  out_item.pointA = in_item->pointA;

  // Uplink Channel BW-PerSCS-List
  const f1ap_uplink_channel_bw_per_scs_list_t *uplink_channel_bw_per_scs_list = &in_item->uplink_channel_bw_per_scs_list;
  f1ap_uplink_channel_bw_per_scs_list_t *f1_uplink_channel_bw_per_scs_list = &out_item.uplink_channel_bw_per_scs_list;

  uint32_t scs_specific_carrier_list_length = uplink_channel_bw_per_scs_list->scs_specific_carrier_list_length;
  f1_uplink_channel_bw_per_scs_list->scs_specific_carrier_list_length = scs_specific_carrier_list_length;
  f1_uplink_channel_bw_per_scs_list->scs_specific_carrier =
      calloc_or_fail(scs_specific_carrier_list_length, sizeof(*f1_uplink_channel_bw_per_scs_list->scs_specific_carrier));
  for (int i = 0; i < scs_specific_carrier_list_length; i++) {
    f1ap_scs_specific_carrier_t *scs_specific_carrier = &uplink_channel_bw_per_scs_list->scs_specific_carrier[i];
    f1ap_scs_specific_carrier_t *f1_scs_specific_carrier = &f1_uplink_channel_bw_per_scs_list->scs_specific_carrier[i];
    // offset to carrier
    f1_scs_specific_carrier->offset_to_carrier = scs_specific_carrier->offset_to_carrier;
    // subcarrier spacing
    f1_scs_specific_carrier->subcarrier_spacing = scs_specific_carrier->subcarrier_spacing;
    // carrier bandwidth
    f1_scs_specific_carrier->carrier_bandwidth = scs_specific_carrier->carrier_bandwidth;
  }

  // Active UL BWP
  f1ap_active_ul_bwp_t *f1_active_ul_bwp = &out_item.active_ul_bwp;
  const f1ap_active_ul_bwp_t *active_ul_bwp = &in_item->active_ul_bwp;

  // location and bandwidth
  f1_active_ul_bwp->location_and_bandwidth = active_ul_bwp->location_and_bandwidth;
  // subcarrier spacing
  f1_active_ul_bwp->subcarrier_spacing = active_ul_bwp->subcarrier_spacing;

  // cyclic prefix
  f1_active_ul_bwp->cyclic_prefix = active_ul_bwp->cyclic_prefix;

  // Tx Direct Current Location
  f1_active_ul_bwp->tx_direct_current_location = active_ul_bwp->tx_direct_current_location;

  // SRS Config
  f1ap_srs_config_t *f1_sRSConfig = &f1_active_ul_bwp->srs_config;
  const f1ap_srs_config_t *sRSConfig = &active_ul_bwp->srs_config;
  *f1_sRSConfig = cp_srs_config(sRSConfig);
  return out_item;
}

static f1ap_srs_carrier_list_t cp_srs_carrier_list(const f1ap_srs_carrier_list_t *in_list)
{
  f1ap_srs_carrier_list_t out_list = {0};
  uint32_t list_len = in_list->srs_carrier_list_length;
  out_list.srs_carrier_list_length = list_len;
  out_list.srs_carrier_list_item = calloc_or_fail(list_len, sizeof(*out_list.srs_carrier_list_item));
  for (int i = 0; i < list_len; i++) {
    f1ap_srs_carrier_list_item_t *in_item = &in_list->srs_carrier_list_item[i];
    f1ap_srs_carrier_list_item_t *out_item = &out_list.srs_carrier_list_item[i];
    *out_item = cp_srs_carrier_list_item(in_item);
  }
  return out_list;
}

static bool eq_srs_config(const f1ap_srs_config_t *f1_sRSConfig, const f1ap_srs_config_t *sRSConfig)
{
  // optional: srs_resource_list
  if ((f1_sRSConfig->srs_resource_list == NULL) != (sRSConfig->srs_resource_list == NULL)) {
    return false;
  }
  if (sRSConfig->srs_resource_list) {
    f1ap_srs_resource_list_t *srs_resource_list = sRSConfig->srs_resource_list;
    f1ap_srs_resource_list_t *f1_srs_resource_list = f1_sRSConfig->srs_resource_list;
    uint32_t srs_resource_list_length = srs_resource_list->srs_resource_list_length;
    _F1_EQ_CHECK_INT(f1_srs_resource_list->srs_resource_list_length, srs_resource_list_length);
    for (int i = 0; i < srs_resource_list_length; i++) {
      f1ap_srs_resource_t *srs_resource = &srs_resource_list->srs_resource[i];
      f1ap_srs_resource_t *f1_srs_resource = &f1_srs_resource_list->srs_resource[i];
      _F1_EQ_CHECK_INT(f1_srs_resource->srs_resource_id, srs_resource->srs_resource_id);
      _F1_EQ_CHECK_INT(f1_srs_resource->nr_of_srs_ports, srs_resource->nr_of_srs_ports);

      f1ap_transmission_comb_t *f1_srs_tx_comb = &f1_srs_resource->transmission_comb;
      f1ap_transmission_comb_t *srs_tx_comb = &srs_resource->transmission_comb;

      _F1_EQ_CHECK_INT(f1_srs_tx_comb->present, srs_tx_comb->present);
      switch (srs_tx_comb->present) {
        case F1AP_TRANSMISSION_COMB_PR_NOTHING:
          // nothing to check
          break;
        case F1AP_TRANSMISSION_COMB_PR_N2:
          _F1_EQ_CHECK_INT(f1_srs_tx_comb->choice.n2.comb_offset_n2, srs_tx_comb->choice.n2.comb_offset_n2);
          _F1_EQ_CHECK_INT(f1_srs_tx_comb->choice.n2.cyclic_shift_n2, srs_tx_comb->choice.n2.cyclic_shift_n2);
          break;
        case F1AP_TRANSMISSION_COMB_PR_N4:
          _F1_EQ_CHECK_INT(f1_srs_tx_comb->choice.n4.comb_offset_n4, srs_tx_comb->choice.n4.comb_offset_n4);
          _F1_EQ_CHECK_INT(f1_srs_tx_comb->choice.n4.cyclic_shift_n4, srs_tx_comb->choice.n4.cyclic_shift_n4);
          break;
        default:
          AssertFatal(false, "illegal transmissionComb %d\n", srs_tx_comb->present);
          break;
      }

      _F1_EQ_CHECK_INT(f1_srs_resource->start_position, srs_resource->start_position);
      _F1_EQ_CHECK_INT(f1_srs_resource->nr_of_symbols, srs_resource->nr_of_symbols);
      _F1_EQ_CHECK_INT(f1_srs_resource->repetition_factor, srs_resource->repetition_factor);
      _F1_EQ_CHECK_INT(f1_srs_resource->freq_domain_position, srs_resource->freq_domain_position);
      _F1_EQ_CHECK_INT(f1_srs_resource->freq_domain_shift, srs_resource->freq_domain_shift);
      _F1_EQ_CHECK_INT(f1_srs_resource->c_srs, srs_resource->c_srs);
      _F1_EQ_CHECK_INT(f1_srs_resource->b_srs, srs_resource->b_srs);
      _F1_EQ_CHECK_INT(f1_srs_resource->b_hop, srs_resource->b_hop);
      _F1_EQ_CHECK_INT(f1_srs_resource->group_or_sequence_hopping, srs_resource->group_or_sequence_hopping);

      f1ap_resource_type_t *f1_res_type = &f1_srs_resource->resource_type;
      f1ap_resource_type_t *res_type = &srs_resource->resource_type;
      _F1_EQ_CHECK_INT(f1_res_type->present, res_type->present);
      if (res_type->present == F1AP_RESOURCE_TYPE_PR_NOTHING) {
        // nothing to check
      } else if (res_type->present == F1AP_RESOURCE_TYPE_PR_PERIODIC) {
        _F1_EQ_CHECK_INT(f1_res_type->choice.periodic.periodicity, res_type->choice.periodic.periodicity);
        _F1_EQ_CHECK_INT(f1_res_type->choice.periodic.offset, res_type->choice.periodic.offset);
      } else if (res_type->present == F1AP_RESOURCE_TYPE_PR_SEMI_PERSISTENT) {
        _F1_EQ_CHECK_INT(f1_res_type->choice.semi_persistent.periodicity, res_type->choice.semi_persistent.periodicity);
        _F1_EQ_CHECK_INT(f1_res_type->choice.semi_persistent.offset, res_type->choice.semi_persistent.offset);
      } else if (res_type->present == F1AP_RESOURCE_TYPE_PR_APERIODIC) {
        _F1_EQ_CHECK_INT(f1_res_type->choice.aperiodic, res_type->choice.aperiodic);
      } else {
        AssertFatal(false, "illegal resourceType %d\n", res_type->present);
      }

      _F1_EQ_CHECK_INT(f1_srs_resource->sequence_id, srs_resource->sequence_id);
    }
  }

  // optional: pos_srs_resource_list
  if ((f1_sRSConfig->pos_srs_resource_list == NULL) != (sRSConfig->pos_srs_resource_list == NULL)) {
    return false;
  }
  if (sRSConfig->pos_srs_resource_list) {
    f1ap_pos_srs_resource_list_t *pos_srs_resource_list = sRSConfig->pos_srs_resource_list;
    f1ap_pos_srs_resource_list_t *f1_pos_srs_resource_list = f1_sRSConfig->pos_srs_resource_list;
    uint32_t pos_srs_resource_list_length = pos_srs_resource_list->pos_srs_resource_list_length;
    _F1_EQ_CHECK_INT(f1_pos_srs_resource_list->pos_srs_resource_list_length, pos_srs_resource_list_length);
    for (int i = 0; i < pos_srs_resource_list_length; i++) {
      f1ap_pos_srs_resource_item_t *pos_srs_resource = &pos_srs_resource_list->pos_srs_resource_item[i];
      f1ap_pos_srs_resource_item_t *f1_pos_srs_resource = &f1_pos_srs_resource_list->pos_srs_resource_item[i];
      _F1_EQ_CHECK_INT(f1_pos_srs_resource->srs_pos_resource_id, pos_srs_resource->srs_pos_resource_id);
      _F1_EQ_CHECK_INT(f1_pos_srs_resource->transmission_comb_pos.present, pos_srs_resource->transmission_comb_pos.present);

      f1ap_transmission_comb_pos_t *f1_srs_tx_comb_pos = &f1_pos_srs_resource->transmission_comb_pos;
      f1ap_transmission_comb_pos_t *srs_tx_comb_pos = &pos_srs_resource->transmission_comb_pos;
      switch (srs_tx_comb_pos->present) {
        case F1AP_TRANSMISSION_COMB_POS_PR_NOTHING:
          // nothing to check
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N2:
          _F1_EQ_CHECK_INT(f1_srs_tx_comb_pos->choice.n2.comb_offset_n2, srs_tx_comb_pos->choice.n2.comb_offset_n2);
          _F1_EQ_CHECK_INT(f1_srs_tx_comb_pos->choice.n2.cyclic_shift_n2, srs_tx_comb_pos->choice.n2.cyclic_shift_n2);
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N4:
          _F1_EQ_CHECK_INT(f1_srs_tx_comb_pos->choice.n4.comb_offset_n4, srs_tx_comb_pos->choice.n4.comb_offset_n4);
          _F1_EQ_CHECK_INT(f1_srs_tx_comb_pos->choice.n4.cyclic_shift_n4, srs_tx_comb_pos->choice.n4.cyclic_shift_n4);
          break;
        case F1AP_TRANSMISSION_COMB_POS_PR_N8:
          _F1_EQ_CHECK_INT(f1_srs_tx_comb_pos->choice.n8.comb_offset_n8, srs_tx_comb_pos->choice.n8.comb_offset_n8);
          _F1_EQ_CHECK_INT(f1_srs_tx_comb_pos->choice.n8.cyclic_shift_n8, srs_tx_comb_pos->choice.n8.cyclic_shift_n8);
          break;
        default:
          AssertFatal(false, "illegal transmissionComb %d\n", srs_tx_comb_pos->present);
          break;
      }

      _F1_EQ_CHECK_INT(f1_pos_srs_resource->start_position, pos_srs_resource->start_position);
      _F1_EQ_CHECK_INT(f1_pos_srs_resource->nr_of_symbols, pos_srs_resource->nr_of_symbols);
      _F1_EQ_CHECK_INT(f1_pos_srs_resource->freq_domain_shift, pos_srs_resource->freq_domain_shift);
      _F1_EQ_CHECK_INT(f1_pos_srs_resource->c_srs, pos_srs_resource->c_srs);
      _F1_EQ_CHECK_INT(f1_pos_srs_resource->group_or_sequence_hopping, pos_srs_resource->group_or_sequence_hopping);

      f1ap_resource_type_pos_t *f1_res_type_pos = &f1_pos_srs_resource->resource_type_pos;
      f1ap_resource_type_pos_t *res_type_pos = &pos_srs_resource->resource_type_pos;
      _F1_EQ_CHECK_INT(f1_res_type_pos->present, res_type_pos->present);
      if (res_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_NOTHING) {
        // nothing to check
      } else if (res_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_PERIODIC) {
        _F1_EQ_CHECK_INT(f1_res_type_pos->choice.periodic.periodicity, res_type_pos->choice.periodic.periodicity);
        _F1_EQ_CHECK_INT(f1_res_type_pos->choice.periodic.offset, res_type_pos->choice.periodic.offset);
      } else if (res_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_SEMI_PERSISTENT) {
        _F1_EQ_CHECK_INT(f1_res_type_pos->choice.semi_persistent.periodicity, res_type_pos->choice.semi_persistent.periodicity);
        _F1_EQ_CHECK_INT(f1_res_type_pos->choice.semi_persistent.offset, res_type_pos->choice.semi_persistent.offset);
      } else if (res_type_pos->present == F1AP_RESOURCE_TYPE_POS_PR_APERIODIC) {
        _F1_EQ_CHECK_INT(f1_res_type_pos->choice.aperiodic.slot_offset, res_type_pos->choice.aperiodic.slot_offset);
      } else {
        AssertFatal(false, "illegal resourceType %d\n", res_type_pos->present);
      }

      _F1_EQ_CHECK_INT(f1_pos_srs_resource->sequence_id, pos_srs_resource->sequence_id);
    }
  }

  // optional: srs_resource_set_list
  if ((f1_sRSConfig->srs_resource_set_list == NULL) != (sRSConfig->srs_resource_set_list == NULL)) {
    return false;
  }
  if (sRSConfig->srs_resource_set_list) {
    f1ap_srs_resource_set_list_t *srs_resource_set_list = sRSConfig->srs_resource_set_list;
    f1ap_srs_resource_set_list_t *f1_srs_resource_set_list = f1_sRSConfig->srs_resource_set_list;
    uint32_t srs_resource_set_list_length = srs_resource_set_list->srs_resource_set_list_length;
    _F1_EQ_CHECK_INT(f1_srs_resource_set_list->srs_resource_set_list_length, srs_resource_set_list_length);
    for (int i = 0; i < srs_resource_set_list_length; i++) {
      f1ap_srs_resource_set_t *srs_resource_set = &srs_resource_set_list->srs_resource_set[i];
      f1ap_srs_resource_set_t *f1_srs_resource_set = &f1_srs_resource_set_list->srs_resource_set[i];
      _F1_EQ_CHECK_INT(f1_srs_resource_set->srs_resource_set_id, srs_resource_set->srs_resource_set_id);
      uint8_t srs_resource_id_list_length = srs_resource_set->srs_resource_id_list.srs_resource_id_list_length;
      _F1_EQ_CHECK_INT(f1_srs_resource_set->srs_resource_id_list.srs_resource_id_list_length, srs_resource_id_list_length);
      for (int j = 0; j < srs_resource_id_list_length; j++) {
        _F1_EQ_CHECK_LONG(f1_srs_resource_set->srs_resource_id_list.srs_resource_id[j],
                          srs_resource_set->srs_resource_id_list.srs_resource_id[j]);
      }

      f1ap_resource_set_type_t *f1_res_set_type = &f1_srs_resource_set->resource_set_type;
      f1ap_resource_set_type_t *res_set_type = &srs_resource_set->resource_set_type;
      _F1_EQ_CHECK_INT(f1_res_set_type->present, res_set_type->present);
      switch (res_set_type->present) {
        case F1AP_RESOURCE_SET_TYPE_PR_NOTHING:
          // nothing to check
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_PERIODIC:
          _F1_EQ_CHECK_INT(f1_res_set_type->choice.periodic, res_set_type->choice.periodic);
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT:
          _F1_EQ_CHECK_INT(f1_res_set_type->choice.semi_persistent, res_set_type->choice.semi_persistent);
          break;
        case F1AP_RESOURCE_SET_TYPE_PR_APERIODIC:
          _F1_EQ_CHECK_INT(f1_res_set_type->choice.aperiodic.srs_resource_trigger,
                           res_set_type->choice.aperiodic.srs_resource_trigger);
          _F1_EQ_CHECK_LONG(f1_res_set_type->choice.aperiodic.slot_offset, res_set_type->choice.aperiodic.slot_offset);
          break;
        default:
          AssertFatal(false, "illegal resource set type %d\n", res_set_type->present);
          break;
      }
    }
  }

  // optional: pos_srs_resource_set_list
  if ((f1_sRSConfig->pos_srs_resource_set_list == NULL) != (sRSConfig->pos_srs_resource_set_list == NULL)) {
    return false;
  }
  if (sRSConfig->pos_srs_resource_set_list) {
    f1ap_pos_srs_resource_set_list_t *pos_srs_resource_set_list = sRSConfig->pos_srs_resource_set_list;
    f1ap_pos_srs_resource_set_list_t *f1_pos_srs_resource_set_list = f1_sRSConfig->pos_srs_resource_set_list;
    uint32_t pos_srs_resource_set_list_length = pos_srs_resource_set_list->pos_srs_resource_set_list_length;
    _F1_EQ_CHECK_INT(f1_pos_srs_resource_set_list->pos_srs_resource_set_list_length, pos_srs_resource_set_list_length);
    for (int i = 0; i < pos_srs_resource_set_list_length; i++) {
      f1ap_pos_srs_resource_set_item_t *pos_srs_resource_set = &pos_srs_resource_set_list->pos_srs_resource_set_item[i];
      f1ap_pos_srs_resource_set_item_t *f1_pos_srs_resource_set = &f1_pos_srs_resource_set_list->pos_srs_resource_set_item[i];
      _F1_EQ_CHECK_INT(f1_pos_srs_resource_set->pos_srs_resource_set_id, pos_srs_resource_set->pos_srs_resource_set_id);
      uint8_t pos_srs_resource_id_list_length = pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length;
      _F1_EQ_CHECK_INT(f1_pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length,
                       pos_srs_resource_id_list_length);
      for (int j = 0; j < pos_srs_resource_id_list_length; j++) {
        _F1_EQ_CHECK_LONG(f1_pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[j],
                          pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[j]);
      }

      f1ap_pos_resource_set_type_t *f1_pos_res_set_type = &f1_pos_srs_resource_set->pos_resource_set_type;
      f1ap_pos_resource_set_type_t *pos_res_set_type = &pos_srs_resource_set->pos_resource_set_type;
      _F1_EQ_CHECK_INT(f1_pos_res_set_type->present, pos_res_set_type->present);
      switch (pos_res_set_type->present) {
        case F1AP_POS_RESOURCE_SET_TYPE_PR_NOTHING:
          // nothing to check
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_PERIODIC:
          _F1_EQ_CHECK_INT(f1_pos_res_set_type->choice.periodic, pos_res_set_type->choice.periodic);
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT:
          _F1_EQ_CHECK_INT(f1_pos_res_set_type->choice.semi_persistent, pos_res_set_type->choice.semi_persistent);
          break;
        case F1AP_POS_RESOURCE_SET_TYPE_PR_APERIODIC:
          _F1_EQ_CHECK_INT(f1_pos_res_set_type->choice.srs_resource, pos_res_set_type->choice.srs_resource);
          break;
        default:
          AssertFatal(false, "illegal resource set type pos %d\n", pos_res_set_type->present);
          break;
      }
    }
  }
  return true;
}

static bool eq_srs_carrier_list_item(const f1ap_srs_carrier_list_item_t *f1_srs_carrier_list_item,
                                     const f1ap_srs_carrier_list_item_t *srs_carrier_list_item)
{
  // pointA
  _F1_EQ_CHECK_INT(f1_srs_carrier_list_item->pointA, srs_carrier_list_item->pointA);

  // Uplink Channel BW-PerSCS-List
  const f1ap_uplink_channel_bw_per_scs_list_t *uplink_channel_bw_per_scs_list =
      &srs_carrier_list_item->uplink_channel_bw_per_scs_list;
  const f1ap_uplink_channel_bw_per_scs_list_t *f1_uplink_channel_bw_per_scs_list =
      &f1_srs_carrier_list_item->uplink_channel_bw_per_scs_list;

  uint32_t scs_specific_carrier_list_length = uplink_channel_bw_per_scs_list->scs_specific_carrier_list_length;
  _F1_EQ_CHECK_INT(f1_uplink_channel_bw_per_scs_list->scs_specific_carrier_list_length, scs_specific_carrier_list_length);
  for (int i = 0; i < scs_specific_carrier_list_length; i++) {
    f1ap_scs_specific_carrier_t *scs_specific_carrier = &uplink_channel_bw_per_scs_list->scs_specific_carrier[i];
    f1ap_scs_specific_carrier_t *f1_scs_specific_carrier = &f1_uplink_channel_bw_per_scs_list->scs_specific_carrier[i];
    // offset to carrier
    _F1_EQ_CHECK_INT(f1_scs_specific_carrier->offset_to_carrier, scs_specific_carrier->offset_to_carrier);
    // subcarrier spacing
    _F1_EQ_CHECK_INT(f1_scs_specific_carrier->subcarrier_spacing, scs_specific_carrier->subcarrier_spacing);
    // carrier bandwidth
    _F1_EQ_CHECK_INT(f1_scs_specific_carrier->carrier_bandwidth, scs_specific_carrier->carrier_bandwidth);
  }

  // Active UL BWP
  const f1ap_active_ul_bwp_t *f1_active_ul_bwp = &f1_srs_carrier_list_item->active_ul_bwp;
  const f1ap_active_ul_bwp_t *active_ul_bwp = &srs_carrier_list_item->active_ul_bwp;

  // location and bandwidth
  _F1_EQ_CHECK_INT(f1_active_ul_bwp->location_and_bandwidth, active_ul_bwp->location_and_bandwidth);
  // subcarrier spacing
  _F1_EQ_CHECK_INT(f1_active_ul_bwp->subcarrier_spacing, active_ul_bwp->subcarrier_spacing);

  // cyclic prefix
  _F1_EQ_CHECK_INT(f1_active_ul_bwp->cyclic_prefix, active_ul_bwp->cyclic_prefix);

  // Tx Direct Current Location
  _F1_EQ_CHECK_INT(f1_active_ul_bwp->tx_direct_current_location, active_ul_bwp->tx_direct_current_location);

  // SRS Config
  const f1ap_srs_config_t *f1_sRSConfig = &f1_active_ul_bwp->srs_config;
  const f1ap_srs_config_t *sRSConfig = &active_ul_bwp->srs_config;
  _F1_CHECK_EXP(eq_srs_config(f1_sRSConfig, sRSConfig));
  return true;
}

static bool eq_srs_carrier_list(const f1ap_srs_carrier_list_t *srs_carrier_list_a,
                                const f1ap_srs_carrier_list_t *srs_carrier_list_b)
{
  uint32_t srs_carrier_list_len = srs_carrier_list_a->srs_carrier_list_length;
  _F1_EQ_CHECK_INT(srs_carrier_list_b->srs_carrier_list_length, srs_carrier_list_len);
  for (int i = 0; i < srs_carrier_list_len; i++) {
    f1ap_srs_carrier_list_item_t *srs_carrier_list_item_a = &srs_carrier_list_a->srs_carrier_list_item[i];
    f1ap_srs_carrier_list_item_t *srs_carrier_list_item_b = &srs_carrier_list_b->srs_carrier_list_item[i];
    _F1_CHECK_EXP(eq_srs_carrier_list_item(srs_carrier_list_item_a, srs_carrier_list_item_b));
  }
  return true;
}

static void free_srs_carrier_list(f1ap_srs_carrier_list_t *srs_carrier_list)
{
  uint32_t srs_carrier_list_len = srs_carrier_list->srs_carrier_list_length;
  for (int i = 0; i < srs_carrier_list_len; i++) {
    f1ap_srs_carrier_list_item_t *srs_carrier_list_item = &srs_carrier_list->srs_carrier_list_item[i];
    free(srs_carrier_list_item->uplink_channel_bw_per_scs_list.scs_specific_carrier);

    f1ap_active_ul_bwp_t *active_ul_bwp = &srs_carrier_list_item->active_ul_bwp;
    f1ap_srs_config_t *sRSConfig = &active_ul_bwp->srs_config;
    if (sRSConfig->srs_resource_list) {
      f1ap_srs_resource_list_t *srs_resource_list = sRSConfig->srs_resource_list;
      free(srs_resource_list->srs_resource);
      free(sRSConfig->srs_resource_list);
    }
    if (sRSConfig->pos_srs_resource_list) {
      f1ap_pos_srs_resource_list_t *pos_srs_resource_list = sRSConfig->pos_srs_resource_list;
      free(pos_srs_resource_list->pos_srs_resource_item);
      free(sRSConfig->pos_srs_resource_list);
    }
    if (sRSConfig->srs_resource_set_list) {
      f1ap_srs_resource_set_list_t *srs_resource_set_list = sRSConfig->srs_resource_set_list;
      uint32_t srs_resource_set_list_length = srs_resource_set_list->srs_resource_set_list_length;
      for (int j = 0; j < srs_resource_set_list_length; j++) {
        f1ap_srs_resource_set_t *srs_resource_set = &srs_resource_set_list->srs_resource_set[j];
        free(srs_resource_set->srs_resource_id_list.srs_resource_id);
      }
      free(srs_resource_set_list->srs_resource_set);
      free(sRSConfig->srs_resource_set_list);
    }
    if (sRSConfig->pos_srs_resource_set_list) {
      f1ap_pos_srs_resource_set_list_t *pos_srs_resource_set_list = sRSConfig->pos_srs_resource_set_list;
      uint32_t pos_srs_resource_set_list_length = pos_srs_resource_set_list->pos_srs_resource_set_list_length;
      for (int j = 0; j < pos_srs_resource_set_list_length; j++) {
        f1ap_pos_srs_resource_set_item_t *pos_srs_resource_set = &pos_srs_resource_set_list->pos_srs_resource_set_item[j];
        free(pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id);
      }
      free(pos_srs_resource_set_list->pos_srs_resource_set_item);
      free(sRSConfig->pos_srs_resource_set_list);
    }
  }
  free(srs_carrier_list->srs_carrier_list_item);
}

static F1AP_SRSType_t encode_f1ap_srstype(const f1ap_srs_type_t *srs_type)
{
  F1AP_SRSType_t f1_srstype = {0};
  switch (srs_type->present) {
    case F1AP_SRS_TYPE_PR_NOTHING:
      f1_srstype.present = F1AP_SRSType_PR_NOTHING;
      break;
    case F1AP_SRS_TYPE_PR_SEMIPERSISTENTSRS:
      f1_srstype.present = F1AP_SRSType_PR_semipersistentSRS;
      asn1cCalloc(f1_srstype.choice.semipersistentSRS, sp_srs);
      sp_srs->sRSResourceSetID = *srs_type->choice.srs_resource_set_id;
      break;
    case F1AP_SRS_TYPE_PR_APERIODICSRS:
      f1_srstype.present = F1AP_SRSType_PR_aperiodicSRS;
      asn1cCalloc(f1_srstype.choice.aperiodicSRS, ap_srs);
      ap_srs->aperiodic = *srs_type->choice.aperiodic;
      break;
    default:
      AssertFatal(false, "unknown SRS type %d\n", srs_type->present);
      break;
  }
  return f1_srstype;
}

static bool decode_f1ap_srstype(const F1AP_SRSType_t *srs_type, f1ap_srs_type_t *out)
{
  switch (srs_type->present) {
    case F1AP_SRSType_PR_NOTHING:
      out->present = F1AP_SRS_TYPE_PR_NOTHING;
      break;
    case F1AP_SRSType_PR_semipersistentSRS:
      out->present = F1AP_SRS_TYPE_PR_SEMIPERSISTENTSRS;
      out->choice.srs_resource_set_id = calloc_or_fail(1, sizeof(*out->choice.srs_resource_set_id));
      *out->choice.srs_resource_set_id = srs_type->choice.semipersistentSRS->sRSResourceSetID;
      break;
    case F1AP_SRSType_PR_aperiodicSRS:
      out->present = F1AP_SRS_TYPE_PR_APERIODICSRS;
      out->choice.aperiodic = calloc_or_fail(1, sizeof(*out->choice.aperiodic));
      *out->choice.aperiodic = srs_type->choice.aperiodicSRS->aperiodic;
      break;
    default:
      PRINT_ERROR("received illegal SRS type %d\n", srs_type->present);
      return false;
      break;
  }
  return true;
}

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

/**
 * @brief Encode F1 positioning information response to ASN.1
 */
F1AP_F1AP_PDU_t *encode_positioning_information_resp(const f1ap_positioning_information_resp_t *msg)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_PositioningInformationExchange;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_SuccessfulOutcome__value_PR_PositioningInformationResponse;
  F1AP_PositioningInformationResponse_t *out = &tmp->value.choice.PositioningInformationResponse;

  /* mandatory : GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningInformationResponseIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_PositioningInformationResponseIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = msg->gNB_CU_ue_id;

  /* mandatory : GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningInformationResponseIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_PositioningInformationResponseIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = msg->gNB_DU_ue_id;

  /* optional : SRSConfiguration */
  if (msg->srs_configuration) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningInformationResponseIEs_t, ie3);
    ie3->id = F1AP_ProtocolIE_ID_id_SRSConfiguration;
    ie3->criticality = F1AP_Criticality_ignore;
    ie3->value.present = F1AP_PositioningInformationResponseIEs__value_PR_SRSConfiguration;
    f1ap_srs_carrier_list_t *srs_carrier_list = &msg->srs_configuration->srs_carrier_list;
    ie3->value.choice.SRSConfiguration.sRSCarrier_List = encode_srs_carrier_list(srs_carrier_list);
  }

  return pdu;
}

/**
 * @brief Decode F1 positioning information response
 */
bool decode_positioning_information_resp(const F1AP_F1AP_PDU_t *pdu, f1ap_positioning_information_resp_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_PositioningInformationResponse_t *in = &pdu->choice.successfulOutcome->value.choice.PositioningInformationResponse;
  F1AP_PositioningInformationResponseIEs_t *ie;

  F1AP_LIB_FIND_IE(F1AP_PositioningInformationResponseIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID,
                   true);
  F1AP_LIB_FIND_IE(F1AP_PositioningInformationResponseIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID,
                   true);
  F1AP_LIB_FIND_IE(F1AP_PositioningInformationResponseIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_SRSConfiguration,
                   false);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningInformationResponseIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningInformationResponseIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_SRSConfiguration:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningInformationResponseIEs__value_PR_SRSConfiguration);
        F1AP_SRSCarrier_List_t *f1_sRSCarrier_List = &ie->value.choice.SRSConfiguration.sRSCarrier_List;
        out->srs_configuration = calloc_or_fail(1, sizeof(*out->srs_configuration));
        f1ap_srs_carrier_list_t *srs_carrier_list = &out->srs_configuration->srs_carrier_list;
        decode_srs_carrier_list(srs_carrier_list, f1_sRSCarrier_List);
        break;
      case F1AP_ProtocolIE_ID_id_SFNInitialisationTime:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld not handled, skipping\n", ie->id);
        break;
      case F1AP_ProtocolIE_ID_id_CriticalityDiagnostics:
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
 * @brief F1 positioning information response deep copy
 */
f1ap_positioning_information_resp_t cp_positioning_information_resp(const f1ap_positioning_information_resp_t *orig)
{
  /* copy all mandatory fields that are not dynamic memory */
  f1ap_positioning_information_resp_t cp = {
      .gNB_CU_ue_id = orig->gNB_CU_ue_id,
      .gNB_DU_ue_id = orig->gNB_DU_ue_id,
  };

  /* optional: SRS Configuration */
  if (orig->srs_configuration) {
    cp.srs_configuration = calloc_or_fail(1, sizeof(*cp.srs_configuration));
    f1ap_srs_carrier_list_t *srs_carrier_list_cp = &cp.srs_configuration->srs_carrier_list;
    f1ap_srs_carrier_list_t *srs_carrier_list = &orig->srs_configuration->srs_carrier_list;
    *srs_carrier_list_cp = cp_srs_carrier_list(srs_carrier_list);
  }

  return cp;
}

/**
 * @brief F1 positioning information response equality check
 */
bool eq_positioning_information_resp(const f1ap_positioning_information_resp_t *a, const f1ap_positioning_information_resp_t *b)
{
  _F1_EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _F1_EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);

  /* optional: SRS Configuration (O) */
  if ((a->srs_configuration == NULL) != (b->srs_configuration == NULL)) {
    return false;
  }
  if (a->srs_configuration) {
    f1ap_srs_carrier_list_t *srs_carrier_list_a = &a->srs_configuration->srs_carrier_list;
    f1ap_srs_carrier_list_t *srs_carrier_list_b = &b->srs_configuration->srs_carrier_list;
    _F1_CHECK_EXP(eq_srs_carrier_list(srs_carrier_list_a, srs_carrier_list_b));
  }

  return true;
}

/**
 * @brief Free Allocated F1 positioning information response
 */
void free_positioning_information_resp(f1ap_positioning_information_resp_t *msg)
{
  /* SRS Configuration (O) */
  if (msg->srs_configuration) {
    f1ap_srs_carrier_list_t *srs_carrier_list = &msg->srs_configuration->srs_carrier_list;
    free_srs_carrier_list(srs_carrier_list);
    free(msg->srs_configuration);
  }
}

/**
 * @brief Encode F1 positioning information failure to ASN.1
 */
F1AP_F1AP_PDU_t *encode_positioning_information_failure(const f1ap_positioning_information_failure_t *msg)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu->choice.unsuccessfulOutcome, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_PositioningInformationExchange;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_UnsuccessfulOutcome__value_PR_PositioningInformationFailure;
  F1AP_PositioningInformationFailure_t *out = &tmp->value.choice.PositioningInformationFailure;

  /* mandatory : GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningInformationFailureIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_PositioningInformationFailureIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = msg->gNB_CU_ue_id;

  /* mandatory : GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningInformationFailureIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_PositioningInformationFailureIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = msg->gNB_DU_ue_id;

  /* mandatory : Cause */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningInformationFailureIEs_t, ie3);
  ie3->id = F1AP_ProtocolIE_ID_id_Cause;
  ie3->criticality = F1AP_Criticality_ignore;
  ie3->value.present = F1AP_PositioningInformationFailureIEs__value_PR_Cause;
  ie3->value.choice.Cause = encode_f1ap_cause(msg->cause, msg->cause_value);

  return pdu;
}

/**
 * @brief Decode F1 positioning information failure
 */
bool decode_positioning_information_failure(const F1AP_F1AP_PDU_t *pdu, f1ap_positioning_information_failure_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_PositioningInformationFailure_t *in = &pdu->choice.unsuccessfulOutcome->value.choice.PositioningInformationFailure;
  F1AP_PositioningInformationFailureIEs_t *ie;

  F1AP_LIB_FIND_IE(F1AP_PositioningInformationFailureIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID,
                   true);
  F1AP_LIB_FIND_IE(F1AP_PositioningInformationFailureIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID,
                   true);
  F1AP_LIB_FIND_IE(F1AP_PositioningInformationFailureIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_Cause, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningInformationFailureIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningInformationFailureIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_Cause:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningInformationFailureIEs__value_PR_Cause);
        _F1_CHECK_EXP(decode_f1ap_cause(ie->value.choice.Cause, &out->cause, &out->cause_value));
        break;
      case F1AP_ProtocolIE_ID_id_CriticalityDiagnostics:
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
 * @brief F1 positioning information failure deep copy
 */
f1ap_positioning_information_failure_t cp_positioning_information_failure(const f1ap_positioning_information_failure_t *orig)
{
  /* copy all mandatory fields that are not dynamic memory */
  f1ap_positioning_information_failure_t cp = {
      .gNB_CU_ue_id = orig->gNB_CU_ue_id,
      .gNB_DU_ue_id = orig->gNB_DU_ue_id,
      .cause = orig->cause,
      .cause_value = orig->cause_value,
  };

  return cp;
}

/**
 * @brief F1 positioning information failure equality check
 */
bool eq_positioning_information_failure(const f1ap_positioning_information_failure_t *a,
                                        const f1ap_positioning_information_failure_t *b)
{
  _F1_EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _F1_EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);
  _F1_EQ_CHECK_INT(a->cause, b->cause);
  _F1_EQ_CHECK_LONG(a->cause_value, b->cause_value);

  return true;
}

/**
 * @brief Free Allocated F1 positioning information failure
 */
void free_positioning_information_failure(f1ap_positioning_information_failure_t *msg)
{
  // nothing to free
}

/**
 * @brief Encode F1 positioning activation request to ASN.1
 */
F1AP_F1AP_PDU_t *encode_positioning_activation_req(const f1ap_positioning_activation_req_t *msg)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_PositioningActivation;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_InitiatingMessage__value_PR_PositioningActivationRequest;
  F1AP_PositioningActivationRequest_t *out = &tmp->value.choice.PositioningActivationRequest;

  /* mandatory : GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningActivationRequestIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_PositioningActivationRequestIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = msg->gNB_CU_ue_id;

  /* mandatory : GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningActivationRequestIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_PositioningActivationRequestIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = msg->gNB_DU_ue_id;

  /* mandatory : SRS type */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_PositioningActivationRequestIEs_t, ie3);
  ie3->id = F1AP_ProtocolIE_ID_id_SRSType;
  ie3->criticality = F1AP_Criticality_reject;
  ie3->value.present = F1AP_PositioningActivationRequestIEs__value_PR_SRSType;
  ie3->value.choice.SRSType = encode_f1ap_srstype(&msg->srs_type);

  return pdu;
}

/**
 * @brief Decode F1 positioning activation request
 */
bool decode_positioning_activation_req(const F1AP_F1AP_PDU_t *pdu, f1ap_positioning_activation_req_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_PositioningActivationRequest_t *in = &pdu->choice.initiatingMessage->value.choice.PositioningActivationRequest;
  F1AP_PositioningActivationRequestIEs_t *ie;

  F1AP_LIB_FIND_IE(F1AP_PositioningActivationRequestIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID,
                   true);
  F1AP_LIB_FIND_IE(F1AP_PositioningActivationRequestIEs_t,
                   ie,
                   &in->protocolIEs.list,
                   F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID,
                   true);
  F1AP_LIB_FIND_IE(F1AP_PositioningActivationRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_SRSType, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningActivationRequestIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningActivationRequestIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_SRSType:
        _F1_EQ_CHECK_INT(ie->value.present, F1AP_PositioningActivationRequestIEs__value_PR_SRSType);
        _F1_CHECK_EXP(decode_f1ap_srstype(&ie->value.choice.SRSType, &out->srs_type));
        break;
      case F1AP_ProtocolIE_ID_id_ActivationTime:
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
 * @brief F1 positioning activation request deep copy
 */
f1ap_positioning_activation_req_t cp_positioning_activation_req(const f1ap_positioning_activation_req_t *orig)
{
  /* copy all mandatory fields that are not dynamic memory */
  f1ap_positioning_activation_req_t cp = {
      .gNB_CU_ue_id = orig->gNB_CU_ue_id,
      .gNB_DU_ue_id = orig->gNB_DU_ue_id,
      .srs_type.present = orig->srs_type.present,
  };

  switch (orig->srs_type.present) {
    case F1AP_SRS_TYPE_PR_NOTHING:
      // nothing to copy
      break;
    case F1AP_SRS_TYPE_PR_SEMIPERSISTENTSRS:
      cp.srs_type.choice.srs_resource_set_id = calloc_or_fail(1, sizeof(*cp.srs_type.choice.srs_resource_set_id));
      *cp.srs_type.choice.srs_resource_set_id = *orig->srs_type.choice.srs_resource_set_id;
      break;
    case F1AP_SRS_TYPE_PR_APERIODICSRS:
      cp.srs_type.choice.aperiodic = calloc_or_fail(1, sizeof(*cp.srs_type.choice.aperiodic));
      *cp.srs_type.choice.aperiodic = *orig->srs_type.choice.aperiodic;
      break;
    default:
      PRINT_ERROR("received illegal SRS type %d\n", orig->srs_type.present);
      break;
  }

  return cp;
}

/**
 * @brief F1 positioning activation request equality check
 */
bool eq_positioning_activation_req(const f1ap_positioning_activation_req_t *a, const f1ap_positioning_activation_req_t *b)
{
  _F1_EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _F1_EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);
  _F1_EQ_CHECK_INT(a->srs_type.present, b->srs_type.present);

  const f1ap_srs_type_t *srs_type_a = &a->srs_type;
  const f1ap_srs_type_t *srs_type_b = &b->srs_type;
  switch (srs_type_a->present) {
    case F1AP_SRS_TYPE_PR_NOTHING:
      // nothing to check
      break;
    case F1AP_SRS_TYPE_PR_SEMIPERSISTENTSRS:
      _F1_EQ_CHECK_INT(*srs_type_a->choice.srs_resource_set_id, *srs_type_b->choice.srs_resource_set_id);
      break;
    case F1AP_SRS_TYPE_PR_APERIODICSRS:
      _F1_EQ_CHECK_INT(*srs_type_a->choice.aperiodic, *srs_type_b->choice.aperiodic);
      break;
    default:
      PRINT_ERROR("received illegal SRS type %d\n", srs_type_a->present);
      break;
  }

  return true;
}

/**
 * @brief Free Allocated F1 activation request
 */
void free_positioning_activation_req(f1ap_positioning_activation_req_t *msg)
{
  DevAssert(msg != NULL);

  switch (msg->srs_type.present) {
    case F1AP_SRS_TYPE_PR_NOTHING:
      // nothing to free
      return;
    case F1AP_SRS_TYPE_PR_SEMIPERSISTENTSRS:
      free(msg->srs_type.choice.srs_resource_set_id);
      break;
    case F1AP_SRS_TYPE_PR_APERIODICSRS:
      free(msg->srs_type.choice.aperiodic);
      break;
    default:
      PRINT_ERROR("received illegal SRS type %d\n", msg->srs_type.present);
      break;
  }
}
