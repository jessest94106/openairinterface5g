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

/*! \file nas_config.c
* \brief Configures the nasmesh interface
* \author Daniel Camara and Navid Nikaein
* \date 2006-2011
* \version 0.1
* \email:navid.nikaein@eurecom.fr
* \company Eurecom
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>

#include "nas_config.h"
#include "common/utils/LOG/log.h"
#include "executables/lte-softmodem.h"
#include "common/config/config_userapi.h"
#include "pdcp.h"

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

int nas_config_mbms(int interface_id, int thirdOctet, int fourthOctet, const char *ifname)
{
  //char buf[5];
  char ipAddress[20];
  char broadcastAddress[20];
  char interfaceName[20];
  int returnValue;
  //if(strcmp(ifname,"ue") == 0)
       //sprintf(ipAddress, "%s.%d.%d", "20.0",thirdOctet,fourthOctet);
  ////else
       sprintf(ipAddress, "%s.%d.%d",baseNetAddress,thirdOctet,fourthOctet);

  sprintf(broadcastAddress, "%s.%d.255",baseNetAddress, thirdOctet);
  sprintf(interfaceName, "%s%d", ifname, interface_id);
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
    bringInterfaceUp(interfaceName, 1);

  if(!returnValue)
    LOG_I(OIP,"Interface %s successfully configured, ip address %s, mask %s broadcast address %s\n",
          interfaceName, ipAddress, netMask, broadcastAddress);
  else
    LOG_E(OIP,"Interface %s couldn't be configured (ip address %s, mask %s broadcast address %s)\n",
          interfaceName, ipAddress, netMask, broadcastAddress);

  return returnValue;
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

  int res;
  char command_line[500];
  res = sprintf(command_line,
    "ip rule add from %s/32 table %d && "
    "ip rule add to %s/32 table %d && "
    "ip route add default dev %s%d table %d",
    ipAddress, interface_id - 1 + 10000,
    ipAddress, interface_id - 1 + 10000,
    UE_NAS_USE_TUN ? "oaitun_ue" : "oip",
    interface_id, interface_id - 1 + 10000);

  if (res < 0) {
    LOG_E(OIP,"Could not create ip rule/route commands string\n");
    return res;
  }

  background_system(command_line);

  return returnValue;
}

#ifdef STANDALONE

// program help
static void helpOptions(char **argv)
{
  printf("Help for %s\n",  argv[0]);
  printf("  -i <interfaceName>\n");
  printf("  -a <IP address>\n");
  printf("  -n <Net mask>\n");
  printf("  -b <broadcast address>\n");
  printf("  -h Shows this help\n");
  printf("If no option is passed as parameter the default values are: \n");
  printf("    Interface Name: nasmesh0\n");
  printf("    IP Address: 10.0.1.1\n");
  printf("    Net mask: 255.255.255.0\n");
  printf("    Broadcast address: [Beginning of the IP address].255\n");
  exit(1);
}

// creates the broadcast address if it wasn't set before
static void createBroadcast(char *broadcastAddress)
{
  int pos=strlen(broadcastAddress)-1;

  while(broadcastAddress[pos]!='.')
    pos--;

  broadcastAddress[++pos]='2';
  broadcastAddress[++pos]='2';
  broadcastAddress[++pos]='5';
  broadcastAddress[++pos]='\0';
}

// main function
//---------------------------------------------------------------------------
int main(int argc,char **argv)
//---------------------------------------------------------------------------
{
  int c;
  char interfaceName[100];
  char ipAddress[100];
  char networkMask[100];
  char broadcastAddress[100];
  strcpy(interfaceName, "oai0");
  strcpy(ipAddress, "10.0.1.1");
  strcpy(networkMask, "255.255.255.0");
  broadcastAddress[0]='\0';

  while ((c = getopt (argc, argv, "i:a:n:b:h")) != -1)
    switch (c) {
      case 'h':
        helpOptions(argv);
        break;

      case 'i':
        strcpy(interfaceName,optarg);
        break;

      case 'a':
        strcpy(ipAddress,optarg);
        break;

      case 'n':
        strcpy(networkMask,optarg);
        break;

      case 'b':
        strcpy(broadcastAddress,optarg);
        break;

      case '?':
        if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr,
                   "Unknown option character `\\x%x'.\n",
                   optopt);

        return 1;

      default:
        abort ();
    }

  if(strlen(broadcastAddress)==0) {
    strcpy(broadcastAddress,ipAddress);
    createBroadcast(broadcastAddress);
  }

  printf("Command: ifconfig %s %s networkMask %s broadcast %s\n", interfaceName, ipAddress, networkMask, broadcastAddress);

  //test
  //     setBaseNetAddress("11.11");
  //     nas_config(interfaceName, 33, 44);
}

#endif
