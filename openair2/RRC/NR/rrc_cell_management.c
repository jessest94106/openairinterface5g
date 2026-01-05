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

#include <stdbool.h>
#include <stdint.h>
#include "rrc_cell_management.h"
#include "common/utils/assertions.h" // for DevAssert
#include "common/utils/collection/tree.h" // for RB_FOREACH
#include "common/utils/alg/find.h" // for find_if
#include "common/utils/ds/seq_arr.h" // for seq_arr_t, seq_arr_free
#include "common/utils/LOG/log.h" // for LOG_I, LOG_W, LOG_E
#include "asn_application.h" // for ASN_STRUCT_FREE
#include "NR_MIB.h" // for asn_DEF_NR_MIB
#include "NR_SIB1.h" // for asn_DEF_NR_SIB1
#include "NR_MeasurementTimingConfiguration.h" // for asn_DEF_NR_MeasurementTimingConfiguration

/** @brief Comparison function for cell tree
 * @param[in] a Pointer to first cell container
 * @param[in] b Pointer to second cell container
 * @return Comparison result */
int rrc_cell_cmp(const struct nr_rrc_cell_container_t *a, const struct nr_rrc_cell_container_t *b)
{
  if (a->info.cell_id < b->info.cell_id)
    return -1;
  if (a->info.cell_id > b->info.cell_id)
    return 1;
  return 0;
}

/** @brief Generate RB tree functions for cell tree */
RB_GENERATE(rrc_cell_tree, nr_rrc_cell_container_t, entries, rrc_cell_cmp);

/** @brief Find cell container by cell ID (lookup in global cell tree)
 * @param[in] cells Pointer to global cell tree
 * @param[in] cell_id Cell ID
 * @return Pointer to the cell container, or NULL if not found
 * @note O(log N_CELL) using RB-tree lookup */
nr_rrc_cell_container_t *get_cell_by_cell_id(struct rrc_cell_tree *cells, const uint64_t cell_id)
{
  DevAssert(cells != NULL);
  nr_rrc_cell_container_t search_key = {.info = {.cell_id = cell_id}};
  return RB_FIND(rrc_cell_tree, cells, &search_key);
}

/** @brief Add cell to global cell tree and increment rrc->num_cells on success
 * @param[in] rrc Pointer to RRC instance
 * @param[in] cell Pointer to cell container to add
 * @return NULL if inserted successfully; pointer to existing cell (collision) if duplicate cell_id */
nr_rrc_cell_container_t *rrc_add_cell(gNB_RRC_INST *rrc, nr_rrc_cell_container_t *cell)
{
  DevAssert(rrc != NULL);
  DevAssert(cell != NULL);
  nr_rrc_cell_container_t *collision = RB_INSERT(rrc_cell_tree, &rrc->cells, cell);
  if (collision == NULL)
    rrc->num_cells++;
  return collision;
}

/** @brief Remove cell from global cell tree, decrement rrc->num_cells, and free the cell
 * @param[in] rrc Pointer to RRC instance
 * @param[in] cell Pointer to cell container to remove */
void rrc_rm_cell(gNB_RRC_INST *rrc, nr_rrc_cell_container_t *cell)
{
  DevAssert(rrc != NULL);
  DevAssert(cell != NULL);
  nr_rrc_cell_container_t *removed = RB_REMOVE(rrc_cell_tree, &rrc->cells, cell);
  AssertFatal(removed == cell, "Failed to remove cell %ld from tree\n", cell->info.cell_id);
  rrc->num_cells--;
  rrc_free_cell_container(cell);
}

static bool eq_cell_id(const void *vval, const void *vit)
{
  const uint64_t *id = (const uint64_t *)vval;
  const nr_rrc_cell_container_t **elem_ptr = (const nr_rrc_cell_container_t **)vit;
  return (*elem_ptr) != NULL && (*elem_ptr)->info.cell_id == *id;
}

/** @brief Get cell by cell_id from DU's cells array
 * @param[in] cells Pointer to DU's cells sequence array
 * @param[in] cell_id Cell ID to search for
 * @return Pointer to the cell container, or NULL if not found */
