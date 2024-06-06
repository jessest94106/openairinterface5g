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

/*! \file nas_config.h
 * \brief Configures the nasmesh interface
 * \author Daniel Camara and navid nikaein
 * \date 2006-2011
 * \version 0.1
 * \company Eurecom
 */
#ifndef NAS_CONFIG_H_
#define NAS_CONFIG_H_

#include <netinet/in.h>

/*! \fn void void nas_getparams(void)(void)
 * \brief This function get parameters used to configure network interface when running in noS1 mode
 * \note
 * @ingroup  _nas
 */
void nas_getparams(void);

/*! \fn int  nas_config(char*, int, int)
 * \brief This function initializes the nasmesh interface using the basic values,
 * basic address, network mask and broadcast address, as the default configured
 * ones
 * \param[in] interfaceName, the name of the interface, e.g. nasmesh0 or nasmesh1
 * \param[in] third octet of the ip address e.g. for the 10.1.2.3 address would be 2
 * \param[in] fourth octet of the ip address e.g. for the 10.1.2.3 address would be 3
 * \return 0 on success, otherwise 1, if couldn't open a socket and 2 if the ioctl fails
 * \note
 * @ingroup  _nas
 */
int nas_config(int interface_id, int thirdOctet, int fourthOctet, char *ifsuffix);

/*! \fn int  nas_config_mbms(char*, int, int)
 * \brief This function initializes the nasmesh interface using the basic values,
 * basic address, network mask and broadcast address, as the default configured
 * ones
 * \param[in] interfaceName, the name of the interface, e.g. nasmesh0 or nasmesh1
 * \param[in] third octet of the ip address e.g. for the 10.1.2.3 address would be 2
 * \param[in] fourth octet of the ip address e.g. for the 10.1.2.3 address would be 3
 * \return 0 on success, otherwise 1, if couldn't open a socket and 2 if the ioctl fails
 * \note
 * @ingroup  _nas
 */
int nas_config_mbms(int interface_id, int thirdOctet, int fourthOctet, char *ifsuffix);

/*! \fn int  nas_config_mbms_s1(char*, int, int)
 * \brief This function initializes the nasmesh interface using the basic values,
 * basic address, network mask and broadcast address, as the default configured
 * ones
 * \param[in] interfaceName, the name of the interface, e.g. nasmesh0 or nasmesh1
 * \param[in] third octet of the ip address e.g. for the 10.1.2.3 address would be 2
 * \param[in] fourth octet of the ip address e.g. for the 10.1.2.3 address would be 3
 * \return 0 on success, otherwise 1, if couldn't open a socket and 2 if the ioctl fails
 * \note
 * @ingroup  _nas
 */
int nas_config_mbms_s1(int interface_id, int thirdOctet, int fourthOctet, char *ifsuffix);

/*! \fn void  setBaseNetAddress(char*)
 * \brief This function sets the basic network address used
 * \param[in] baseAddr, the new basic address e.g.for 10.0.1.2, would be 10.0
 * \note
 * @ingroup  _nas
 */
void setBaseNetAddress(char *baseAddr);

/*! \fn void  setNetMask(char*)
 * \brief This function sets the new default network mask used
 * \param[in] newNetworkMask, the new network mask e.g.for 255.255.0.0
 * \note
 * @ingroup  _nas
 */
void setNetMask(char *baseAddr);

#endif /*NAS_CONFIG_H_*/
