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

#ifndef NRPPA_GNB_POSITIONING_PROCEDURES_H_
#define NRPPA_GNB_POSITIONING_PROCEDURES_H_

int nrppa_gNB_handle_trp_information_request(nrppa_gnb_ue_info_t *nrppa_msg_info, const NRPPA_NRPPA_PDU_t *pdu);
void free_trp_information_request(nrppa_trp_information_req_t *msg);
int nrppa_gNB_trp_information_response(instance_t instance, MessageDef *msg_p);
void free_trp_information_request(nrppa_trp_information_req_t *msg);
NRPPA_TRPInformationItem_t encode_trp_info_type_response_item_nrppa(nrppa_trp_information_type_response_item_t *in);
void free_trp_information_response(nrppa_trp_information_resp_t *msg);
int nrppa_gNB_handle_positioning_information_request(nrppa_gnb_ue_info_t *nrppa_msg_info, const NRPPA_NRPPA_PDU_t *pdu);
int nrppa_gNB_positioning_information_response(instance_t instance, MessageDef *msg_p);
NRPPA_SRSCarrier_List_t encode_srs_carrier_list_nrppa(const nrppa_srs_carrier_list_t *in_list);
void free_srs_carrier_list(nrppa_srs_carrier_list_t *srs_carrier_list);
void free_positioning_information_response(nrppa_positioning_information_resp_t *msg);
int nrppa_gNB_handle_positioning_activation_request(nrppa_gnb_ue_info_t *nrppa_msg_info, const NRPPA_NRPPA_PDU_t *pdu);
void decode_nrppa_srstype(NRPPA_SRSType_t *srs_type, nrppa_srs_type_t *out);
void free_positioning_activation_request(nrppa_positioning_activation_req_t *msg);
int nrppa_gNB_positioning_activation_response(instance_t instance, MessageDef *msg_p);

#endif /* NRPPA_GNB_POSITIONING_PROCEDURES_H_ */