nr_rrc_cell_container_t *rrc_get_cell_for_du(seq_arr_t *cells, uint64_t cell_id)
{
  DevAssert(cells);
  elm_arr_t elm = find_if(cells, &cell_id, eq_cell_id);
  if (elm.found) {
    // elm.it points to an element in seq_arr, which stores nr_rrc_cell_container_t*
    nr_rrc_cell_container_t **cell_ptr = (nr_rrc_cell_container_t **)elm.it;
    return *cell_ptr;
  }
  return NULL;
}

static bool eq_pci_in_du(const void *vval, const void *vit)
{
  const uint16_t *pci = (const uint16_t *)vval;
  const nr_rrc_cell_container_t **elem_ptr = (const nr_rrc_cell_container_t **)vit;
  return (*elem_ptr) != NULL && (*elem_ptr)->info.pci == *pci;
}

/** @brief Get cell by PCI from DU's cells array
 * @param[in] cells Pointer to DU's cells sequence array
 * @param[in] pci PCI to search for
 * @return Pointer to the cell container, or NULL if not found
 * @note PCI must be unique within a DU; returns the first match */
nr_rrc_cell_container_t *rrc_get_cell_by_pci_for_du(const seq_arr_t *cells, uint16_t pci)
{
  DevAssert(cells);
  elm_arr_t elm = find_if((seq_arr_t *)cells, &pci, eq_pci_in_du);
  if (elm.found) {
    // elm.it points to an element in seq_arr, which stores nr_rrc_cell_container_t*
    nr_rrc_cell_container_t **cell_ptr = (nr_rrc_cell_container_t **)elm.it;
    return *cell_ptr;
  }
  return NULL;
}

/** @brief Add cell to DU's cells array
 * @param[in] cells Pointer to DU's cells sequence array
 * @param[in] cell Pointer to cell container to add
 * @return Pointer to the added cell, or NULL on duplicate cell_id/PCI or error
 * @note Rejects duplicate cell_id or PCI within the DU */
nr_rrc_cell_container_t *rrc_add_cell_to_du(seq_arr_t *cells, nr_rrc_cell_container_t *cell)
{
  DevAssert(cells);
  DevAssert(cell);

  // Check for duplicate cell_id within this DU
  nr_rrc_cell_container_t *exists = rrc_get_cell_for_du(cells, cell->info.cell_id);
  if (exists) {
    LOG_E(NR_RRC, "Trying to add an already existing cell with ID=%lu\n", cell->info.cell_id);
    return NULL;
  }

  // Check for duplicate PCI within this DU (PCI must be unique within a DU)
  nr_rrc_cell_container_t *existing_pci = rrc_get_cell_by_pci_for_du(cells, cell->info.pci);
  if (existing_pci != NULL) {
    LOG_E(NR_RRC,
          "Trying to add cell with ID=%lu, but PCI %d already exists in this DU (cell ID %lu)\n",
          cell->info.cell_id,
          cell->info.pci,
          existing_pci->info.cell_id);
    return NULL;
  }

  seq_arr_push_back(cells, &cell, sizeof(nr_rrc_cell_container_t *));
  LOG_I(NR_RRC, "Added cell %lu to DU cells array (total cells = %ld)\n", cell->info.cell_id, seq_arr_size(cells));

  return cell;
}

/** @brief Free a cell container and all its associated resources
 * @param[in] cell Pointer to the cell container to free
 * @note Frees ASN.1 structures (mib, sib1, mtc) and the cell container itself
 * @note Does NOT remove the cell from the tree - caller must do RB_REMOVE first */
void rrc_free_cell_container(nr_rrc_cell_container_t *cell)
{
  DevAssert(cell);
  ASN_STRUCT_FREE(asn_DEF_NR_MIB, cell->mib);
  ASN_STRUCT_FREE(asn_DEF_NR_SIB1, cell->sib1);
  ASN_STRUCT_FREE(asn_DEF_NR_MeasurementTimingConfiguration, cell->mtc);
  free(cell);
}
