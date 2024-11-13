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

#include "NR_NAS_defs.h"
#include "nas_log.h"
#include "TLVEncoder.h"
#include "fgs_nas_utils.h"

int _nas_mm_msg_encode_header(const fgmm_msg_header_t *header, uint8_t *buffer, uint32_t len)
{
  int size = 0;

  /* Check the buffer length */
  if (len < sizeof(fgmm_msg_header_t)) {
    return (TLV_ENCODE_BUFFER_TOO_SHORT);
  }

  /* Check the protocol discriminator */
  if (header->ex_protocol_discriminator != FGS_MOBILITY_MANAGEMENT_MESSAGE) {
    LOG_TRACE(ERROR, "Unexpected extened protocol discriminator: 0x%x", header->ex_protocol_discriminator);
    return (TLV_ENCODE_PROTOCOL_NOT_SUPPORTED);
  }

  /* Encode the extendedprotocol discriminator */
  ENCODE_U8(buffer + size, header->ex_protocol_discriminator, size);
  /* Encode the security header type */
  ENCODE_U8(buffer + size, (header->security_header_type & 0xf), size);
  /* Encode the message type */
  ENCODE_U8(buffer + size, header->message_type, size);
  return (size);
}

int mm_msg_encode(const fgmm_nas_message_plain_t *p, uint8_t *buffer, uint32_t len)
{
  LOG_FUNC_IN;
  int header_result;
  int encode_result;
  uint8_t msg_type = p->header.message_type;

  /* First encode the 5GMM message header */
  header_result = _nas_mm_msg_encode_header(&p->header, buffer, len);

  if (header_result < 0) {
    LOG_TRACE(ERROR,
              "Failed to encode 5GMM message header"
              "(%d)",
              header_result);
    LOG_FUNC_RETURN(header_result);
  }

  buffer += header_result;
  len -= header_result;

  switch (msg_type) {
    case FGS_REGISTRATION_REQUEST:
      encode_result = encode_registration_request(&p->mm_msg.registration_request, buffer, len);
      break;
    case FGS_IDENTITY_RESPONSE:
      encode_result = encode_fgmm_identity_response(buffer, &p->mm_msg.fgs_identity_response, len);
      break;
    case FGS_AUTHENTICATION_RESPONSE:
      encode_result = encode_fgs_authentication_response(&p->mm_msg.fgs_auth_response, buffer, len);
      break;
    case FGS_SECURITY_MODE_COMPLETE:
      encode_result = encode_fgs_security_mode_complete(&p->mm_msg.fgs_security_mode_complete, buffer, len);
      break;
    case FGS_UPLINK_NAS_TRANSPORT:
      encode_result = encode_fgs_uplink_nas_transport(&p->mm_msg.uplink_nas_transport, buffer, len);
      break;
    case FGS_DEREGISTRATION_REQUEST_UE_ORIGINATING:
      encode_result =
          encode_fgs_deregistration_request_ue_originating(&p->mm_msg.fgs_deregistration_request_ue_originating, buffer, len);
      break;
    case FGS_SERVICE_REQUEST:
      encode_result = encode_fgs_service_request(buffer, &p->mm_msg.service_request, len);
      break;
    default:
      LOG_TRACE(ERROR, " Unexpected message type: 0x%x", p->header.message_type);
      encode_result = TLV_ENCODE_WRONG_MESSAGE_TYPE;
      break;
      /* TODO: Handle not standard layer 3 messages: SERVICE_REQUEST */
  }

  if (encode_result < 0) {
    LOG_TRACE(ERROR,
              " Failed to encode L3 EMM message 0x%x "
              "(%d)",
              p->header.message_type,
              encode_result);
  }

  if (encode_result < 0)
    LOG_FUNC_RETURN(encode_result);

  LOG_FUNC_RETURN(header_result + encode_result);
}

int nas_protected_security_header_encode(uint8_t *buffer, const fgs_nas_message_security_header_t *header, int length)
{
  LOG_FUNC_IN;

  int size = 0;

  /* Encode the protocol discriminator) */
  ENCODE_U8(buffer, header->protocol_discriminator, size);

  /* Encode the security header type */
  ENCODE_U8(buffer + size, (header->security_header_type & 0xf), size);

  /* Encode the message authentication code */
  ENCODE_U32(buffer + size, header->message_authentication_code, size);
  /* Encode the sequence number */
  ENCODE_U8(buffer + size, header->sequence_number, size);

  LOG_FUNC_RETURN(size);
}
