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

#ifndef RAL_MESSAGES_TYPES_H_
#define RAL_MESSAGES_TYPES_H_
#include "asn_constant.h"
#include "common/platform_types.h"
#include "commonDef.h"
/* 802.21 mappings with the RRC protocol :
(Research Report RR-12-265 Mapping of IEEE 802.21 MIH primitives to EPS/LTE protocols March 13 th, 2012, Michelle WETTERWALD, Prof. Christian BONNET )
LINK EVENTS:
    Link_Detected       System Information

    Link_Up           RRC Connection establishment
                  RRC Connection re-establishment
                  RRC Connection reconfiguration

    Link_Down         RRC Connection reconfiguration
                  RRC Connection Release

    Link_Parameters_Report    Measurement report
    Link_Going_Down       N/A
    Link_Handover_Imminent    N/A
    Link_Handover_Complete    RRC Connection reconfiguration
    Link_PDU_Transmit_Status  N/A


LINK COMMANDS:
    Link_Capability_Discover  N/A
    Link_Event_Subscribe    Measurement configuration
    Link_Event_Unsubscribe    Measurement configuration
    Link_Get_Parameters     Measurement configuration
    Link_Configure_Thresholds Measurement configuration
    Link_Action / Disconnect  RRC Connection Release
    Link_Action / Low Power   N/A
    Link_Action / Power Down  RRC Connection Release
    Link_Action / Power Up    RRC Connection establishment
*/

#define RRC_RAL_SYSTEM_CONFIGURATION_IND(mSGpTR)          (mSGpTR)->ittiMsg.rrc_ral_system_configuration_ind
#define RRC_RAL_SYSTEM_INFORMATION_IND(mSGpTR)            (mSGpTR)->ittiMsg.rrc_ral_system_information_ind

#define RRC_RAL_SCAN_REQ(mSGpTR)                          (mSGpTR)->ittiMsg.rrc_ral_scan_req
#define RRC_RAL_SCAN_CONF(mSGpTR)                         (mSGpTR)->ittiMsg.rrc_ral_scan_conf

#define RRC_RAL_CONFIGURE_THRESHOLD_REQ(mSGpTR)           (mSGpTR)->ittiMsg.rrc_ral_configure_threshold_req
#define RRC_RAL_CONFIGURE_THRESHOLD_CONF(mSGpTR)          (mSGpTR)->ittiMsg.rrc_ral_configure_threshold_conf
#define RRC_RAL_MEASUREMENT_REPORT_IND(mSGpTR)            (mSGpTR)->ittiMsg.rrc_ral_measurement_report_ind

#define RRC_RAL_CONNECTION_ESTABLISHMENT_REQ(mSGpTR)      (mSGpTR)->ittiMsg.rrc_ral_connection_establishment_req
#define RRC_RAL_CONNECTION_ESTABLISHMENT_CONF(mSGpTR)     (mSGpTR)->ittiMsg.rrc_ral_connection_establishment_conf
#define RRC_RAL_CONNECTION_ESTABLISHMENT_IND(mSGpTR)      (mSGpTR)->ittiMsg.rrc_ral_connection_establishment_ind
#define RRC_RAL_CONNECTION_REESTABLISHMENT_IND(mSGpTR)    (mSGpTR)->ittiMsg.rrc_ral_connection_reestablishment_ind
#define RRC_RAL_CONNECTION_RECONFIGURATION_REQ(mSGpTR)    (mSGpTR)->ittiMsg.rrc_ral_connection_reconfiguration_req
#define RRC_RAL_CONNECTION_RECONFIGURATION_IND(mSGpTR)    (mSGpTR)->ittiMsg.rrc_ral_connection_reconfiguration_ind
#define RRC_RAL_CONNECTION_RECONFIGURATION_HO_IND(mSGpTR) (mSGpTR)->ittiMsg.rrc_ral_connection_reconfiguration_ho_ind

#define RRC_RAL_CONNECTION_RELEASE_REQ(mSGpTR)            (mSGpTR)->ittiMsg.rrc_ral_connection_release_req
#define RRC_RAL_CONNECTION_RELEASE_CONF(mSGpTR)           (mSGpTR)->ittiMsg.rrc_ral_connection_release_conf
#define RRC_RAL_CONNECTION_RELEASE_IND(mSGpTR)            (mSGpTR)->ittiMsg.rrc_ral_connection_release_ind

#include "MIH_C_header_codec.h"

typedef MIH_C_TRANSACTION_ID_T ral_transaction_id_t;
typedef MIH_C_STATUS_T         ral_status_t;
typedef MIH_C_CHOICE_T         ral_choice_t;
typedef MIH_C_CONFIG_STATUS_T  ral_config_status_t;
typedef MIH_C_TH_ACTION_T      ral_th_action_t;
typedef MIH_C_LINK_ACTION_T      ral_link_action_t;

