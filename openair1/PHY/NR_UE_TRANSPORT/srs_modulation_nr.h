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

/***********************************************************************
*
* FILENAME    :  srs_modulation_nr.h
*
* MODULE      :
*
* DESCRIPTION :  function to generate uplink reference sequences
*                see 3GPP TS 38.211 6.4.1.4 Sounding reference signal
*
************************************************************************/

#ifndef SRS_MODULATION_NR_H
#define SRS_MODULATION_NR_H

#include "PHY/defs_nr_UE.h"

/*************** FUNCTIONS *****************************************/


/** \brief This function processes srs configuration
 *  @param ue context
    @param rxtx context
    @param current gNB_id identifier
    @returns 0 if srs is transmitted -1 otherwise */
int ue_srs_procedures_nr(PHY_VARS_NR_UE *ue,
                         const UE_nr_rxtx_proc_t *proc,
                         c16_t **txdataF,
                         nr_phy_data_tx_t *phy_data,
                         bool was_symbol_used[NR_NUMBER_OF_SYMBOLS_PER_SLOT]);

#endif /* SRS_MODULATION_NR_H */
