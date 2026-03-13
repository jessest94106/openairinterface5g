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

#ifndef XNAP_GNB_INTERFACE_MANAGEMENT_H_
#define XNAP_GNB_INTERFACE_MANAGEMENT_H_

#include <stdbool.h>
#include "xnap_messages_types.h"
typedef struct XNAP_XnAP_PDU XNAP_XnAP_PDU_t;

XNAP_XnAP_PDU_t *encode_xn_setup_request(const xnap_setup_req_t *req);
XNAP_XnAP_PDU_t *encode_xn_setup_response(const xnap_setup_resp_t *resp);
XNAP_XnAP_PDU_t *encode_xn_setup_failure(const xnap_setup_failure_t *fail);

bool decode_xn_setup_request(xnap_setup_req_t *req, const XNAP_XnAP_PDU_t *pdu);
bool decode_xn_setup_response(xnap_setup_resp_t *out, const XNAP_XnAP_PDU_t *pdu);
bool decode_xn_setup_failure(xnap_setup_failure_t *out, const XNAP_XnAP_PDU_t *pdu);

bool eq_xnap_setup_request(const xnap_setup_req_t *a, const xnap_setup_req_t *b);
bool eq_xnap_setup_response(const xnap_setup_resp_t *a, const xnap_setup_resp_t *b);
bool eq_xnap_setup_failure(const xnap_setup_failure_t *a, const xnap_setup_failure_t *b);

void free_xnap_setup_request(const xnap_setup_req_t *msg);
void free_xnap_setup_response(const xnap_setup_resp_t *msg);
void free_xnap_setup_failure(xnap_setup_failure_t *msg);

#endif /* XNAP_GNB_INTERFACE_MANAGEMENT_H_ */
