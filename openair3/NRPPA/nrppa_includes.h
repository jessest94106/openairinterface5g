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

#ifndef NRPPA_INCLUDE_H_
#define NRPPA_INCLUDE_H_

#include "NRPPA_NRPPA-PDU.h"
#include "NRPPA_InitiatingMessage.h"
#include "NRPPA_SuccessfulOutcome.h"
#include "NRPPA_UnsuccessfulOutcome.h"
#include "NRPPA_ProtocolIE-ID.h"
#include "NRPPA_ProtocolIE-Field.h"
#include "NRPPA_ProtocolIE-Container.h"
#include "NRPPA_ProtocolExtensionField.h"
#include "NRPPA_ProtocolIE-ContainerList.h"
#include "NRPPA_ProtocolExtensionContainer.h"
#include "NRPPA_ProtocolIE-Single-Container.h"
#include "NRPPA_asn_constant.h"
#include "NRPPA_TRPInformationRequest.h"
#include "NRPPA_TRPInformationItem.h"
#include "NRPPA_TRPPositionDirect.h"
#include "NRPPA_NG-RANAccessPointPosition.h"
#include "NRPPA_NGRANHighAccuracyAccessPointPosition.h"
#include "NRPPA_TRPPositionReferenced.h"
#include "NRPPA_RelativeGeodeticLocation.h"
#include "NRPPA_RelativeCartesianLocation.h"
#include "NRPPA_SRSCarrier-List-Item.h"
#include "NRPPA_SCS-SpecificCarrier.h"
#include "NRPPA_SRSConfig.h"
#include "NRPPA_SRSResource-List.h"
#include "NRPPA_SRSResource.h"
#include "NRPPA_ResourceType.h"
#include "NRPPA_ResourceTypePeriodic.h"
#include "NRPPA_ResourceTypeSemi-persistent.h"
#include "NRPPA_ResourceTypeAperiodic.h"
#include "NRPPA_PosSRSResource-List.h"
#include "NRPPA_ResourceTypePos.h"
#include "NRPPA_ResourceTypePeriodicPos.h"
#include "NRPPA_ResourceTypeSemi-persistentPos.h"
#include "NRPPA_ResourceTypeAperiodicPos.h"
#include "NRPPA_SRSResourceSet-List.h"
#include "NRPPA_SRSResourceSet.h"
#include "NRPPA_PosSRSResource-List.h"
#include "NRPPA_PosSRSResourceSet-Item.h"
#include "NRPPA_PosSRSResource-List.h"
#include "NRPPA_PosSRSResource-Item.h"
#include "NRPPA_ResourceSetTypePeriodic.h"
#include "NRPPA_ResourceSetTypeSemi-persistent.h"
#include "NRPPA_ResourceSetTypeAperiodic.h"
#include "NRPPA_PosSRSResourceSet-List.h"
#include "NRPPA_PosResourceSetTypePeriodic.h"
#include "NRPPA_PosResourceSetTypeSemi-persistent.h"
#include "NRPPA_PosResourceSetTypeAperiodic.h"
#include "NRPPA_SemipersistentSRS.h"
#include "NRPPA_AperiodicSRS.h"
#include "NRPPA_TRP-MeasurementRequestItem.h"
#include "NRPPA_TRPMeasurementQuantities-Item.h"
#include "NRPPA_TRPMeasurementQuantitiesList-Item.h"
#include "NRPPA_TRP-MeasurementResponseList.h"
#include "NRPPA_TRP-MeasurementResponseItem.h"
#include "NRPPA_TrpMeasurementResultItem.h"
#include "NRPPA_UL-RTOAMeasurement.h"
#include "NRPPA_GNB-RxTxTimeDiff.h"

#endif /* NRPPA_INCLUDE_H_ */
