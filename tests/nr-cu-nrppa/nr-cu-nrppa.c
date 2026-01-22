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

static nrppa_trp_information_resp_t fill_trp_resp(uint8_t transaction_id)
{
  positioning_config_t positioning_config = RCconfig_nr_positioning();
  dump_positioning_config(&positioning_config);
  nrppa_trp_information_resp_t resp = {0};
  resp.transaction_id = transaction_id;

  nrppa_trp_information_list_t *list = &resp.trp_information_list;
  uint32_t trp_info_item_len = positioning_config.num_trp;
  list->trp_information_item_length = trp_info_item_len;
  list->trp_information_item = calloc_or_fail(trp_info_item_len, sizeof(*list->trp_information_item));

  uint8_t resp_item_len = 3;
  for (int i = 0; i < trp_info_item_len; i++) {
    nrppa_trp_information_t *tRPInformation = &list->trp_information_item[i];
    tRPInformation->trp_id = positioning_config.trps[i].id;
    nrppa_trp_information_type_response_list_t *resp_list = &tRPInformation->trp_information_type_response_list;
    resp_list->trp_information_type_response_item_length = resp_item_len;
    nrppa_trp_information_type_response_item_t *resp_item = calloc_or_fail(resp_item_len, sizeof(*resp_item));
    resp_list->trp_information_type_response_item = resp_item;

    // nrPCI
    resp_item[0].present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_PCI_NR;
    resp_item[0].choice.pci_nr = 123 + i;

    // nG_RAN_CGI
    resp_item[1].present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NG_RAN_CGI;
    resp_item[1].choice.ng_ran_cgi.plmn.mcc = 001;
    resp_item[1].choice.ng_ran_cgi.plmn.mnc = 01;
    resp_item[1].choice.ng_ran_cgi.plmn.mnc_digit_length = 2;
    resp_item[1].choice.ng_ran_cgi.nr_cellid = gnb_id << 8; // HACK: Made to work with lmf

    // geographicalCoordinates
    resp_item[2].present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_GEOGRAPHICALCOORDINATES;
    nrppa_geographical_coordinates_t *geographicalCoordinates = &resp_item[2].choice.geographical_coordinates;
    nrppa_trp_position_definition_type_t *tRPPositionDefinitionType = &geographicalCoordinates->trp_position_definition_type;
    // referenced
    tRPPositionDefinitionType->present = NRPPA_TRP_POSITION_DEFINITION_TYPE_PR_REFERENCED;
    nrppa_trp_position_referenced_t *referenced = &tRPPositionDefinitionType->choice.referenced;
    // coordinate ID
    referenced->reference_point.present = NRPPA_REFERENCE_POINT_PR_COORDINATEID;
    referenced->reference_point.choice.coordinate_id = 2;
    nrppa_trp_reference_point_type_t *referencePointType = &referenced->reference_point_type;
    // relative cartesian
    referencePointType->present = NRPPA_TRP_REFERENCE_POINT_TYPE_PR_TRPPOSITION_RELATIVE_CARTESIAN;
    nrppa_relative_cartesian_location_t *trp_pos_cart = &referencePointType->choice.trp_position_relative_cartesian;
    // 0 = millimeter
    trp_pos_cart->xyz_unit = positioning_config.trps[i].unit;

    // random reference cartesian coordinates in millimeter
    trp_pos_cart->xvalue = positioning_config.trps[i].x_axis;
    trp_pos_cart->yvalue = positioning_config.trps[i].y_axis;
    trp_pos_cart->zvalue = positioning_config.trps[i].z_axis;
    // testing random values for uncertainity and confidence
    trp_pos_cart->location_uncertainty.horizontal_uncertainty = i + 1;
    trp_pos_cart->location_uncertainty.horizontal_confidence = i + 2;
    trp_pos_cart->location_uncertainty.vertical_uncertainty = i + 3;
    trp_pos_cart->location_uncertainty.vertical_confidence = i + 4;
  }
  return resp;
}

void *rrc_gnb_task(void *args_p)
{
  MessageDef *msg_p;
  MessageDef *msg_p_resp;
  instance_t instance;
  int result;

  itti_mark_task_ready(TASK_RRC_GNB);
  LOG_I(NR_RRC, "Entering main loop of NR_RRC message task\n");

  while (1) {
    // Wait for a message
    itti_receive_msg(TASK_RRC_GNB, &msg_p);
    const char *msg_name_p = ITTI_MSG_NAME(msg_p);
    instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);
    LOG_D(NR_RRC,
          "RRC GNB Task Received %s for instance %ld from task %s\n",
          ITTI_MSG_NAME(msg_p),
          ITTI_MSG_DESTINATION_INSTANCE(msg_p),
          ITTI_MSG_ORIGIN_NAME(msg_p));
    switch (ITTI_MSG_ID(msg_p)) {
      case TERMINATE_MESSAGE:
        LOG_W(NR_RRC, " *** Exiting NR_RRC thread\n");
        itti_exit_task();
        break;
      case NRPPA_TRP_INFORMATION_REQ:
        nrppa_trp_information_req_t *trp_req = &NRPPA_TRP_INFORMATION_REQ(msg_p);
        LOG_I(NR_RRC, "Received NRPPA_TRP_INFORMATION_REQ transaction_id %d\n", trp_req->transaction_id);
        msg_p_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NRPPA_TRP_INFORMATION_RESP);
        NRPPA_TRP_INFORMATION_RESP(msg_p_resp) = fill_trp_resp(trp_req->transaction_id);
        LOG_I(NR_RRC, "Sending NRPPA_TRP_INFORMATION_RESP to TASK_NRPPA\n");
        itti_send_msg_to_task(TASK_NRPPA, 0, msg_p_resp);
        break;
      default:
        LOG_E(NR_RRC, "[gNB %ld] Received unexpected message %s\n", instance, msg_name_p);
        break;
    }
    result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
    msg_p = NULL;
  }
}

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

  rc = itti_create_task(TASK_RRC_GNB, rrc_gnb_task, NULL);
  AssertFatal(rc >= 0, "Create task for RRC failed\n");

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
