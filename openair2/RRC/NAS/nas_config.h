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

#ifndef NAS_CONFIG_H_
#define NAS_CONFIG_H_

#include <stdbool.h>
#include <netinet/in.h>

/*! \fn int  nas_config(char*, int, int)
 * \brief This function initializes the nasmesh interface using the basic values,
 * basic address, network mask and broadcast address, as the default configured
 * ones
 * \param[in] interface_id number of this interface, prepended after interface
 * name
 * \param[in] ip IPv4 address of this interface as a string
 * \param[in] ifprefix interface name prefix to which an interface number will
 * be appended
 * \return true on success, otherwise false
 * \note
 * @ingroup  _nas
 */
bool nas_config(int interface_id, const char *ip, const char *ifprefix);

#endif /*NAS_CONFIG_H_*/
