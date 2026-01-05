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

#ifndef RRC_CELL_MANAGEMENT_H_
#define RRC_CELL_MANAGEMENT_H_

#include "nr_rrc_defs.h" // for nr_rrc_du_container_t, nr_rrc_cell_container_t, RB_HEAD
#include "common/utils/collection/tree.h"
#include <stdbool.h>
#include <stdint.h>

// Generate RB tree functions (prototypes)
int rrc_cell_cmp(const struct nr_rrc_cell_container_t *a, const struct nr_rrc_cell_container_t *b);
RB_PROTOTYPE(rrc_cell_tree, nr_rrc_cell_container_t, entries, rrc_cell_cmp);

// Cell management (global cell tree)
nr_rrc_cell_container_t *get_cell_by_cell_id(struct rrc_cell_tree *cells, const uint64_t cell_id);
nr_rrc_cell_container_t *rrc_add_cell(gNB_RRC_INST *rrc, nr_rrc_cell_container_t *cell);
void rrc_rm_cell(gNB_RRC_INST *rrc, nr_rrc_cell_container_t *cell);

// Cell management (DU-specific cell tree)
nr_rrc_cell_container_t *rrc_get_cell_for_du(seq_arr_t *cells, uint64_t cell_id);
nr_rrc_cell_container_t *rrc_get_cell_by_pci_for_du(const seq_arr_t *cells, uint16_t pci);
nr_rrc_cell_container_t *rrc_add_cell_to_du(seq_arr_t *cells, nr_rrc_cell_container_t *cell);

// Cell cleanup
void rrc_free_cell_container(nr_rrc_cell_container_t *cell);

#endif /* RRC_CELL_MANAGEMENT_H_ */
