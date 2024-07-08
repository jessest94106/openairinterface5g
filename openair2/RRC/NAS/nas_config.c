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

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "nas_config.h"
#include "common/utils/LOG/log.h"
#include "common/config/config_userapi.h"

//default values according to the examples,

char *baseNetAddress ;
char *netMask ;
#define NASHLP_NETPREFIX "<NAS network prefix, two first bytes of network addresses>\n"
#define NASHLP_NETMASK   "<NAS network mask>\n"
void nas_getparams(void) {
  // this datamodel require this static because we partially keep data like baseNetAddress (malloc on a global)
  // but we loose the opther attributes in nasoptions between two calls if is is not static !
  // clang-format off
  paramdef_t nasoptions[] = {
    /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    /*                                            configuration parameters for netlink, includes network parameters when running in noS1 mode                             */
    /*   optname                     helpstr                paramflags           XXXptr                               defXXXval               type                 numelt */
    /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    {"NetworkPrefix",    NASHLP_NETPREFIX,       0,              .strptr=&baseNetAddress,        .defstrval="10.0",            TYPE_STRING,  0 },
    {"NetworkMask",      NASHLP_NETMASK,         0,              .strptr=&netMask,               .defstrval="255.255.255.0",   TYPE_STRING,  0 },
  };
  // clang-format on
  config_get(config_get_if(), nasoptions, sizeofArray(nasoptions), "nas.noS1");
}

void setBaseNetAddress (char *baseAddr) {
  strcpy(baseNetAddress,baseAddr);
}

void setNetMask (char *baseAddr) {
  strcpy(netMask,baseAddr);
}

// sets a genneric interface parameter
// (SIOCSIFADDR, SIOCSIFNETMASK, SIOCSIFBRDADDR, SIOCSIFFLAGS)
static int setInterfaceParameter(const char *interfaceName, const char *settingAddress, int operation)
{
  int sock_fd;
  struct ifreq ifr;
  struct sockaddr_in addr;

  if((sock_fd = socket(AF_INET,SOCK_DGRAM,0)) < 0)    {
    LOG_E(OIP,"Setting operation %d, for %s, address, %s : socket failed\n",
          operation, interfaceName, settingAddress);
    return 1;
  }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, interfaceName, sizeof(ifr.ifr_name)-1);
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  inet_aton(settingAddress,&addr.sin_addr);
  memcpy(&ifr.ifr_ifru.ifru_addr,&addr,sizeof(struct sockaddr_in));

  if(ioctl(sock_fd,operation,&ifr) < 0)    {
    close(sock_fd);
    LOG_E(OIP,"Setting operation %d, for %s, address, %s : ioctl call failed\n",
          operation, interfaceName, settingAddress);
    return 2;
  }

  close(sock_fd);
  return 0;
}

// sets a genneric interface parameter
// (SIOCSIFADDR, SIOCSIFNETMASK, SIOCSIFBRDADDR, SIOCSIFFLAGS)
static int bringInterfaceUp(char *interfaceName, int up)
{
  int sock_fd;
  struct ifreq ifr;

  if((sock_fd = socket(AF_INET,SOCK_DGRAM,0)) < 0) {
    LOG_E(OIP,"Bringing interface UP, for %s, failed creating socket\n", interfaceName);
    return 1;
  }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, interfaceName, sizeof(ifr.ifr_name)-1);

  if(up) {
    ifr.ifr_flags |= IFF_UP | IFF_NOARP | IFF_MULTICAST;

    if (ioctl(sock_fd, SIOCSIFFLAGS, (caddr_t)&ifr) == -1) {
      close(sock_fd);
      LOG_E(OIP,"Bringing interface UP, for %s, failed UP ioctl\n", interfaceName);
      return 2;
    }
  } else {
    //        printf("desactivation de %s\n", interfaceName);
    ifr.ifr_flags &= (~IFF_UP);

    if (ioctl(sock_fd, SIOCSIFFLAGS, (caddr_t)&ifr) == -1) {
      close(sock_fd);
      LOG_E(OIP,"Bringing interface down, for %s, failed UP ioctl\n", interfaceName);
      return 2;
    }
  }

  //   printf("UP/DOWN OK!\n");
  close( sock_fd );
  return 0;
}

// non blocking full configuration of the interface (address, and the two lest octets of the address)
int nas_config(int interface_id, int thirdOctet, int fourthOctet, const char *ifpref)
{
  //char buf[5];
  char ipAddress[20];
  char broadcastAddress[20];
  char interfaceName[20];
  int returnValue;
  sprintf(ipAddress, "%s.%d.%d", baseNetAddress,thirdOctet,fourthOctet);
  sprintf(broadcastAddress, "%s.%d.255",baseNetAddress, thirdOctet);
  sprintf(interfaceName, "%s%d", ifpref, interface_id);
  bringInterfaceUp(interfaceName, 0);
  // sets the machine address
  returnValue= setInterfaceParameter(interfaceName, ipAddress,SIOCSIFADDR);

  // sets the machine network mask
  if(!returnValue)
    returnValue= setInterfaceParameter(interfaceName, netMask,SIOCSIFNETMASK);

  // sets the machine broadcast address
  if(!returnValue)
    returnValue= setInterfaceParameter(interfaceName, broadcastAddress,SIOCSIFBRDADDR);

  if(!returnValue)
	  returnValue=bringInterfaceUp(interfaceName, 1);

  if(!returnValue)
    LOG_I(OIP,"Interface %s successfully configured, ip address %s, mask %s broadcast address %s\n",
          interfaceName, ipAddress, netMask, broadcastAddress);
  else
    LOG_E(OIP,"Interface %s couldn't be configured (ip address %s, mask %s broadcast address %s)\n",
          interfaceName, ipAddress, netMask, broadcastAddress);

  return returnValue;
}
