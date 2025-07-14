/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.0  (the "License"); you may not use this file
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

#ifndef NR_UE_RU_H
#define NR_UE_RU_H

#include "PHY/defs_nr_UE.h"
#include "radio/COMMON/common_lib.h"

int nrue_get_ru_count(void);
const nrUE_RU_params_t *nrue_get_ru(int ru_id);
void nrue_set_ru_params(configmodule_interface_t *cfg);

void nrue_init_openair0(const PHY_VARS_NR_UE *ue);

void nrue_ru_start(void);
void nrue_ru_end(void);

#endif
