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

#ifndef NRPPA_MESSAGES_TYPES_H_
#define NRPPA_MESSAGES_TYPES_H_

// Defines to access message fields.

#define NRPPA_TRP_INFORMATION_REQ(mSGpTR) (mSGpTR)->ittiMsg.nrppa_trp_information_req
#define NRPPA_TRP_INFORMATION_RESP(mSGpTR) (mSGpTR)->ittiMsg.nrppa_trp_information_resp
#define NRPPA_POSITIONING_INFORMATION_REQ(mSGpTR) (mSGpTR)->ittiMsg.nrppa_positioning_information_req

/* Structure of Positioning related NRPPA messages */
/* IE structures for Positioning related messages as per TS 38.455 V16.7.1*/

typedef enum nrppa_trp_information_type_item_e {
  NRPPA_TRP_INFORMATION_TYPE_ITEM_NR_PCI,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_NG_RAN_CGI,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_NR_ARFCN,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_PRS_CONFIG,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_SSB_CONFIG,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_SFN_INIT_TIME,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_SPATIAL_DIRECTION_INFO,
  NRPPA_TRP_INFORMATION_TYPE_ITEM_GEO_COORDINATES
} nrppa_trp_information_type_item_pr;

typedef struct nrppa_trp_information_type_list_s {
  nrppa_trp_information_type_item_pr *trp_information_type_item;
  uint8_t trp_information_type_list_length;
} nrppa_trp_information_type_list_t;

typedef struct nrppa_trp_list_item_s {
  uint32_t trp_id;
} nrppa_trp_list_item_t;

typedef struct nrppa_trp_list_s {
  nrppa_trp_list_item_t *trp_list_item;
  uint32_t trp_list_length;
} nrppa_trp_list_t;

typedef struct nrppa_access_point_position_s {
  long latitude_sign;
  long latitude;
  long longitude;
  long direction_of_altitude;
  long altitude;
  long uncertainty_semi_major;
  long uncertainty_semi_minor;
  long orientation_of_major_axis;
  long uncertainty_altitude;
  long confidence;
} nrppa_access_point_position_t;

typedef struct nrppa_ngran_high_accuracy_access_point_position_s {
  long latitude;
  long longitude;
  long altitude;
  long uncertainty_semi_major;
  long uncertainty_semi_minor;
  long orientation_of_major_axis;
  long horizontal_confidence;
  long uncertainty_altitude;
  long vertical_confidence;
} nrppa_ngran_high_accuracy_access_point_position_t;

typedef union nrppa_trp_position_direct_accuracy_c {
  nrppa_access_point_position_t trp_position;
  nrppa_ngran_high_accuracy_access_point_position_t trp_HAposition;
} nrppa_trp_position_direct_accuracy_u;

typedef enum nrppa_trp_position_direct_accuracy_e {
  NRPPA_TRP_POSITION_DIRECT_ACCURACY_PR_NOTHING,
  NRPPA_TRP_POSITION_DIRECT_ACCURACY_PR_TRPPOSITION,
  NRPPA_TRP_POSITION_DIRECT_ACCURACY_PR_TRPHAPOSITION
} nrppa_trp_position_direct_accuracy_pr;

typedef struct nrppa_trp_position_direct_accuracy_s {
  nrppa_trp_position_direct_accuracy_pr present;
  nrppa_trp_position_direct_accuracy_u choice;
} nrppa_trp_position_direct_accuracy_t;

typedef struct nrppa_trp_position_direct_s {
  nrppa_trp_position_direct_accuracy_t accuracy;
} nrppa_trp_position_direct_t;

typedef enum nrppa_reference_point_e {
  NRPPA_REFERENCE_POINT_PR_NOTHING,
  NRPPA_REFERENCE_POINT_PR_COORDINATEID,
  NRPPA_REFERENCE_POINT_PR_REFERENCEPOINTCOORDINATE,
  NRPPA_REFERENCE_POINT_PR_REFERENCEPOINTCOORDINATEHA
} nrppa_reference_point_pr;

typedef union nrppa_reference_point_c {
  long coordinate_id;
  nrppa_access_point_position_t reference_point_coordinate;
  nrppa_ngran_high_accuracy_access_point_position_t reference_point_coordinateHA;
} nrppa_reference_point_u;

typedef struct nrppa_reference_point_s {
  nrppa_reference_point_pr present;
  nrppa_reference_point_u choice;
} nrppa_reference_point_t;

typedef struct nrppa_location_uncertainty_s {
  long horizontal_uncertainty;
  long horizontal_confidence;
  long vertical_uncertainty;
  long vertical_confidence;
} nrppa_location_uncertainty_t;

typedef struct nrppa_relative_geodetic_location_s {
  long milli_arc_second_units;
  long height_units;
  long delta_latitude;
  long delta_longitude;
  long delta_height;
  nrppa_location_uncertainty_t location_uncertainty;
} nrppa_relative_geodetic_location_t;

