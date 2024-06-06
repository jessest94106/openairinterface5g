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

/*! \file rb_config.c
* \brief User-space utility for configuring NASMESH IOCTL interface
* \author Raymond Knopp and Navid Nikaein
* \date 2009
* \version 1.1
* \warning
* @ingroup driver

*/

#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "nas_config.h"
#include "common/utils/LOG/log.h"

#include "NETWORK_DRIVER/MESH/rrc_nas_primitives.h"
#include "NETWORK_DRIVER/MESH/ioctl.h"
#include "NETWORK_DRIVER/MESH/constant.h"


#define BUF_SIZE      800

// Global variables
//int sd_graal;
//int fd; // moved to the struct rb_config
//char  myIPAddr[16]; // local IP Address
//int meas_counter;
//struct ralu_priv ru_priv;
//struct ralu_priv *ralupriv;

//ioctl
char dummy_buffer[BUF_SIZE]; // moved to stats req
struct nas_ioctl gifr;
//int wait_start_nas;

static int fd;
static int socket_enabled;

static void init_socket(void)
{

  if ((fd=socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
    LOG_E(OIP,"ERROR opening socket\n");
    socket_enabled=0;
  }

  socket_enabled=1;
}

static void rb_ioctl_init(int inst)
{

  /* init the gifr struct for the given inst */
  sprintf(gifr.name, "oai%d",inst);
  memset ((void *)dummy_buffer,0,BUF_SIZE);
  gifr.msg= &(dummy_buffer[0]);
}


static int rb_validate_config_ipv4(int cx, int inst, int rb)
{

  if (inst == -1) {
    LOG_E(OIP,"Specify an interface\n");
    return(1);
  }

  if (rb == -1) {
    LOG_E(OIP,"Specify an RAB identifier \n");
    return (1);
  }

  if (cx == -1) {
    LOG_E(OIP,"Specify an LCR \n");
    return(1);
  }

  /*
  if ( inet_aton(src,&saddr_ipv4)<= 0) {
    LOG_E(OIP,"Specify a source IP address\n");
    return(1);
  }
   if (inet_aton(src,&saddr_ipv4)<= 0 0) {
    LOG_E(OIP,"Specify a destination IP address\n");
    return(1);
    }
  */
  return (0);

}

int rb_conf_ipv4(int action,int cx, int inst, int rb, int dscp, in_addr_t saddr_ipv4, in_addr_t daddr_ipv4)
{
  //int rb_conf_ipv6(int action,int cx, int inst, int qos, in6_addr src, in6_addr dst) {
  //int rb_conf(rb_config *rb_cfg) {

  int err;

  struct nas_msg_rb_establishment_request *msgreq;
  struct nas_msg_class_add_request *msgreq_class;

  struct nas_msg_rb_release_request *msgrel;
  struct nas_msg_class_del_request *msgdel_class;

  if (socket_enabled ==0) {
    init_socket();
  }

  rb_ioctl_init(inst);

  if (rb_validate_config_ipv4(cx,inst,rb) > 0) {
    LOG_E(OIP, "OIP configuration failed, inputs parameters incorrect\n");
    return (1);
  }

  // add an OIP
  if (action == 0 ) {

    msgreq = (struct nas_msg_rb_establishment_request *)(gifr.msg);
    msgreq->rab_id =  (nasRadioBearerId_t) rb;
    msgreq->lcr = (nasRadioBearerId_t) cx;
    msgreq->qos = 0;

    gifr.type =  NAS_MSG_RB_ESTABLISHMENT_REQUEST;

    if ((err=ioctl(fd, NAS_IOCTL_RRM, &gifr)) < 0) {
      perror("IOCTL error: OIP ESTAB REQ\n");
      LOG_E(OIP,"IOCTL error: OIP ESTAB REQ");
    }

    msgreq_class = (struct nas_msg_class_add_request *)(gifr.msg);
    msgreq_class->rab_id = (nasRadioBearerId_t) rb;
    msgreq_class->lcr = (nasRadioBearerId_t) cx;
    msgreq_class->version = 4;//ipv4

    msgreq_class->classref = 0 + (msgreq_class->lcr<<3);
    msgreq_class->dir = NAS_DIRECTION_SEND;
    msgreq_class->fct = NAS_FCT_QOS_SEND;
    msgreq_class->saddr.ipv4 = saddr_ipv4;
    msgreq_class->daddr.ipv4 = daddr_ipv4;

    // TO BE FIXED WHEN WE CAN SPECIFY A PROTOCOL-based rule
    msgreq_class->protocol = NAS_PROTOCOL_DEFAULT;
    msgreq_class->dscp=dscp;
    gifr.type =  NAS_MSG_CLASS_ADD_REQUEST;

    if ((err=ioctl(fd, NAS_IOCTL_RRM, &gifr)) < 0 ) {
      perror("IOCTL error: DIR SEND: ADD REQ");
      LOG_E(OIP,"IOCTL error: DIR SEND: ADD REQ\n");
    }

    msgreq_class->rab_id = (nasRadioBearerId_t)rb;
    msgreq_class->lcr = (nasRadioBearerId_t)cx;
    msgreq_class->classref = 1+(msgreq_class->lcr<<3);
    msgreq_class->dir = NAS_DIRECTION_RECEIVE;
    msgreq_class->daddr.ipv4 = saddr_ipv4;
    msgreq_class->saddr.ipv4 = daddr_ipv4;
    gifr.type =  NAS_MSG_CLASS_ADD_REQUEST;

    if ((err=ioctl(fd, NAS_IOCTL_RRM, &gifr)) < 0 ) {
      perror("IOCTL error: DIR RECEIVE: ADD REQ");
      LOG_E(OIP,"IOCTL error: DIR RECEIVE: ADD REQ\n");
    }

  }
  // OIP DEL
  else if (action == 1) {

    msgrel = (struct nas_msg_rb_release_request *)(gifr.msg);
    msgrel->rab_id = rb;
    msgrel->lcr = cx;

    gifr.type =  NAS_MSG_RB_RELEASE_REQUEST;

    if ((err=ioctl(fd, NAS_IOCTL_RRM, &gifr)) < 0) {
      perror("IOCTL error: OIP RELEASE REQ");
      LOG_E(OIP,"IOCTL error: OIP RELEASE REQ\n");
    }

    msgdel_class = (struct nas_msg_class_del_request *)(gifr.msg);
    msgdel_class->lcr = cx;
    msgdel_class->dir = NAS_DIRECTION_SEND;
    msgdel_class->classref = 0 + (msgdel_class->lcr<<3);
    msgdel_class->dscp=dscp;

    gifr.type =  NAS_MSG_CLASS_DEL_REQUEST;

    if ((err=ioctl(fd, NAS_IOCTL_RRM, &gifr)) < 0 ) {
      perror("IOCTL error: DIR SEND : CLASS DEL REQ");
      LOG_E(OIP,"IOCTL error: DIR SEND : CLASS DEL REQ\n");
    }

    msgdel_class->lcr = cx;
    msgdel_class->classref = 1+(msgdel_class->lcr<<3);
    msgdel_class->dir = NAS_DIRECTION_RECEIVE;

    gifr.type =  NAS_MSG_CLASS_DEL_REQUEST;

    if ((err=ioctl(fd, NAS_IOCTL_RRM, &gifr)) < 0 ) {
      perror("IOCTL error: DIR RECEIVE : CLASS DEL REQ");
      LOG_E(OIP,"IOCTL erro: DIR RECEIVE : CLASS DEL REQ\n");
    }

  }

  return(0);

}

in_addr_t ipv4_address (int thirdOctet, int fourthOctet)
{

  struct in_addr saddr_ipv4;
  char ipAddress[20];
  sprintf(ipAddress, "10.0.%d.%d",thirdOctet,fourthOctet);
  inet_aton(ipAddress,&saddr_ipv4);
  return saddr_ipv4.s_addr;

}