#define RAL_LINK_ADDR_LIST_LENGTH        MIH_C_LINK_ADDR_LIST_LENGTH

typedef  struct ral_network_id_s {
  u_int16_t length;
  u_int8_t val[253];
} ral_network_id_t;

typedef  struct ral_link_addr_s {
  u_int16_t length;
  u_int8_t val[RAL_LINK_ADDR_LIST_LENGTH];
} ral_link_addr_t;

typedef struct ral_sig_strength_s {
  ral_choice_t               choice;
  union  {
    uint8_t    dbm;
    uint8_t   percentage;
  } _union;
} ral_sig_strength_t;
#define RAL_SIG_STRENGTH_CHOICE_DBM        (ral_choice_t)0
#define RAL_SIG_STRENGTH_CHOICE_PERCENTAGE (ral_choice_t)1


// FOR ENB ONLY
typedef struct rrc_ral_system_configuration_ind_s {
  plmn_t       plmn_id;
  unsigned int cell_id:28;
} rrc_ral_system_configuration_ind_t;

// FOR UE ONLY
typedef struct rrc_ral_system_information_ind_s {
  plmn_t       plmn_id;
  unsigned int cell_id:28;
  uint8_t      dbm;        // sig strength
  uint16_t     sinr;
  uint32_t     link_data_rate;  //the maximum data rate in kb/s

} rrc_ral_system_information_ind_t;

typedef struct ral_link_scan_resp_s {
  ral_link_addr_t            link_addr;
  ral_network_id_t           network_id;
  ral_sig_strength_t         sig_strength;
} ral_link_scan_resp_t;

typedef struct rrc_ral_scan_req_s {
  ral_transaction_id_t transaction_id;

} rrc_ral_scan_req_t;

typedef struct rrc_ral_scan_conf_s {
  ral_transaction_id_t transaction_id;
#define RAL_MAX_LINK_SCAN_RESP 16
  uint8_t                  num_scan_resp;
  ral_link_scan_resp_t     link_scan_resp[RAL_MAX_LINK_SCAN_RESP];
} rrc_ral_scan_conf_t;

typedef struct rrc_ral_connection_establishment_req_s {
  ral_transaction_id_t transaction_id;

} rrc_ral_connection_establishment_req_t;

typedef struct rrc_ral_connection_establishment_conf_s {
  ral_transaction_id_t transaction_id;

} rrc_ral_connection_establishment_conf_t;

typedef struct rrc_ral_connection_establishment_ind_s {
  uint16_t             ue_id; // may be c-rnti
} rrc_ral_connection_establishment_ind_t;


typedef struct rrc_ral_connection_reestablishment_ind_s {
  uint16_t             ue_id;
  uint8_t              num_drb;
  rb_id_t              drb_id[maxDRB];
  uint8_t              num_srb;
} rrc_ral_connection_reestablishment_ind_t;

typedef struct rrc_ral_connection_reconfiguration_req_s {
  ral_transaction_id_t transaction_id;
  uint16_t             ue_id;
  ral_link_action_t    link_action;
  //     uint8_t              num_drb;
  //     rb_id_t              drb_id[maxDRB];
  //     uint8_t              num_srb;
} rrc_ral_connection_reconfiguration_req_t;


typedef struct rrc_ral_connection_reconfiguration_ind_s {
  uint16_t             ue_id;
  uint8_t              num_drb;
  rb_id_t              drb_id[maxDRB];
  uint8_t              num_srb;
} rrc_ral_connection_reconfiguration_ind_t;

typedef struct rrc_ral_connection_reconfiguration_ho_ind_s {
  uint16_t             ue_id;
  uint8_t              num_drb;
  rb_id_t              drb_id[maxDRB];
  uint8_t              num_srb;
  //unsigned int cell_id:28;
} rrc_ral_connection_reconfiguration_ho_ind_t;

typedef struct ral_threshold_s {
  uint16_t        threshold_val;
  uint8_t         threshold_xdir;
} ral_threshold_t;


typedef MIH_C_LINK_PARAM_GEN_T                                 ral_link_param_gen_t;
#define RAL_LINK_PARAM_GEN_PACKET_ERROR_RATE                   MIH_C_LINK_PARAM_GEN_PACKET_ERROR_RATE
#define RAL_LINK_PARAM_GEN_MAX                                 RAL_LINK_PARAM_GEN_PACKET_ERROR_RATE

typedef MIH_C_LINK_PARAM_QOS_T                                 ral_link_param_qos_t;
#define RAL_LINK_PARAM_QOS_PACKET_LOSS_RATE_ALL_COS_FRAME_RATIO MIH_C_LINK_PARAM_QOS_PACKET_LOSS_RATE_ALL_COS_FRAME_RATIO

