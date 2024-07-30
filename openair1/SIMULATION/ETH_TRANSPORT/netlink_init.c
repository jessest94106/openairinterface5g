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

#include <linux/netlink.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include "common/platform_constants.h"
#include "common/utils/LOG/log.h"

int nas_sock_fd[MAX_MOBILES_PER_ENB * 2]; // Allocated for both LTE UE and NR UE.
int nas_sock_mbms_fd;

static int tun_alloc(char *dev)
{
  struct ifreq ifr;
  int fd, err;

  if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
    LOG_E(UTIL, "failed to open /dev/net/tun\n");
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));
  /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
   *        IFF_TAP   - TAP device
   *
   *        IFF_NO_PI - Do not provide packet information
   */
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

  if (*dev)
    strncpy(ifr.ifr_name, dev, sizeof(ifr.ifr_name) - 1);

  if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
    close(fd);
    return err;
  }

  strcpy(dev, ifr.ifr_name);
  return fd;
}

int netlink_init_mbms_tun(char *ifprefix, int id)
{
  int ret;
  char ifname[64];

  sprintf(ifname, "%s%d", ifprefix, id);
  nas_sock_mbms_fd = tun_alloc(ifname);

  if (nas_sock_mbms_fd == -1) {
    LOG_E(UTIL, "Error opening mbms socket %s (%d:%s)\n", ifname, errno, strerror(errno));
    exit(1);
  }

  LOG_D(UTIL, "Opened socket %s with fd %d\n", ifname, nas_sock_mbms_fd);
  ret = fcntl(nas_sock_mbms_fd, F_SETFL, O_NONBLOCK);

  if (ret == -1) {
    LOG_E(UTIL, "Error fcntl (%d:%s)\n", errno, strerror(errno));
    return 0;
  }

  struct sockaddr_nl nas_src_addr = {0};
  nas_src_addr.nl_family = AF_NETLINK;
  nas_src_addr.nl_pid = 1;
  nas_src_addr.nl_groups = 0; /* not in mcast groups */
  ret = bind(nas_sock_mbms_fd, (struct sockaddr *)&nas_src_addr, sizeof(nas_src_addr));

  return 1;
}

int netlink_init_tun(const char *ifprefix, int num_if, int id)
{
  int ret;
  char ifname[64];

  int begx = (id == 0) ? 0 : id - 1;
  int endx = (id == 0) ? num_if : id;
  for (int i = begx; i < endx; i++) {
    sprintf(ifname, "%s%d", ifprefix, i + 1);
    nas_sock_fd[i] = tun_alloc(ifname);

    if (nas_sock_fd[i] == -1) {
      LOG_E(UTIL, "Error opening socket %s (%d:%s)\n", ifname, errno, strerror(errno));
      return 0;
    }

    LOG_I(UTIL, "Opened socket %s with fd nas_sock_fd[%d]=%d\n", ifname, i, nas_sock_fd[i]);
    ret = fcntl(nas_sock_fd[i], F_SETFL, O_NONBLOCK);

    if (ret == -1) {
      LOG_E(UTIL, "Error fcntl (%d:%s)\n", errno, strerror(errno));

      return 0;
    }
  }

  return 1;
}
