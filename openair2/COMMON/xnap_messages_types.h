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

#ifndef XNAP_MESSAGES_TYPES_H_
#define XNAP_MESSAGES_TYPES_H_

#include "common/5g_platform_types.h"

typedef struct {
  // PLMN Identity
  plmn_id_t plmn;
  // Number of supported S-NSSAIs
  uint16_t num_nssai;
  // List of supported S-NSSAIs
  nssai_t *nssai;
} xnap_plmn_support_t;

typedef struct {
  // Tracking Area Code
  uint32_t tac;
  // Number of supported PLMNs
  uint8_t num_plmn;
  // List of supported PLMNs
  xnap_plmn_support_t *plmn_support;
} xnap_tai_support_t;

typedef struct {
  // PLMN Identity
  plmn_id_t plmn;
  // AMF Region Identifier
  uint8_t amf_region_id;
} xnap_amf_region_info_t;

/* 3GPP TS 38.423 9.1.3.1 – Xn Setup Request */
typedef struct {
  // Global NG-RAN Node ID (gNB_id+plmn) (M)
  uint32_t gNB_id;
  plmn_id_t plmn;
  // TAI Support List (M)
  uint16_t num_tai;
  xnap_tai_support_t *tai_support;
  // AMF Region Information (M)
  uint8_t num_amf_regions;
  xnap_amf_region_info_t *amf_region_info;
} xnap_setup_req_t;

#endif /* XNAP_MESSAGES_TYPES_H_ */