typedef MIH_C_LINK_PARAM_LTE_T                                 ral_link_param_lte_t;
#define RAL_LINK_PARAM_LTE_NUM_ACTIVE_EMBMS_RECEIVERS_PER_FLOW MIH_C_LINK_PARAM_LTE_NUM_ACTIVE_EMBMS_RECEIVERS_PER_FLOW
#define RAL_LINK_PARAM_LTE_MAX                                 RAL_LINK_PARAM_LTE_NUM_ACTIVE_EMBMS_RECEIVERS_PER_FLOW

#define RAL_LINK_PARAM_TYPE_CHOICE_GEN       MIH_C_LINK_PARAM_TYPE_CHOICE_GEN  // 0
#define RAL_LINK_PARAM_TYPE_CHOICE_LTE       MIH_C_LINK_PARAM_TYPE_CHOICE_LTE  // 12

typedef struct ral_link_param_type_s {
  ral_choice_t                    choice;
  union  {
    ral_link_param_gen_t      link_param_gen;
    ral_link_param_qos_t      link_param_qos;
    ral_link_param_lte_t      link_param_lte;
  } _union;
} ral_link_param_type_t;

typedef uint16_t               ral_link_param_val_t;
typedef MIH_C_NUM_COS_TYPES_T  ral_num_cos_types_t  ;

typedef struct ral_qos_param_val_s {
  ral_choice_t                 choice;
  union  {
    ral_num_cos_types_t      num_qos_types;
    LIST(MIH_C_MIN_PK_TX_DELAY, min_pk_tx_delay)
    LIST(MIH_C_AVG_PK_TX_DELAY, avg_pk_tx_delay)
    LIST(MIH_C_MAX_PK_TX_DELAY, max_pk_tx_delay)
    LIST(MIH_C_PK_DELAY_JITTER, pk_delay_jitter)
    LIST(MIH_C_PK_LOSS_RATE,    pk_loss_rate)
  } _union;
} ral_qos_param_val_t;

typedef struct ral_link_param_s {
  ral_link_param_type_t      link_param_type;
  ral_choice_t               choice;
  union  {
    ral_link_param_val_t   link_param_val;
    ral_qos_param_val_t    qos_param_val;
  } _union;
} ral_link_param_t;

typedef struct ral_link_cfg_param_s {
  ral_link_param_type_t        link_param_type;
  uint8_t                      union_choice;
  union  {
    uint8_t    null_attr;
    uint16_t   timer_interval;         // This timer value (ms) is used to set the interval between periodic reports.
  } _union;
  ral_th_action_t             th_action; // indicates what action to apply to the listed thresholds.
  // When “Cancel threshold” is selected and no thresholds are specified, then
  // all currently configured thresholds for the given LINK_PARAM_TYPE are can-
  // celled.
  // When “Cancel threshold” is selected and thresholds are specified only those
  // configured thresholds for the given LINK_PARAM_TYPE and whose threshold
  // value match what was specified are cancelled.
  // With “Set one-shot threshold” the listed thresholds are first set and then
  // each of the threshold is cancelled as soon as it is crossed for the first
  // time.
  uint8_t             num_thresholds;
#define RAL_LINK_CFG_PARAM_MAX_THRESHOLDS 16
  ral_threshold_t     thresholds[RAL_LINK_CFG_PARAM_MAX_THRESHOLDS];
} ral_link_cfg_param_t;


typedef struct rrc_ral_configure_threshold_req_s {
  ral_transaction_id_t     transaction_id;
#define RAL_MAX_LINK_CFG_PARAMS 16
  uint8_t                  num_link_cfg_params;
  ral_link_cfg_param_t     link_cfg_params[RAL_MAX_LINK_CFG_PARAMS];
} rrc_ral_configure_threshold_req_t;

typedef struct ral_link_cfg_status_s {
  ral_link_param_type_t      link_param_type;
  ral_threshold_t            threshold;
  ral_config_status_t        config_status;
} ral_link_cfg_status_t;

typedef struct rrc_ral_configure_threshold_conf_s {
  ral_transaction_id_t     transaction_id;
  ral_status_t             status;
  uint8_t                  num_link_cfg_params;
  ral_link_cfg_status_t    cfg_status[RAL_MAX_LINK_CFG_PARAMS];
} rrc_ral_configure_threshold_conf_t;

typedef struct rrc_ral_measurement_report_ind_s {
  ral_threshold_t            threshold;
  ral_link_param_t           link_param;
} rrc_ral_measurement_report_ind_t;


typedef struct rrc_ral_connection_release_req_s {
  ral_transaction_id_t transaction_id;
  uint16_t             ue_id;
} rrc_ral_connection_release_req_t;

typedef struct rrc_ral_connection_release_conf_s {
  ral_transaction_id_t transaction_id;
  uint16_t             ue_id;
} rrc_ral_connection_release_conf_t;


typedef struct rrc_ral_connection_release_ind_s {
  uint16_t     ue_id;
} rrc_ral_connection_release_ind_t;




#endif /* RAL_MESSAGES_TYPES_H_ */
