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

#ifndef NRPPA_GNB_MEASUREMENT_INFORMATION_TRANSFER_H_
#define NRPPA_GNB_MEASUREMENT_INFORMATION_TRANSFER_H_

int nrppa_gNB_handle_measurement_request(nrppa_gnb_ue_info_t *nrppa_msg_info, const NRPPA_NRPPA_PDU_t *pdu);
void decode_srs_carrier_list(nrppa_srs_carrier_list_t *out_list, const NRPPA_SRSCarrier_List_t *in_list);
void free_measurement_request(nrppa_measurement_req_t *msg);
int nrppa_gNB_measurement_response(instance_t instance, MessageDef *msg_p);
NRPPA_TRP_MeasurementResponseList_t encode_trp_measurement_reponse_list(nrppa_measurement_response_list_t *in_list);
void free_measurement_resp(nrppa_measurement_resp_t *msg);

#endif /* NRPPA_GNB_MEASUREMENT_INFORMATION_TRANSFER_H_ */
