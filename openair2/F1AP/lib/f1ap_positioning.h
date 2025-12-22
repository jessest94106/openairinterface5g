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

#ifndef F1AP_POSITIONING_H_
#define F1AP_POSITIONING_H_

#include <stdbool.h>
#include "f1ap_messages_types.h"

struct F1AP_F1AP_PDU;

// positioning information request
struct F1AP_F1AP_PDU *encode_positioning_information_req(const f1ap_positioning_information_req_t *msg);
bool decode_positioning_information_req(const struct F1AP_F1AP_PDU *pdu, f1ap_positioning_information_req_t *out);
f1ap_positioning_information_req_t cp_positioning_information_req(const f1ap_positioning_information_req_t *orig);
bool eq_positioning_information_req(const f1ap_positioning_information_req_t *a, const f1ap_positioning_information_req_t *b);
void free_positioning_information_req(f1ap_positioning_information_req_t *msg);

// positioning information response
struct F1AP_F1AP_PDU *encode_positioning_information_resp(const f1ap_positioning_information_resp_t *msg);
bool decode_positioning_information_resp(const struct F1AP_F1AP_PDU *pdu, f1ap_positioning_information_resp_t *out);
f1ap_positioning_information_resp_t cp_positioning_information_resp(const f1ap_positioning_information_resp_t *orig);
bool eq_positioning_information_resp(const f1ap_positioning_information_resp_t *a, const f1ap_positioning_information_resp_t *b);
void free_positioning_information_resp(f1ap_positioning_information_resp_t *msg);

// positioning information failure
struct F1AP_F1AP_PDU *encode_positioning_information_failure(const f1ap_positioning_information_failure_t *msg);
bool decode_positioning_information_failure(const struct F1AP_F1AP_PDU *pdu, f1ap_positioning_information_failure_t *out);
f1ap_positioning_information_failure_t cp_positioning_information_failure(const f1ap_positioning_information_failure_t *orig);
bool eq_positioning_information_failure(const f1ap_positioning_information_failure_t *a,
                                        const f1ap_positioning_information_failure_t *b);
void free_positioning_information_failure(f1ap_positioning_information_failure_t *msg);

// positioning activation request
struct F1AP_F1AP_PDU *encode_positioning_activation_req(const f1ap_positioning_activation_req_t *msg);
bool decode_positioning_activation_req(const struct F1AP_F1AP_PDU *pdu, f1ap_positioning_activation_req_t *out);
f1ap_positioning_activation_req_t cp_positioning_activation_req(const f1ap_positioning_activation_req_t *orig);
bool eq_positioning_activation_req(const f1ap_positioning_activation_req_t *a, const f1ap_positioning_activation_req_t *b);
void free_positioning_activation_req(f1ap_positioning_activation_req_t *msg);

// positioning activation response
struct F1AP_F1AP_PDU *encode_positioning_activation_resp(const f1ap_positioning_activation_resp_t *msg);
bool decode_positioning_activation_resp(const struct F1AP_F1AP_PDU *pdu, f1ap_positioning_activation_resp_t *out);
f1ap_positioning_activation_resp_t cp_positioning_activation_resp(const f1ap_positioning_activation_resp_t *orig);
bool eq_positioning_activation_resp(const f1ap_positioning_activation_resp_t *a, const f1ap_positioning_activation_resp_t *b);
void free_positioning_activation_resp(f1ap_positioning_activation_resp_t *msg);

// positioning activation failure
struct F1AP_F1AP_PDU *encode_positioning_activation_failure(const f1ap_positioning_activation_failure_t *msg);
bool decode_positioning_activation_failure(const struct F1AP_F1AP_PDU *pdu, f1ap_positioning_activation_failure_t *out);
f1ap_positioning_activation_failure_t cp_positioning_activation_failure(const f1ap_positioning_activation_failure_t *orig);
bool eq_positioning_activation_failure(const f1ap_positioning_activation_failure_t *a,
                                       const f1ap_positioning_activation_failure_t *b);
void free_positioning_activation_failure(f1ap_positioning_activation_failure_t *msg);

// positioning deactivation
struct F1AP_F1AP_PDU *encode_positioning_deactivation(const f1ap_positioning_deactivation_t *msg);
bool decode_positioning_deactivation(const struct F1AP_F1AP_PDU *pdu, f1ap_positioning_deactivation_t *out);
f1ap_positioning_deactivation_t cp_positioning_deactivation(const f1ap_positioning_deactivation_t *orig);
bool eq_positioning_deactivation(const f1ap_positioning_deactivation_t *a, const f1ap_positioning_deactivation_t *b);
void free_positioning_deactivation(f1ap_positioning_deactivation_t *msg);

#endif /* F1AP_POSITIONING_H_ */
