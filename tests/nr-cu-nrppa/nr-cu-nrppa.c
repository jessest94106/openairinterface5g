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

#include "executables/softmodem-common.h"
#include "common/utils/ocp_itti/intertask_interface.h"
#include "common/ran_context.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "openair2/GNB_APP/gnb_config_common.h"
#include "openair2/GNB_APP/gnb_config_ng.h"
#include "openair2/GNB_APP/gnb_paramdef.h"
#include "openair3/SCTP/sctp_default_values.h"
#include "openair3/NRPPA/nrppa_gNB_config.h"
#include "openair3/NRPPA/nrppa_gNB.h"

RAN_CONTEXT_t RC;
THREAD_STRUCT thread_struct;
uint64_t downlink_frequency[MAX_NUM_CCs][4];
int32_t uplink_frequency_offset[MAX_NUM_CCs][4];
int oai_exit = 0;
int gnb_id;

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  if (assert) {
    abort();
  } else {
    sleep(1); // allow other threads to exit first
    exit(EXIT_SUCCESS);
  }
}

nfapi_mode_t nfapi_mod = -1;
void nfapi_setmode(nfapi_mode_t nfapi_mode)
{
  nfapi_mod = nfapi_mode;
}
nfapi_mode_t nfapi_getmode(void)
{
  return nfapi_mod;
}

ngran_node_t get_node_type()
{
  return ngran_gNB_CUUP;
}

configmodule_interface_t *uniqCfg = NULL;

int main(int argc, char **argv)
{
  // load the config file
  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == NULL) {
    exit_fun("[SOFTMODEM] Error, configuration module init failed\n");
  }
  logInit();

  set_softmodem_sighandler();

  // Initialize ITTI tasks
  itti_init(TASK_MAX, tasks_info);

  int rc;
  rc = itti_create_task(TASK_SCTP, sctp_eNB_task, NULL);
  AssertFatal(rc >= 0, "Create task for SCTP failed\n");

  rc = itti_create_task(TASK_NGAP, ngap_gNB_task, NULL);
  AssertFatal(rc >= 0, "Create task for NGAP failed\n");

  rc = itti_create_task(TASK_NRPPA, nrppa_gNB_task, NULL);
  AssertFatal(rc >= 0, "Create task for NRPPA failed\n");

  MessageDef *msg_p;
  msg_p = itti_alloc_new_message(TASK_GNB_APP, 0, NGAP_REGISTER_GNB_REQ);
  RCconfig_NR_NG(msg_p, 0);
  gnb_id = NGAP_REGISTER_GNB_REQ(msg_p).gNB_id;

  LOG_I(GNB_APP, "Sending NGAP_REGISTER_GNB_REQ to TASK_NGAP\n");
  itti_send_msg_to_task(TASK_NGAP, 0, msg_p);

  printf("TYPE <CTRL-C> TO TERMINATE\n");
  itti_wait_tasks_end(NULL);

  logClean();
  printf("Bye.\n");
  return 0;
}
