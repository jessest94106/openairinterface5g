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
#include <errno.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "nas_config.h"
#include "common/utils/LOG/log.h"
#include "common/utils/system.h"

/*
 * \brief set a genneric interface parameter
 * \param ifn the name of the interface to modify
 * \param if_addr the address that needs to be modified
 * \param operation one of SIOCSIFADDR (set interface address), SIOCSIFNETMASK
 *   (set network mask), SIOCSIFBRDADDR (set broadcast address), SIOCSIFFLAGS
 *   (set flags)
 * \return true on success, false otherwise
 */
static bool setInterfaceParameter(int sock_fd, const char *ifn, const char *if_addr, int operation)
{
  struct ifreq ifr = {0};
  strncpy(ifr.ifr_name, ifn, sizeof(ifr.ifr_name));

  struct sockaddr_in addr = {.sin_family = AF_INET};
  inet_aton(if_addr, &addr.sin_addr);
  //inet_pton(AF_INET6 or AF_INET)
  memcpy(&ifr.ifr_ifru.ifru_addr,&addr,sizeof(struct sockaddr_in));

  bool success = ioctl(sock_fd,operation,&ifr) == 0;
  if (!success)
    LOG_E(OIP, "Setting operation %d for %s: ioctl call failed: %d, %s\n", operation, ifn, errno, strerror(errno));
  return success;
}

/*
 * \brief bring interface up (up != 0) or down (up == 0)
 */
typedef enum { INTERFACE_DOWN, INTERFACE_UP } if_action_t;
static bool change_interface_state(int sock_fd, const char *ifn, if_action_t if_action)
{
  struct ifreq ifr = {0};
  strncpy(ifr.ifr_name, ifn, sizeof(ifr.ifr_name));

  if (if_action == INTERFACE_UP) {
    ifr.ifr_flags |= IFF_UP | IFF_NOARP | IFF_MULTICAST;
  } else {
    ifr.ifr_flags &= (~IFF_UP);
  }

  bool success = ioctl(sock_fd, SIOCSIFFLAGS, (caddr_t)&ifr) == 0;
  if (!success) {
    const char* action = if_action == INTERFACE_DOWN ? "DOWN" : "UP";
    LOG_E(OIP, "Bringing interface %s for %s: ioctl call failed: %d, %s\n", action, ifn, errno, strerror(errno));
  }
  return success;
}

// non blocking full configuration of the interface (address, and the two lest octets of the address)
bool nas_config(int interface_id, const char *ip, const char *ifpref)
{
  char interfaceName[IFNAMSIZ];
  snprintf(interfaceName, sizeof(interfaceName), "%s%d", ifpref, interface_id);

  int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    LOG_E(UTIL, "Failed creating socket for interface management: %d, %s\n", errno, strerror(errno));
    return false;
  }

  change_interface_state(sock_fd, interfaceName, INTERFACE_DOWN);
  bool success = setInterfaceParameter(sock_fd, interfaceName, ip, SIOCSIFADDR);
  // set the machine network mask
  if (success)
    success = setInterfaceParameter(sock_fd, interfaceName, "255.255.255.0", SIOCSIFNETMASK);

  if (success)
    success = change_interface_state(sock_fd, interfaceName, INTERFACE_UP);

  if (success)
    LOG_I(OIP, "Interface %s successfully configured, ip address %s\n", interfaceName, ip);
  else
    LOG_E(OIP, "Interface %s couldn't be configured (ip address %s)\n", interfaceName, ip);

  close(sock_fd);
  return success;
}

void setup_ue_ipv4_route(int interface_id, const char *ipv4, const char *ifpref)
{
  int table_id = interface_id - 1 + 10000;
  char interfaceName[IFNAMSIZ];
  snprintf(interfaceName, sizeof(interfaceName), "%s%d", ifpref, interface_id);

  char command_line[500];
  int res = sprintf(command_line,
                    "ip rule add from %s/32 table %d && "
                    "ip rule add to %s/32 table %d && "
                    "ip route add default dev %s table %d",
                    ipv4,
                    table_id,
                    ipv4,
                    table_id,
                    interfaceName,
                    table_id);

  if (res < 0) {
    LOG_E(UTIL, "Could not create ip rule/route commands string\n");
    return;
  }
  background_system(command_line);
}

