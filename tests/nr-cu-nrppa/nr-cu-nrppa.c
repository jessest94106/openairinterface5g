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
#include "openair3/NAS/NR_UE/nr_nas_msg.h"
#include "executables/nr-uesoftmodem.h"

RAN_CONTEXT_t RC;
THREAD_STRUCT thread_struct;
uint64_t downlink_frequency[MAX_NUM_CCs][4];
int32_t uplink_frequency_offset[MAX_NUM_CCs][4];
int oai_exit = 0;
int gnb_id;
int NB_UE_INST = 1;

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

nrUE_params_t *get_nrUE_params(void)
{
  static nrUE_params_t params = {0};
  params.extra_pdu_id = -1;
  return &params;
}

void create_ue_ip_if(void)
{
}

void create_ue_eth_if(void)
{
}

void set_qfi(void)
{
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

// Initializing UE NAS Context with simulated SIM data
nr_ue_nas_t *simulated_ue_nas = NULL;
void init_ue_nas_context(void)
{
  simulated_ue_nas = calloc_or_fail(1, sizeof(nr_ue_nas_t));
  simulated_ue_nas->UE_id = 1;
  simulated_ue_nas->security_container = calloc_or_fail(1, sizeof(stream_security_container_t));
  // Allocation and simulation of UICC data
  simulated_ue_nas->uicc = calloc_or_fail(1, sizeof(uicc_t));
  // IMSI : 001010000000001
  simulated_ue_nas->uicc->imsiStr = calloc_or_fail(1, 16 * sizeof(char));
  strcpy(simulated_ue_nas->uicc->imsiStr, "001010000000001");
  simulated_ue_nas->uicc->nmc_size = 2;
  // DNN : oai
  simulated_ue_nas->uicc->dnnStr = calloc_or_fail(1, 32 * sizeof(char));
  strcpy(simulated_ue_nas->uicc->dnnStr, "oai");
  // IMEISV : 6754567890123413
  simulated_ue_nas->uicc->imeisvStr = calloc_or_fail(1, 17 * sizeof(char));
  strcpy(simulated_ue_nas->uicc->imeisvStr, "6754567890123413");
  // NSSAI: sst = 1, sd = 0xffffff
  simulated_ue_nas->uicc->nssai_sst = 1;
  simulated_ue_nas->uicc->nssai_sd = 0xffffff;
  // Ki: fec86ba6eb707ed08905757b1bb44b8f
  static const uint8_t TEST_K[16] =
      {0xfe, 0xc8, 0x6b, 0xa6, 0xeb, 0x70, 0x7e, 0xd0, 0x89, 0x05, 0x75, 0x7b, 0x1b, 0xb4, 0x4b, 0x8f};
  memcpy(simulated_ue_nas->uicc->key, TEST_K, 16);
  // OPc: c42449363bbad02b66d16bc975d77cc1
  static const uint8_t TEST_OPC[16] =
      {0xc4, 0x24, 0x49, 0x36, 0x3b, 0xba, 0xd0, 0x2b, 0x66, 0xd1, 0x6b, 0xc9, 0x75, 0xd7, 0x7c, 0xc1};
  memcpy(simulated_ue_nas->uicc->opc, TEST_OPC, 16);
  simulated_ue_nas->security.nas_count_ul = 0;
  simulated_ue_nas->security.nas_count_dl = 0;
  simulated_ue_nas->fiveGMM_state = FGS_DEREGISTERED;
}

// Emulate registration request to register UE in the AMF (required for positioning)
void send_initial_ue_message(instance_t instance)
{
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_NAS_FIRST_REQ);

  NGAP_NAS_FIRST_REQ(msg_p).gNB_ue_ngap_id = 1; // Simulated UE ID

  NGAP_NAS_FIRST_REQ(msg_p).plmn.mcc = 1;
  NGAP_NAS_FIRST_REQ(msg_p).plmn.mnc = 1;
  NGAP_NAS_FIRST_REQ(msg_p).plmn.mnc_digit_length = 2;

  NGAP_NAS_FIRST_REQ(msg_p).nr_cell_id = gnb_id;

  NGAP_NAS_FIRST_REQ(msg_p).establishment_cause = NGAP_RRC_CAUSE_MO_SIGNALLING;

  as_nas_info_t initialNasMsg = {0};
  generateRegistrationRequest(&initialNasMsg, simulated_ue_nas, false);

  NGAP_NAS_FIRST_REQ(msg_p).nas_pdu.buf = initialNasMsg.nas_data;
  NGAP_NAS_FIRST_REQ(msg_p).nas_pdu.len = initialNasMsg.length;

  NGAP_NAS_FIRST_REQ(msg_p).ue_identity.presenceMask = 0;

  itti_send_msg_to_task(TASK_NGAP, instance, msg_p);
}

void *gNB_app_task(void *args_p)
{
  MessageDef *msg_p = NULL;
  const char *msg_name = NULL;
  instance_t instance;
  int result;
  /* for no gcc warnings */
  (void)instance;

  itti_mark_task_ready(TASK_GNB_APP);
  do {
    // Wait for a message
    itti_receive_msg(TASK_GNB_APP, &msg_p);

    msg_name = ITTI_MSG_NAME(msg_p);
    instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);

    switch (ITTI_MSG_ID(msg_p)) {
      case TERMINATE_MESSAGE:
        LOG_W(GNB_APP, " *** Exiting GNB_APP thread\n");
        itti_exit_task();
        break;

      case NGAP_REGISTER_GNB_CNF:
        LOG_I(GNB_APP, "[gNB %ld] Received %s: associated AMF %d\n", instance, msg_name, NGAP_REGISTER_GNB_CNF(msg_p).nb_amf);
        send_initial_ue_message(instance);
        break;

      default:
        LOG_E(GNB_APP, "Received unexpected message %s\n", msg_name);
        break;
    }
    result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
  } while (1);

  return NULL;
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
      case NGAP_DOWNLINK_NAS:
        LOG_I(NR_RRC, "Ignore NGAP_DOWNLINK_NAS: dont have to handle");
        ngap_downlink_nas_t *nas = &NGAP_DOWNLINK_NAS(msg_p);
        if (nas->nas_pdu.buf) {
          free(nas->nas_pdu.buf);
        }
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

  init_ue_nas_context();

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

  rc = itti_create_task(TASK_GNB_APP, gNB_app_task, NULL);
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
