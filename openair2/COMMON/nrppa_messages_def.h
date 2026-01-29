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

#include "openair2/COMMON/nrppa_messages_types.h"

// LMF -> CU
MESSAGE_DEF(NRPPA_TRP_INFORMATION_REQ, MESSAGE_PRIORITY_MED, nrppa_trp_information_req_t, nrppa_trp_information_req)
MESSAGE_DEF(NRPPA_TRP_INFORMATION_RESP, MESSAGE_PRIORITY_MED, nrppa_trp_information_resp_t, nrppa_trp_information_resp)
MESSAGE_DEF(NRPPA_POSITIONING_INFORMATION_REQ,
            MESSAGE_PRIORITY_MED,
            nrppa_positioning_information_req_t,
            nrppa_positioning_information_req)
MESSAGE_DEF(NRPPA_POSITIONING_INFORMATION_RESP,
            MESSAGE_PRIORITY_MED,
            nrppa_positioning_information_resp_t,
            nrppa_positioning_information_resp)
MESSAGE_DEF(NRPPA_POSITIONING_ACTIVATION_REQ,
            MESSAGE_PRIORITY_MED,
            nrppa_positioning_activation_req_t,
            nrppa_positioning_activation_req)
MESSAGE_DEF(NRPPA_POSITIONING_ACTIVATION_RESP,
            MESSAGE_PRIORITY_MED,
            nrppa_positioning_activation_resp_t,
            nrppa_positioning_activation_resp)
MESSAGE_DEF(NRPPA_MEASUREMENT_REQ, MESSAGE_PRIORITY_MED, nrppa_measurement_req_t, nrppa_measurement_req)
MESSAGE_DEF(NRPPA_MEASUREMENT_RESP, MESSAGE_PRIORITY_MED, nrppa_measurement_resp_t, nrppa_measurement_resp)
