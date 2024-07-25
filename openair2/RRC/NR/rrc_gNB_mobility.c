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

#include <stdlib.h>

#include "assertions.h"
#include "rrc_gNB_mobility.h"

typedef enum { HO_CTX_BOTH, HO_CTX_SOURCE, HO_CTX_TARGET } ho_ctx_type_t;
static nr_handover_context_t *alloc_ho_ctx(ho_ctx_type_t type)
{
  nr_handover_context_t *ho_ctx = calloc(1, sizeof(*ho_ctx));
  AssertFatal(ho_ctx != NULL, "out of memory\n");
  if (type == HO_CTX_SOURCE || type == HO_CTX_BOTH) {
    ho_ctx->source = calloc(1, sizeof(*ho_ctx->source));
    AssertFatal(ho_ctx->source != NULL, "out of memory\n");
  }
  if (type == HO_CTX_TARGET || type == HO_CTX_BOTH) {
    ho_ctx->target = calloc(1, sizeof(*ho_ctx->target));
    AssertFatal(ho_ctx->target != NULL, "out of memory\n");
  }
  return ho_ctx;
}

static void free_ho_ctx(nr_handover_context_t *ho_ctx)
{
  free(ho_ctx->source);
  free(ho_ctx->target);
  free(ho_ctx);
}
