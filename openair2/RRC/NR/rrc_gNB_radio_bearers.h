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

#ifndef _RRC_GNB_DRBS_H_
#define _RRC_GNB_DRBS_H_

#include <stdbool.h>
#include <stdint.h>
#include "NR_DRB-ToAddMod.h"
#include "e1ap_messages_types.h"
#include "nr_rrc_defs.h"

/// @brief Generates an ASN1 DRB-ToAddMod, from the established_drbs in gNB_RRC_UE_t.
/// @param drb_t drb_asn1
/// @return Returns the ASN1 DRB-ToAddMod structs.
NR_DRB_ToAddMod_t *generateDRB_ASN1(const drb_t *drb_asn1);

/// @brief retrieve the data structure representing DRB with ID drb_id of UE ue
drb_t *get_drb(seq_arr_t *seq, int id);

/// @brief Creates and stores a DRB in the gNB_RRC_UE_t struct
/// @param ue The gNB_RRC_UE_t struct that holds information for the UEs
/// @param drb_id The Data Radio Bearer Identity to be created for the established DRB.
/// @param pduSession The PDU Session that the DRB is created for.
/// @param enable_sdap If true the SDAP header will be added to the packet, else it will not add or search for SDAP header.
/// @param do_drb_integrity
/// @param do_drb_ciphering
/// @param pdcp_config
/// @return returns a pointer to the generated DRB structure
drb_t *generateDRB(gNB_RRC_UE_t *ue,
                   const pdusession_t *pduSession,
                   bool enable_sdap,
                   int do_drb_integrity,
                   int do_drb_ciphering,
                   const nr_pdcp_configuration_t *pdcp_config);

/// @brief retrieve PDU session of UE ue with ID id
rrc_pdu_session_param_t *find_pduSession(seq_arr_t *seq, int id);

/// @brief Add a new PDU session for UE @param ue and configuration @param in
rrc_pdu_session_param_t *add_pduSession(seq_arr_t *sessions_ptr, const pdusession_t *in);

/// @brief get PDU session of UE ue through the DRB drb_id
rrc_pdu_session_param_t *find_pduSession_from_drbId(gNB_RRC_UE_t *ue, int drb_id);

/// @brief set PDCP configuration in a bearer context management message
void set_bearer_context_pdcp_config(bearer_context_pdcp_config_t *pdcp_config, drb_t *rrc_drb, bool um_on_default_drb);

void free_pdusession(void *ptr);

/// @brief Add DRB to RRC list
drb_t *nr_rrc_add_drb(seq_arr_t *drb_ptr, int pdusession_id);

/// @brief Function to free DRB in RRC
void free_drb(void *ptr);

#endif
