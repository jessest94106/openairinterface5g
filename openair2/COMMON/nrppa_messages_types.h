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

#ifndef NRPPA_MESSAGES_TYPES_H_
#define NRPPA_MESSAGES_TYPES_H_

// Defines to access message fields.

#define NRPPA_TRP_INFORMATION_REQ(mSGpTR) (mSGpTR)->ittiMsg.nrppa_trp_information_req

/* Structure of Positioning related NRPPA messages */
/* IE structures for Positioning related messages as per TS 38.455 V16.7.1*/

typedef enum nrppa_trp_information_type_item_e {
  NRPPA_TRP_INFORMATION_TYPE_ITEM_NR_PCI,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_NG_RAN_CGI,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_NR_ARFCN,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_PRS_CONFIG,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_SSB_CONFIG,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_SFN_INIT_TIME,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_SPATIAL_DIRECTION_INFO,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_GEO_COORDINATES
} nrppa_trp_information_type_item_pr;

typedef struct nrppa_trp_information_type_list_s {
  nrppa_trp_information_type_item_pr *trp_information_type_item;
  uint8_t trp_information_type_list_length;
} nrppa_trp_information_type_list_t;

typedef struct nrppa_trp_list_item_s {
  uint32_t trp_id;
} nrppa_trp_list_item_t;

typedef struct nrppa_trp_list_s {
  nrppa_trp_list_item_t *trp_list_item;
  uint32_t trp_list_length;
} nrppa_trp_list_t;

typedef struct nrppa_trp_information_req_s {
  // IE 9.2.4 (mandatory)
  uint16_t transaction_id;
  bool has_trp_list;
  // mandatory
  nrppa_trp_list_t trp_list;
  // mandatory
  nrppa_trp_information_type_list_t trp_information_type_list;
} nrppa_trp_information_req_t;

#endif // NRPPA_MESSAGES_TYPES_H_