typedef struct nrppa_relative_cartesian_location_s {
  long xyz_unit;
  long xvalue;
  long yvalue;
  long zvalue;
  nrppa_location_uncertainty_t location_uncertainty;
} nrppa_relative_cartesian_location_t;

typedef union nrppa_trp_reference_point_type_c {
  nrppa_relative_geodetic_location_t trp_position_relative_geodetic;
  nrppa_relative_cartesian_location_t trp_position_relative_cartesian;
} nrppa_trp_reference_point_type_u;

typedef enum nrppa_trp_reference_point_type_e {
  NRPPA_TRP_REFERENCE_POINT_TYPE_PR_NOTHING,
  NRPPA_TRP_REFERENCE_POINT_TYPE_PR_TRPPOSITION_RELATIVE_GEODETIC,
  NRPPA_TRP_REFERENCE_POINT_TYPE_PR_TRPPOSITION_RELATIVE_CARTESIAN
} nrppa_trp_reference_point_type_pr;

typedef struct nrppa_trp_reference_point_type_t {
  nrppa_trp_reference_point_type_pr present;
  nrppa_trp_reference_point_type_u choice;
} nrppa_trp_reference_point_type_t;

typedef struct nrppa_trp_position_referenced_t {
  nrppa_reference_point_t reference_point;
  nrppa_trp_reference_point_type_t reference_point_type;
} nrppa_trp_position_referenced_t;

typedef union nrppa_trp_position_definition_type_c {
  nrppa_trp_position_direct_t direct;
  nrppa_trp_position_referenced_t referenced;
} nrppa_trp_position_definition_type_u;

typedef enum nrppa_trp_position_definition_type_e {
  NRPPA_TRP_POSITION_DEFINITION_TYPE_PR_NOTHING,
  NRPPA_TRP_POSITION_DEFINITION_TYPE_PR_DIRECT,
  NRPPA_TRP_POSITION_DEFINITION_TYPE_PR_REFERENCED
} nrppa_trp_position_definition_type_pr;

typedef struct nrppa_trp_position_definition_type_s {
  nrppa_trp_position_definition_type_u choice;
  nrppa_trp_position_definition_type_pr present;
} nrppa_trp_position_definition_type_t;

typedef struct nrppa_geographical_coordinates_s {
  nrppa_trp_position_definition_type_t trp_position_definition_type;
} nrppa_geographical_coordinates_t;

typedef struct nrppa_ng_ran_cgi_s {
  plmn_id_t plmn;
  uint64_t nr_cellid;
} nrppa_ng_ran_cgi_t;

typedef union nrppa_trp_information_type_response_item_c {
  uint16_t pci_nr;
  nrppa_ng_ran_cgi_t ng_ran_cgi;
  uint32_t nr_arfcn;
  nrppa_geographical_coordinates_t geographical_coordinates;
} nrppa_trp_information_type_response_item_u;

typedef enum nrppa_trp_information_type_response_item_e {
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NOTHING,
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_PCI_NR,
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NG_RAN_CGI,
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NRARFCN,
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_PRSCONFIGURATION,
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SSBINFORMATION,
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SFNINITIALISATIONTIME,
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SPATIALDIRECTIONINFORMATION,
  NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_GEOGRAPHICALCOORDINATES
} nrppa_trp_information_type_response_item_pr;

typedef struct nrppa_trp_information_type_response_item_s {
  nrppa_trp_information_type_response_item_pr present;
  nrppa_trp_information_type_response_item_u choice;
} nrppa_trp_information_type_response_item_t;

typedef struct nrppa_trp_information_type_response_list_s {
  nrppa_trp_information_type_response_item_t *trp_information_type_response_item;
  uint8_t trp_information_type_response_item_length;
} nrppa_trp_information_type_response_list_t;

typedef struct nrppa_trp_information_s {
  uint32_t trp_id;
  nrppa_trp_information_type_response_list_t trp_information_type_response_list;
} nrppa_trp_information_t;

// IE 9.3.1.176 (TS 38.473 V16.21.0)
typedef struct nrppa_trp_information_list_s {
  nrppa_trp_information_t *trp_information_item;
  uint32_t trp_information_item_length;
} nrppa_trp_information_list_t;

typedef struct nrppa_trp_information_req_s {
  // IE 9.2.4 (mandatory)
  uint16_t transaction_id;
  bool has_trp_list;
  // mandatory
  nrppa_trp_list_t trp_list;
  // mandatory
  nrppa_trp_information_type_list_t trp_information_type_list;
} nrppa_trp_information_req_t;

typedef struct nrppa_trp_information_resp_s {
  // IE 9.2.4 (mandatory)
  uint16_t transaction_id;
  // mandatory
  nrppa_trp_information_list_t trp_information_list;
} nrppa_trp_information_resp_t;

typedef struct nrppa_positioning_information_req_s {
  // IE 9.2.4 (mandatory)
  uint16_t transaction_id;
} nrppa_positioning_information_req_t;

#endif // NRPPA_MESSAGES_TYPES_H_
