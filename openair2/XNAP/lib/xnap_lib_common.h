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

#ifndef XNAP_LIB_COMMON_H_
#define XNAP_LIB_COMMON_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"
#include "common/utils/eq_check.h"
#include "xnap_messages_types.h"
#include "xnap_lib_includes.h"

/* macro to look up XnAP IE. If mandatory and not found, macro will print
 * descriptive debug message to stderr and force exit in calling function */
#define XNAP_LIB_FIND_IE(IE_TYPE, ie, IE_LIST, IE_ID, mandatory)                                     \
  do {                                                                                               \
    ie = NULL;                                                                                       \
    for (IE_TYPE **ptr = (IE_LIST)->array; ptr < &(IE_LIST)->array[(IE_LIST)->count]; ptr++) {       \
      if ((*ptr)->id == IE_ID) {                                                                     \
        ie = *ptr;                                                                                   \
        break;                                                                                       \
      }                                                                                              \
    }                                                                                                \
    if ((mandatory) && (ie == NULL)) {                                                               \
      fprintf(stderr, "%s(): could not find XnAP IE " #IE_ID " of type " #IE_TYPE "\n", __func__);   \
      return false;                                                                                  \
    }                                                                                                \
  } while (0)

/* Function Prototypes */
bool eq_xnap_plmn(const plmn_id_t *a, const plmn_id_t *b);
bool eq_xnap_snssai(const nssai_t *a, const nssai_t *b);
bool eq_xnap_tai_support(const xnap_tai_support_t *a, const xnap_tai_support_t *b);
bool eq_xnap_plmn_support(const xnap_plmn_support_t *a, const xnap_plmn_support_t *b);
bool eq_xnap_cause(const xnap_cause_t *a, const xnap_cause_t *b);

int xnap_gNB_set_cause(XNAP_Cause_t *cause_p,const xnap_cause_t *in);
xnap_cause_t decode_xnap_cause(const XNAP_Cause_t *in);

#endif /* XNAP_LIB_COMMON_H_ */
