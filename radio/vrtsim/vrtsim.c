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

#include "PHY/TOOLS/tools_defs.h"
#include "system.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <linux/limits.h>

#include <common/utils/assertions.h>
#include <common/utils/LOG/log.h>
#include <common/utils/load_module_shlib.h>
#include <common/utils/telnetsrv/telnetsrv.h>
#include <common/config/config_userapi.h>
#include <common/openairinterface5g_limits.h>
#include "common_lib.h"
#include "shm_td_iq_channel.h"
#include "SIMULATION/TOOLS/sim.h"
#include "actor.h"
#include "noise_device.h"
#include "simde/x86/avx512.h"
#include "taps_client.h"
#include "cirdb_provider.h"
#include "cirdb_yaml.h"

// Simulator role
typedef enum { ROLE_SERVER = 1, ROLE_CLIENT } role;

#define MAX_NUM_ANTENNAS_TX 4
#define SAVED_SAMPLES_LEN 256
#define MAX_NUM_UES MAX_MOBILES_PER_GNB

#define ROLE_CLIENT_STRING "client"
#define ROLE_SERVER_STRING "server"

#define VRTSIM_SECTION "vrtsim"
#define TIME_SCALE_HLP \
  "sample time scale. 1.0 means realtime. Values > 1 mean faster than realtime. Values < 1 mean slower than realtime\n"
#define TAPS_SOCKET_HLP "Socket to connect to the channel emulation server\n"
#define CLIENT_NUM_RX_HLP "Number of RX antennas of the client, specified on the server\n"
#define CONNECTION_DESCRIPTOR_HLP "Path to the file written by the server that the client can use to connect."
#define DEFAULT_CHANNEL_NAME "vrtsim_channel"
#define DEFAULT_DESCRIPTOR "/tmp/vrtsim_connection"

// clang-format off
#define VRTSIM_PARAMS_DESC \
  { \
     {"connection_descriptor",  CONNECTION_DESCRIPTOR_HLP,   0, .strptr = &vrtsim_state->connection_descriptor,  .defstrval = DEFAULT_DESCRIPTOR, TYPE_STRING, 0}, \
     {"role",                   "either client or server\n", 0, .strptr = &role,                                 .defstrval = ROLE_CLIENT_STRING, TYPE_STRING, 0}, \
     {"timescale",              TIME_SCALE_HLP,              0, .dblptr = &vrtsim_state->timescale,              .defdblval = 1.0,                TYPE_DOUBLE, 0}, \
     {"chanmod",                "Enable channel modelling",  0, .iptr = &vrtsim_state->chanmod,                  .defintval = 0,                  TYPE_INT,    0}, \
     {"taps-socket",            TAPS_SOCKET_HLP,             0, .strptr = &vrtsim_state->taps_socket,            .defstrval = NULL,               TYPE_STRING, 0}, \
     {"client-num-rx-antennas", CLIENT_NUM_RX_HLP,           0, .iptr = &vrtsim_state->client_num_rx_antennas,   .defintval = 1,                  TYPE_INT,    0}, \
     /* CIR DB enable and paths */ \
     {"cirdb",                  "Use CIR database for channel taps (1 yes, 0 no)", 0, .iptr = &vrtsim_state->use_cirdb,  .defintval = 0, TYPE_INT, 0}, \
     {"cirdb-path",             "Directory that holds vrtsim.yaml and cir_db.bin", 0, .strptr = &vrtsim_state->cirdb_path, .defstrval = NULL, TYPE_STRING, 0}, \
     {"cirdb_yaml",             "Absolute path to CIR DB YAML file (optional, overrides cirdb-path)", 0, .strptr = &vrtsim_state->cirdb_yaml, .defstrval = NULL, TYPE_STRING, 0}, \
     {"cirdb_file",             "Absolute path to CIR DB binary file (optional, overrides cirdb-path)", 0, .strptr = &vrtsim_state->cirdb_file, .defstrval = NULL, TYPE_STRING, 0}, \
     /* CIR DB selection knobs */ \
     {"cirdb_model_id",         "Preferred TDL model id 0..4", 0, .iptr  = &vrtsim_state->cirdb_model_id,  .defintval = 0,    TYPE_INT,    0}, \
     {"cirdb_ds_ns",            "Desired RMS delay spread in ns", 0, .dblptr = &vrtsim_state->cirdb_ds_ns, .defdblval = 10.0, TYPE_DOUBLE, 0}, \
     {"cirdb_speed_mps",        "Desired speed in m/s", 0, .dblptr = &vrtsim_state->cirdb_speed_mps, .defdblval = 1.5, TYPE_DOUBLE, 0}, \
     {"num_ues",                "Number of UE slots (server only)\n", 0, .iptr = &vrtsim_state->num_ues,        .defintval = 1,                  TYPE_INT,    0}, \
     {"ue_id",                  "UE slot index 0..num_ues-1 (client only)\n", 0, .iptr = &vrtsim_state->ue_id, .defintval = 0,                  TYPE_INT,    0}, \
  };
// clang-format on

typedef struct histogram_s {
  uint64_t diff[30];
  int num_samples;
  int min_samples;
  double range;
} histogram_t;

// Information about the peer
typedef struct peer_info_s {
  int num_rx_antennas;
} peer_info_t;

typedef struct tx_timing_s {
  uint64_t tx_samples_late;
  uint64_t tx_early;
  uint64_t tx_samples_total;
  double average_tx_budget;
  histogram_t tx_histogram;
} tx_timing_t;

typedef struct {
  int model_id;
  double ds_ns;
  double speed_mps;
} cirdb_conf_t;

typedef struct {
  int tx_ant;
  int rx_ant;
  int tx_offset;
  int rx_offset;
  cirdb_conf_t cir_conf;
} ue_conf_t;

typedef struct {
  int role;
  char *connection_descriptor;
  ShmTDIQChannel *channel;
  uint64_t last_received_sample;
  pthread_t timing_thread;
  bool run_timing_thread;
  double timescale;
  double sample_rate;
  uint64_t rx_samples_late;
  uint64_t rx_early;
  uint64_t rx_samples_total;
  tx_timing_t *tx_timing;
  peer_info_t peer_info;
  int chanmod;
  double rx_freq;
  double tx_bw;
  int tx_num_channels;
  int rx_num_channels;
  channel_desc_t *channel_desc;
  channel_desc_t *channel_desc_per_ue[MAX_NUM_UES];
  pthread_mutex_t cirdb_mutex;
  Actor_t *channel_modelling_actors;
  char *taps_socket;
  int client_num_rx_antennas;
  struct timespec start_ts;
  /* CIR DB state */
  int use_cirdb;
  char *cirdb_path;
  char *cirdb_yaml;
  char *cirdb_file;
  int cirdb_model_id;
  double cirdb_ds_ns;
  double cirdb_speed_mps;
  // Multi-UE support
  int num_ues;
  int ue_id;

  ue_conf_t ue_conf[MAX_NUM_UES];
  ue_conf_t ue;

  // Totals
  int total_ul_streams;
  int total_dl_streams;
  sample_t *ul_combine_buffer;
  size_t ul_combine_buffer_len;
} vrtsim_state_t;

static void histogram_add(histogram_t *histogram, double diff)
{
  histogram->num_samples++;
  if (histogram->num_samples >= histogram->min_samples) {
    int bin = min(sizeofArray(histogram->diff) - 1, max(0, (int)(diff / histogram->range * sizeofArray(histogram->diff))));
    histogram->diff[bin]++;
  }
}

static void histogram_print(histogram_t *histogram)
{
  LOG_I(HW, "VRTSIM: TX budget histogram: %d samples\n", histogram->num_samples);
  float bin_size = histogram->range / sizeofArray(histogram->diff);
  float bin_start = 0;
  for (int i = 0; i < sizeofArray(histogram->diff); i++) {
    LOG_I(HW, "Bin %d\t[%.1f - %.1fuS]:\t\t%lu\n", i, bin_start, bin_start + bin_size, histogram->diff[i]);
    bin_start += bin_size;
  }
}

static void histogram_merge(histogram_t *dest, histogram_t *src)
{
  for (int i = 0; i < sizeofArray(dest->diff); i++) {
    dest->diff[i] += src->diff[i];
  }
  dest->num_samples += src->num_samples;
}

static void load_channel_model(vrtsim_state_t *vrtsim_state)
{
  load_channellist(vrtsim_state->tx_num_channels,
                   vrtsim_state->peer_info.num_rx_antennas,
                   vrtsim_state->sample_rate,
                   vrtsim_state->rx_freq,
                   vrtsim_state->tx_bw);
  char *model_name = vrtsim_state->role == ROLE_CLIENT ? "client_tx_channel_model" : "server_tx_channel_model";
  vrtsim_state->channel_desc = find_channel_desc_fromname(model_name);
  AssertFatal(vrtsim_state->channel_desc != NULL,
              "Could not find model name %s. Make sure it is present in the config file\n",
              model_name);
  LOG_I(HW,
        "Channel model %s parameters: path_loss_dB=%.2f, nb_tx=%d, nb_rx=%d, channel_length=%d\n",
        model_name,
        vrtsim_state->channel_desc->path_loss_dB,
        vrtsim_state->channel_desc->nb_tx,
        vrtsim_state->channel_desc->nb_rx,
        vrtsim_state->channel_desc->channel_length);
  random_channel(vrtsim_state->channel_desc, 0);
  AssertFatal(vrtsim_state->channel_desc != NULL, "Could not find channel model %s\n", model_name);
}

static void vrtsim_readconfig(vrtsim_state_t *vrtsim_state)
{
  char *role = NULL;
  paramdef_t vrtsim_params[] = VRTSIM_PARAMS_DESC;
  int ret = config_get(config_get_if(), vrtsim_params, sizeofArray(vrtsim_params), VRTSIM_SECTION);
  AssertFatal(ret >= 0, "configuration couldn't be performed\n");
  if (strncmp(role, ROLE_CLIENT_STRING, strlen(ROLE_CLIENT_STRING)) == 0) {
    vrtsim_state->role = ROLE_CLIENT;
  } else if (strncmp(role, ROLE_SERVER_STRING, strlen(ROLE_SERVER_STRING)) == 0) {
    vrtsim_state->role = ROLE_SERVER;
  } else {
    AssertFatal(false, "Invalid role configuration\n");
  }
#ifdef OAI_VRTSIM_TAPS_CLIENT
  if (vrtsim_state->taps_socket) {
    LOG_A(HW, "VRTSIM: will use taps socket %s\n", vrtsim_state->taps_socket);
  }
#else
  if (vrtsim_state->taps_socket) {
    AssertFatal(false, "Invalid configuration: Build with OAI_VRTSIM_TAPS_CLIENT to use taps socket\n");
  }
#endif
  if (vrtsim_state->use_cirdb) {
    LOG_A(HW, "VRTSIM: CIR DB is enabled at runtime\n");
  }
}

static void *vrtsim_timing_job(void *arg)
{
  vrtsim_state_t *vrtsim_state = arg;
  if (clock_gettime(CLOCK_REALTIME, &vrtsim_state->start_ts)) {
    LOG_E(UTIL, "clock_gettime failed\n");
    exit(1);
  }
  int64_t last_sample_index = 0;
  while (vrtsim_state->run_timing_thread) {
    struct timespec current_time;
    if (clock_gettime(CLOCK_REALTIME, &current_time)) {
      LOG_E(UTIL, "clock_gettime failed\n");
      exit(1);
    }
    uint64_t diff = (current_time.tv_sec - vrtsim_state->start_ts.tv_sec) * 1000000000
                    + (current_time.tv_nsec - vrtsim_state->start_ts.tv_nsec);
    double sample_index = vrtsim_state->sample_rate * vrtsim_state->timescale * diff / 1e9;
    int64_t samples_to_produce = sample_index - last_sample_index;
    shm_td_iq_channel_produce_samples(vrtsim_state->channel, samples_to_produce);
    last_sample_index = sample_index;
    usleep(1);
  }
  return 0;
}

typedef struct client_info_s {
  int num_ues;
  int gnb_num_tx_ant;
  int gnb_num_rx_ant;
  ue_conf_t ues[MAX_NUM_UES];
} client_info_t;
/**
 * @brief Publishes the client information information to a file for the client to read.
 *
 * The server writes its client_info (number of RX antennas) to a file, which the client reads.
 * The server does not wait for the client to write back; the client can connect at any point.
 *
 * @param client_info The client information to publish.
 * @return The peer information (same as input, server is authoritative).
 */
static void server_publish_client_info(client_info_t client_info, char *descriptor_file)
{
  FILE *fp = fopen(descriptor_file, "wb");
  AssertFatal(fp != NULL, "Failed to open client info file for writing: %s\n", strerror(errno));
  size_t written = fwrite(&client_info, sizeof(client_info), 1, fp);
  AssertFatal(written == 1, "Failed to write client info to file\n");
  fclose(fp);
}

static client_info_t client_read_info(char *descriptor_file)
{
  client_info_t client_info;
  int tries = 0;
  while (tries < 10) {
    FILE *fp = fopen(descriptor_file, "rb");
    if (fp) {
      size_t read = fread(&client_info, sizeof(client_info), 1, fp);
      fclose(fp);
      if (read == 1) {
        return client_info;
      }
    }
    sleep(1);
    tries++;
  }
  AssertFatal(0, "Timeout waiting for client info\n");
  return client_info;
}

static void parse_ue_config(vrtsim_state_t *vrtsim_state)
{
  AssertFatal(vrtsim_state->num_ues > 0 && vrtsim_state->num_ues <= MAX_NUM_UES,
              "num_ues=%d out of range (1..%d)\n", vrtsim_state->num_ues, MAX_NUM_UES);

  for (int i = 0; i < vrtsim_state->num_ues; i++) {
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%s.ue_config.[%d]", VRTSIM_SECTION, i);

    char *antennas = NULL;
    int model_id = vrtsim_state->cirdb_model_id;
    double ds_ns = vrtsim_state->cirdb_ds_ns;
    double speed_mps = vrtsim_state->cirdb_speed_mps;

    paramdef_t ue_params[] = {
      {"antennas",  "Antenna config e.g. \"1x2\"", 0, .strptr = &antennas,  .defstrval = NULL,                           TYPE_STRING, 0},
      {"model_id",  "TDL model id 0..4",           0, .iptr   = &model_id,  .defintval = vrtsim_state->cirdb_model_id,   TYPE_INT,    0},
      {"ds_ns",     "Delay spread in ns",           0, .dblptr = &ds_ns,     .defdblval = vrtsim_state->cirdb_ds_ns,      TYPE_DOUBLE, 0},
      {"speed_mps", "Speed in m/s",                 0, .dblptr = &speed_mps, .defdblval = vrtsim_state->cirdb_speed_mps,  TYPE_DOUBLE, 0},
    };
    config_get(config_get_if(), ue_params, sizeofArray(ue_params), prefix);

    if (antennas != NULL) {
      int tx_ant, rx_ant;
      AssertFatal(sscanf(antennas, "%dx%d", &tx_ant, &rx_ant) == 2,
                  "Invalid antenna format '%s' for UE %d, use e.g. '1x2'\n", antennas, i);
      AssertFatal(tx_ant > 0 && tx_ant <= MAX_NUM_ANTENNAS_TX,
                  "Invalid TX antenna count %d for UE %d\n", tx_ant, i);
      AssertFatal(rx_ant > 0 && rx_ant <= MAX_NUM_ANTENNAS_TX,
                  "Invalid RX antenna count %d for UE %d\n", rx_ant, i);
      vrtsim_state->ue_conf[i].tx_ant = tx_ant;
      vrtsim_state->ue_conf[i].rx_ant = rx_ant;
    } else {
      vrtsim_state->ue_conf[i].tx_ant = 1;
      vrtsim_state->ue_conf[i].rx_ant = 1;
    }

    AssertFatal(model_id >= 0 && model_id <= 4,
                "Invalid model_id %d for UE %d (must be 0-4)\n", model_id, i);
    AssertFatal(ds_ns > 0,
                "Invalid ds_ns %.1f for UE %d (must be > 0)\n", ds_ns, i);
    AssertFatal(speed_mps >= 0,
                "Invalid speed_mps %.1f for UE %d (must be >= 0)\n", speed_mps, i);

    vrtsim_state->ue_conf[i].cir_conf.model_id  = model_id;
    vrtsim_state->ue_conf[i].cir_conf.ds_ns     = ds_ns;
    vrtsim_state->ue_conf[i].cir_conf.speed_mps = speed_mps;

    LOG_I(HW, "VRTSIM: UE %d - %dx%d antennas, Model %d (TDL-%c), DS %.1fns, Speed %.1fm/s\n",
          i,
          vrtsim_state->ue_conf[i].tx_ant,
          vrtsim_state->ue_conf[i].rx_ant,
          model_id, 'A' + model_id, ds_ns, speed_mps);
  }
}

static void compute_ue_antenna_offsets(vrtsim_state_t *vrtsim_state)
{
  int tx_offset = 0;
  int rx_offset = 0;

  for (int i = 0; i < vrtsim_state->num_ues; i++) {
    vrtsim_state->ue_conf[i].tx_offset = tx_offset;
    vrtsim_state->ue_conf[i].rx_offset = rx_offset;

    LOG_I(HW,
          "VRTSIM: UE %d - TX: %d antennas (offset %d), RX: %d antennas (offset %d)\n",
          i,
          vrtsim_state->ue_conf[i].tx_ant,
          tx_offset,
          vrtsim_state->ue_conf[i].rx_ant,
          rx_offset);

    tx_offset += vrtsim_state->ue_conf[i].tx_ant;
    rx_offset += vrtsim_state->ue_conf[i].rx_ant;
  }

  vrtsim_state->total_ul_streams = tx_offset;
  vrtsim_state->total_dl_streams = rx_offset;

  LOG_I(HW, "VRTSIM: Total streams - UL: %d, DL: %d\n", vrtsim_state->total_ul_streams, vrtsim_state->total_dl_streams);
}

static int vrtsim_connect(openair0_device_t *device)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;

  // Setup a shared memory channel
  if (vrtsim_state->role == ROLE_SERVER) {
    parse_ue_config(vrtsim_state);
    compute_ue_antenna_offsets(vrtsim_state);

    int ul_streams = (vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb)
                     ? vrtsim_state->num_ues * device->openair0_cfg[0].rx_num_channels
                     : vrtsim_state->total_ul_streams;
    vrtsim_state->channel =
        shm_td_iq_channel_create(DEFAULT_CHANNEL_NAME, ul_streams, vrtsim_state->total_dl_streams);
    size_t max_nsamps = 7680 * 2;
    vrtsim_state->ul_combine_buffer = calloc_or_fail(max_nsamps, sizeof(sample_t));
    vrtsim_state->ul_combine_buffer_len = max_nsamps;
    // Exchange peer info

    client_info_t client_info = {
        .num_ues = vrtsim_state->num_ues,
        .gnb_num_tx_ant = device->openair0_cfg[0].tx_num_channels,
        .gnb_num_rx_ant = device->openair0_cfg[0].rx_num_channels,
    };

    for (int i = 0; i < vrtsim_state->num_ues; i++)
      client_info.ues[i] = vrtsim_state->ue_conf[i];

    server_publish_client_info(client_info, vrtsim_state->connection_descriptor);
    vrtsim_state->peer_info.num_rx_antennas = vrtsim_state->client_num_rx_antennas;
    vrtsim_state->last_received_sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
    vrtsim_state->run_timing_thread = true;
    threadCreate(&vrtsim_state->timing_thread, vrtsim_timing_job, vrtsim_state, "vrtsim_timing", -1, OAI_PRIORITY_RT_MAX);
  } else {
    client_info_t client_info = client_read_info(vrtsim_state->connection_descriptor);
    AssertFatal(client_info.num_ues > 0, "Server did not publish valid num_ues\n");
    AssertFatal(vrtsim_state->ue_id < client_info.num_ues, "ue_id %d >= num_ues %d\n", vrtsim_state->ue_id, client_info.num_ues);

    vrtsim_state->ue = client_info.ues[vrtsim_state->ue_id];

    AssertFatal(vrtsim_state->ue.tx_ant == device->openair0_cfg[0].tx_num_channels,
                "Server expects UE %d to have %d TX antennas, this UE has %d\n",
                vrtsim_state->ue_id,
                vrtsim_state->ue.tx_ant,
                device->openair0_cfg[0].tx_num_channels);
    AssertFatal(vrtsim_state->ue.rx_ant == device->openair0_cfg[0].rx_num_channels,
                "Server expects UE %d to have %d RX antennas, this UE has %d\n",
                vrtsim_state->ue_id,
                vrtsim_state->ue.rx_ant,
                device->openair0_cfg[0].rx_num_channels);

    LOG_I(HW,
          "VRTSIM: UE %d/%d - %dx%d antennas, UL offset=%d, DL offset=%d\n",
          vrtsim_state->ue_id,
          client_info.num_ues,
          vrtsim_state->ue.tx_ant,
          vrtsim_state->ue.rx_ant,
          vrtsim_state->ue.tx_offset,
          vrtsim_state->ue.rx_offset);
    if (vrtsim_state->use_cirdb) {
      vrtsim_state->cirdb_model_id = vrtsim_state->ue.cir_conf.model_id;
      vrtsim_state->cirdb_ds_ns = vrtsim_state->ue.cir_conf.ds_ns;
      vrtsim_state->cirdb_speed_mps = vrtsim_state->ue.cir_conf.speed_mps;

      LOG_I(HW,
            "VRTSIM: UE %d channel - Model %d (TDL-%c), DS %.1fns, Speed %.1fm/s\n",
            vrtsim_state->ue_id,
            vrtsim_state->cirdb_model_id,
            'A' + vrtsim_state->cirdb_model_id,
            vrtsim_state->cirdb_ds_ns,
            vrtsim_state->cirdb_speed_mps);
    }
    vrtsim_state->channel = shm_td_iq_channel_connect(DEFAULT_CHANNEL_NAME, 10);
    vrtsim_state->peer_info.num_rx_antennas = client_info.gnb_num_rx_ant;
    vrtsim_state->last_received_sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
  }

  // Handle channel modelling after number of RX antennas are known
  int num_tx_stats = 1;
  if (vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb) {
    int num_actors = (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) ? vrtsim_state->total_dl_streams
                                                                                      : vrtsim_state->peer_info.num_rx_antennas;

    vrtsim_state->channel_modelling_actors = calloc_or_fail(num_actors, sizeof(Actor_t));
    for (int i = 0; i < num_actors; i++) {
      init_actor(&vrtsim_state->channel_modelling_actors[i], "chanmod", -1);
    }
    if (vrtsim_state->taps_socket) {
      taps_client_connect(0,
                          vrtsim_state->taps_socket,
                          device->openair0_cfg[0].tx_num_channels,
                          vrtsim_state->peer_info.num_rx_antennas,
                          &vrtsim_state->channel_desc);
    } else if (vrtsim_state->use_cirdb) {
      const char *yaml_path = NULL;
      const char *bin_path = NULL;

      if (vrtsim_state->cirdb_yaml && vrtsim_state->cirdb_yaml[0])
        yaml_path = vrtsim_state->cirdb_yaml;
      if (vrtsim_state->cirdb_file && vrtsim_state->cirdb_file[0])
        bin_path = vrtsim_state->cirdb_file;

      char yaml_buf[PATH_MAX];
      char bin_buf[PATH_MAX];

      if (!yaml_path || !bin_path) {
        const char *base = vrtsim_state->cirdb_path;
        if (base && base[0]) {
          if (!yaml_path) {
            snprintf(yaml_buf, sizeof(yaml_buf), "%s/%s", base, "vrtsim.yaml");
            yaml_path = yaml_buf;
          }
          if (!bin_path) {
            snprintf(bin_buf, sizeof(bin_buf), "%s/%s", base, "cir_db.bin");
            bin_path = bin_buf;
          }
        }
      }

      cirdb_select_opts_t sel = (cirdb_select_opts_t){0};
      sel.yaml_path = yaml_path;
      sel.bin_path = bin_path;
      AssertFatal(vrtsim_state->cirdb_model_id >= 0 && vrtsim_state->cirdb_model_id <= 4,
            "Invalid cirdb_model_id=%d (valid: 0..4)\n",
            vrtsim_state->cirdb_model_id);
      sel.want_model_id = vrtsim_state->cirdb_model_id;
      sel.want_ds_ns = (float)(vrtsim_state->cirdb_ds_ns > 0.0 ? vrtsim_state->cirdb_ds_ns : -1.0);
      sel.want_speed_mps = (float)(vrtsim_state->cirdb_speed_mps > 0.0 ? vrtsim_state->cirdb_speed_mps : -1.0);

      LOG_A(HW,
            "VRTSIM: CIR DB select yaml='%s' bin='%s'\n",
            sel.yaml_path ? sel.yaml_path : "(auto)",
            sel.bin_path ? sel.bin_path : "(auto)");

      // Multi-UE server: create per-UE channel descriptors
      if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) {
        for (int u = 0; u < vrtsim_state->num_ues; u++) {
          cirdb_select_opts_t ue_sel = sel;
          ue_sel.want_model_id = vrtsim_state->ue_conf[u].cir_conf.model_id;
          ue_sel.want_ds_ns = vrtsim_state->ue_conf[u].cir_conf.ds_ns;
          ue_sel.want_speed_mps = vrtsim_state->ue_conf[u].cir_conf.speed_mps;

          AssertFatal(ue_sel.want_model_id >= 0 && ue_sel.want_model_id <= 4,
                      "Invalid CIRDB model_id=%d for UE %d\n",
                      ue_sel.want_model_id,
                      u);

          cirdb_connect(u,
                        device->openair0_cfg[0].tx_num_channels,
                        vrtsim_state->ue_conf[u].rx_ant,
                        &ue_sel,
                        &vrtsim_state->channel_desc_per_ue[u]);

          channel_desc_t *cd = vrtsim_state->channel_desc_per_ue[u];
          AssertFatal(cd != NULL, "CIRDB failed to create channel_desc for UE %d\n", u);
          AssertFatal(cd->nb_tx == device->openair0_cfg[0].tx_num_channels,
                      "CIRDB shape mismatch UE%d: nb_tx=%d expected %d\n",
                      u, cd->nb_tx, device->openair0_cfg[0].tx_num_channels);
          LOG_I(HW, "VRTSIM: UE %d channel_desc=%p ch_ps=%p ch=%p nb_tx=%d nb_rx=%d\n",
                u, cd, cd->ch_ps, cd->ch, cd->nb_tx, cd->nb_rx);
          LOG_I(HW, "VRTSIM: UE %d CIRDB - Model %d (TDL-%c), antennas %dx%d, nb_rx=%d\n",
                u, ue_sel.want_model_id, 'A' + ue_sel.want_model_id,
                device->openair0_cfg[0].tx_num_channels, vrtsim_state->ue_conf[u].rx_ant, cd->nb_rx);
        }
        vrtsim_state->channel_desc = vrtsim_state->channel_desc_per_ue[0];
        pthread_mutex_init(&vrtsim_state->cirdb_mutex, NULL);
        LOG_A(HW, "VRTSIM: Multi-UE channel taps via CIR DB\n");
      } else {
        cirdb_connect(0,
                      device->openair0_cfg[0].tx_num_channels,
                      vrtsim_state->peer_info.num_rx_antennas,
                      &sel,
                      &vrtsim_state->channel_desc);
        LOG_A(HW, "VRTSIM: channel taps via CIR DB\n");
      }
    } else {
      load_channel_model(vrtsim_state);
    }
    num_tx_stats = vrtsim_state->peer_info.num_rx_antennas;
    if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) {
      vrtsim_state->peer_info.num_rx_antennas = vrtsim_state->total_dl_streams;
      num_tx_stats = vrtsim_state->total_dl_streams;
    }
  }
  vrtsim_state->tx_timing = calloc_or_fail(num_tx_stats, sizeof(tx_timing_t));
  for (int i = 0; i < num_tx_stats; i++) {
    vrtsim_state->tx_timing[i].tx_histogram.min_samples = 100;
    // Set the histogram range to 3000uS. Anything above that is not interesting
    vrtsim_state->tx_timing[i].tx_histogram.range = 3000.0;
  }

  return 0;
}

static int vrtsim_write_internal(vrtsim_state_t *vrtsim_state,
                                 openair0_timestamp_t timestamp,
                                 c16_t *samples,
                                 int nsamps,
                                 int aarx,
                                 int flags,
                                 int stats_index)
{
  tx_timing_t *tx_timing = &vrtsim_state->tx_timing[stats_index];

  uint64_t sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
  int64_t diff = timestamp - sample;
  double budget = diff / (vrtsim_state->sample_rate / 1e6);
  tx_timing->average_tx_budget = .05 * budget + .95 * tx_timing->average_tx_budget;
  histogram_add(&tx_timing->tx_histogram, budget);

  int ret = shm_td_iq_channel_tx(vrtsim_state->channel, timestamp, nsamps, aarx, (sample_t *)samples);

  if (ret == CHANNEL_ERROR_TOO_LATE) {
    tx_timing->tx_samples_late += nsamps;
  } else if (ret == CHANNEL_ERROR_TOO_EARLY) {
    tx_timing->tx_early += 1;
  }
  tx_timing->tx_samples_total += nsamps;

  return nsamps;
}

typedef struct {
  vrtsim_state_t *vrtsim_state;
  openair0_timestamp_t timestamp;
  c16_t *samples[MAX_NUM_ANTENNAS_TX];
  int nsamps;
  int nbAnt;
  int flags;
  int aarx;
  int batch_size;
  int num_batches;
  c16_t saved_samples[MAX_NUM_ANTENNAS_TX][SAVED_SAMPLES_LEN];
} channel_modelling_args_t;

static void perform_channel_modelling(void *arg)
{
  channel_modelling_args_t *channel_modelling_args = arg;
  vrtsim_state_t *vrtsim_state = channel_modelling_args->vrtsim_state;
  int nsamps = channel_modelling_args->nsamps;
  int aarx = channel_modelling_args->aarx;
  int nb_tx_ant = channel_modelling_args->nbAnt;
  c16_t **input_samples = (c16_t **)channel_modelling_args->samples;

  channel_desc_t *channel_desc = vrtsim_state->channel_desc;
  int local_aarx = aarx;
  int global_aarx = aarx;
  int target_ue = 0;

  if (vrtsim_state->role == ROLE_CLIENT) {
    global_aarx = vrtsim_state->ue_id * vrtsim_state->peer_info.num_rx_antennas + aarx;
  }

  if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) {
    if (aarx >= vrtsim_state->total_dl_streams) {
      LOG_E(HW, "VRTSIM: aarx=%d >= total_dl_streams=%d\n", aarx, vrtsim_state->total_dl_streams);
      return;
    }

    bool found = false;
    for (int u = 0; u < vrtsim_state->num_ues; u++) {
      int offset = vrtsim_state->ue_conf[u].rx_offset;
      int count = vrtsim_state->ue_conf[u].rx_ant;
      if (aarx >= offset && aarx < offset + count) {
        target_ue = u;
        local_aarx = aarx - offset;
        channel_desc = vrtsim_state->channel_desc_per_ue[u];
        found = true;
        break;
      }
    }

    if (!found) {
      LOG_E(HW, "VRTSIM: aarx=%d could not map to any UE (total_dl_streams=%d)\n", aarx, vrtsim_state->total_dl_streams);
      return;
    }
  }

  if (channel_desc == NULL) {
    LOG_E(HW, "VRTSIM: channel_desc is NULL for aarx=%d\n", aarx);
    return;
  }

  // Bounds check: ensure local_aarx is within channel_desc bounds
  if (local_aarx >= channel_desc->nb_rx) {
    LOG_E(HW,
          "VRTSIM: UE%d local_aarx=%d >= nb_rx=%d (global aarx=%d). CIRDB shape mismatch.\n",
          target_ue,
          local_aarx,
          channel_desc->nb_rx,
          aarx);
    return;
  }

  if (vrtsim_state->use_cirdb) {
    double seconds = (double)channel_modelling_args->timestamp / vrtsim_state->sample_rate;
    uint64_t elapsed_ns = (uint64_t)(seconds * 1e9 + 0.5);

    // Lock for multi-UE to prevent concurrent CIRDB updates
    if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) {
      pthread_mutex_lock(&vrtsim_state->cirdb_mutex);
    }

    cirdb_update(elapsed_ns);

    if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) {
      pthread_mutex_unlock(&vrtsim_state->cirdb_mutex);
    }
  }

  cf_t channel_impulse_response[nb_tx_ant][channel_desc->channel_length];
  cf_t *channel_impulse_response_p[nb_tx_ant];
  if (!vrtsim_state->taps_socket && !vrtsim_state->use_cirdb) {
    const float pathloss_linear = powf(10, channel_desc->path_loss_dB / 20.0);
    // Convert channel impulse response to float + apply pathloss
    for (int aatx = 0; aatx < nb_tx_ant; aatx++) {
      const struct complexd *channelModel = channel_desc->ch[local_aarx + (aatx * channel_desc->nb_rx)];
      for (int i = 0; i < channel_desc->channel_length; i++) {
        channel_impulse_response[aatx][i].r = channelModel[i].r * pathloss_linear;
        channel_impulse_response[aatx][i].i = channelModel[i].i * pathloss_linear;
      }
      channel_impulse_response_p[aatx] = channel_impulse_response[aatx];
    }
  } else {
    for (int aatx = 0; aatx < nb_tx_ant; aatx++) {
      struct complexf *channelModel = channel_desc->ch_ps[local_aarx + (aatx * channel_desc->nb_rx)];
      channel_impulse_response_p[aatx] = channelModel;
    }
  }

  for (int batch_index = 0; batch_index < channel_modelling_args->num_batches; batch_index++) {
    const int start_sample = batch_index * channel_modelling_args->batch_size;
    const int num_samples = min(channel_modelling_args->batch_size, nsamps - start_sample);
    if (start_sample >= nsamps) {
      break;
    }

    const int aligned_batch = ceil_mod(num_samples, (512 / 8) / sizeof(cf_t));
    cf_t samples[aligned_batch] __attribute__((aligned(64)));
    memset(samples, 0, sizeof(samples));

    // Apply noise from global settings (only valid samples)
    get_noise_vector((float *)samples, num_samples * 2);

    for (int aatx = 0; aatx < nb_tx_ant; aatx++) {
      cf_t *impulse_response = channel_impulse_response_p[aatx];
      for (int i = 0; i < num_samples; i++) {
        const int gi = start_sample + i;
        for (int l = 0; l < channel_desc->channel_length; l++) {
          const int idx = gi - l;
          // TODO: Use AVX2 for this
          c16_t tx_input = idx >= 0 ? input_samples[aatx][idx]
                                    : channel_modelling_args->saved_samples[aatx][SAVED_SAMPLES_LEN + idx];
          samples[i].r += tx_input.r * impulse_response[l].r - tx_input.i * impulse_response[l].i;
          samples[i].i += tx_input.i * impulse_response[l].r + tx_input.r * impulse_response[l].i;
        }
      }
    }

    // Convert to c16_t (only once per batch)
    c16_t samples_out[aligned_batch] __attribute__((aligned(64)));
#if defined(__AVX512F__)
    for (int i = 0; i < aligned_batch / 8; i++) {
      simde__m512 *in = (simde__m512 *)&samples[i * 8];
      simde__m256i *out = (simde__m256i *)&samples_out[i * 8];
      *out = simde_mm512_cvtsepi32_epi16(simde_mm512_cvtps_epi32(*in));
    }
#elif defined(__AVX2__)
    for (int i = 0; i < aligned_batch / 4; i++) {
      simde__m256 *in = (simde__m256 *)&samples[i * 4];
      simde__m128i *out = (simde__m128i *)&samples_out[i * 4];
      *out = simde_mm256_cvtsepi32_epi16(simde_mm256_cvtps_epi32(*in));
    }
#else
    for (int i = 0; i < num_samples; i++) {
      samples_out[i].r = lroundf(samples[i].r);
      samples_out[i].i = lroundf(samples[i].i);
    }
#endif

    // Write the batch as a single contiguous transfer
    vrtsim_write_internal(channel_modelling_args->vrtsim_state,
                          channel_modelling_args->timestamp + start_sample,
                          samples_out,
                          num_samples,
                          global_aarx,
                          channel_modelling_args->flags,
                          aarx);
  }
}

static int vrtsim_write_with_chanmod(vrtsim_state_t *vrtsim_state,
                                     openair0_timestamp_t timestamp,
                                     void **samplesVoid,
                                     int nsamps,
                                     int nbAnt,
                                     int flags)
{
  // Sample history for channel impulse response
  static c16_t saved_samples[MAX_NUM_ANTENNAS_TX][SAVED_SAMPLES_LEN] __attribute__((aligned(32))) = {0};
  // Indicates what samples are saves in saved_samples
  static openair0_timestamp_t last_timestamp = 0;
  const int batch_size = 4096;

  AssertFatal(nbAnt <= MAX_NUM_ANTENNAS_TX, "Number of antennas %d exceeds maximum %d\n", nbAnt, MAX_NUM_ANTENNAS_TX);
  for (int aarx = 0; aarx < vrtsim_state->peer_info.num_rx_antennas; aarx++) {
    notifiedFIFO_elt_t *task = newNotifiedFIFO_elt(sizeof(channel_modelling_args_t), 0, NULL, perform_channel_modelling);
    channel_modelling_args_t *args = (channel_modelling_args_t *)NotifiedFifoData(task);
    args->vrtsim_state = vrtsim_state;
    args->timestamp = timestamp;
    args->nsamps = nsamps;
    args->nbAnt = nbAnt;
    args->flags = flags;
    args->aarx = aarx;
    args->batch_size = batch_size;
    args->num_batches = (nsamps + batch_size - 1) / batch_size;
    for (int i = 0; i < nbAnt; i++) {
      args->samples[i] = samplesVoid[i];
    }

    // Fill in saved_samples
    size_t gap_samples = timestamp - last_timestamp;
    if (gap_samples > 0) {
      size_t gap_samples_needed = min(SAVED_SAMPLES_LEN, gap_samples);
      for (int aatx = 0; aatx < nbAnt; aatx++) {
        memset(&args->saved_samples[aatx][SAVED_SAMPLES_LEN - gap_samples_needed], 0, sizeof(c16_t) * gap_samples_needed);
        if (gap_samples < SAVED_SAMPLES_LEN) {
          size_t samples_from_saved = SAVED_SAMPLES_LEN - gap_samples_needed;
          memcpy(&args->saved_samples[aatx][0], &saved_samples[aatx][SAVED_SAMPLES_LEN - samples_from_saved], sizeof(c16_t) * samples_from_saved);
        }
      }
    } else {
      for (int aatx = 0; aatx < nbAnt; aatx++)
        memcpy(&args->saved_samples[aatx][0], saved_samples[aatx], sizeof(c16_t) * SAVED_SAMPLES_LEN);
    }
    pushNotifiedFIFO(&vrtsim_state->channel_modelling_actors[aarx].fifo, task);
  }

  // Save samples for next round
  if (nsamps < SAVED_SAMPLES_LEN) {
    for (int aatx = 0; aatx < nbAnt; aatx++) {
      memmove(&saved_samples[aatx][0], &saved_samples[aatx][nsamps], sizeof(c16_t) * (SAVED_SAMPLES_LEN - nsamps));
      memcpy(&saved_samples[aatx][SAVED_SAMPLES_LEN - nsamps], samplesVoid[aatx], sizeof(c16_t) * nsamps);
    }
  } else {
    for (int aatx = 0; aatx < nbAnt; aatx++) {
      c16_t* samples = (c16_t*)samplesVoid[aatx];
      memcpy(saved_samples[aatx], &samples[nsamps - SAVED_SAMPLES_LEN], sizeof(c16_t) * (SAVED_SAMPLES_LEN));
    }
  }

  last_timestamp = timestamp + nsamps;
  return nsamps;
}

static int vrtsim_write(openair0_device_t *device,
                        openair0_timestamp_t timestamp,
                        void **samplesVoid,
                        int nsamps,
                        int nbAnt,
                        int flags)
{
  AssertFatal(nsamps > 0, "Number of samples must be greater than 0\n");
  AssertFatal(nbAnt > 0 && nbAnt <= MAX_NUM_ANTENNAS_TX,
              "Number of antennas %d must be between 1 and %d\n",
              nbAnt,
              MAX_NUM_ANTENNAS_TX);
  AssertFatal(timestamp >= 0, "Timestamp must be non-negative, got %ld\n", timestamp);
  timestamp -= device->openair0_cfg->command_line_sample_advance;
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  bool channel_modelling = vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb;
  if (channel_modelling) {
    return vrtsim_write_with_chanmod(vrtsim_state, timestamp, samplesVoid, nsamps, nbAnt, flags);
  }
  if (vrtsim_state->role == ROLE_CLIENT) {
    for (int aatx = 0; aatx < nbAnt; aatx++) {
      int global_ul_ant = vrtsim_state->ue.tx_offset + aatx;
      vrtsim_write_internal(vrtsim_state, timestamp, (c16_t *)samplesVoid[aatx], nsamps, global_ul_ant, flags, 0);
    }
    return nsamps;
  } else {
    AssertFatal(nbAnt == 1, "Multi-TX gNB not yet supported in multi-UE no-chanmod mode\n");
    for (int u = 0; u < vrtsim_state->num_ues; u++) {
      int rx_offset = vrtsim_state->ue_conf[u].rx_offset;
      int num_rx_ant = vrtsim_state->ue_conf[u].rx_ant;
      for (int aarx = 0; aarx < num_rx_ant; aarx++) {
        int global_dl_ant = rx_offset + aarx;
        vrtsim_write_internal(vrtsim_state, timestamp, (c16_t *)samplesVoid[0], nsamps, global_dl_ant, flags, 0);
      }
    }
    return nsamps;
  }
}

static int vrtsim_write_beams(openair0_device_t *device,
                              openair0_timestamp_t timestamp,
                              void ***buff,
                              int nsamps,
                              int nb_antennas_tx,
                              int num_beams,
                              int flags)
{
  vrtsim_write(device, timestamp, (void **)buff[0], nsamps, nb_antennas_tx, flags);
  return nsamps;
}

static int vrtsim_read(openair0_device_t *device, openair0_timestamp_t *ptimestamp, void **samplesVoid, int nsamps, int nbAnt)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  if (shm_td_iq_channel_is_aborted(vrtsim_state->channel)) {
    return 0;
  }
  if (vrtsim_state->role == ROLE_SERVER) {
    uint64_t timeout_uS = 0; // 0 means no timeout
    shm_td_iq_channel_wait(vrtsim_state->channel, vrtsim_state->last_received_sample + nsamps, timeout_uS);
  } else {
    uint64_t start_sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
    uint64_t timeout_uS = 2 * 1000 * 1000; // 2 seconds timeout waiting for sample number to change
    //
    while (shm_td_iq_channel_wait(vrtsim_state->channel, vrtsim_state->last_received_sample + nsamps, timeout_uS) == 1) {
      uint64_t sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
      if (sample == start_sample) {
        LOG_E(HW,
              "VRTSIM: Read timeout waiting for sample %lu to change, aborting channel\n",
              vrtsim_state->last_received_sample + nsamps);
        shm_td_iq_channel_abort(vrtsim_state->channel);
        break;
      } else {
        start_sample = sample;
      }
    }
  }
  if (vrtsim_state->role == ROLE_SERVER) {
    if (vrtsim_state->num_ues > 1) {
      /* Multi-UE UL combining */
      if (nsamps > vrtsim_state->ul_combine_buffer_len) {
        sample_t *tmp = realloc(vrtsim_state->ul_combine_buffer, nsamps * sizeof(sample_t));
        AssertFatal(tmp != NULL, "Failed to realloc UL combine buffer\n");
        vrtsim_state->ul_combine_buffer = tmp;
        vrtsim_state->ul_combine_buffer_len = nsamps;
      }
      for (int aarx = 0; aarx < nbAnt; aarx++)
        memset(samplesVoid[aarx], 0, nsamps * sizeof(sample_t));
      for (int u = 0; u < vrtsim_state->num_ues; u++) {
        for (int aarx = 0; aarx < nbAnt; aarx++) {
          int stream = u * nbAnt + aarx;
          int ret = shm_td_iq_channel_rx(vrtsim_state->channel,
                                         vrtsim_state->last_received_sample,
                                         nsamps,
                                         stream,
                                         vrtsim_state->ul_combine_buffer);
          if (ret == CHANNEL_ERROR_TOO_LATE) {
            vrtsim_state->rx_samples_late += nsamps;
          } else if (ret == CHANNEL_ERROR_TOO_EARLY) {
            vrtsim_state->rx_early += 1;
          }
          int16_t *out = (int16_t *)samplesVoid[aarx];
          int16_t *in  = (int16_t *)vrtsim_state->ul_combine_buffer;
          for (int i = 0; i < nsamps * 2; i++) {
            int32_t sum = (int32_t)out[i] + (int32_t)in[i];
            out[i] = (int16_t)((sum > 32767) ? 32767 : (sum < -32768) ? -32768 : sum);
          }
        }
      }
    } else {
      /* Single-UE server UL read */
      int ret = shm_td_iq_channel_rx(vrtsim_state->channel, vrtsim_state->last_received_sample, nsamps, 0, samplesVoid[0]);
      if (ret == CHANNEL_ERROR_TOO_LATE) {
        vrtsim_state->rx_samples_late += nsamps;
      } else if (ret == CHANNEL_ERROR_TOO_EARLY) {
        vrtsim_state->rx_early += 1;
      }
      for (int aarx = 1; aarx < nbAnt; aarx++) {
        if (samplesVoid[aarx] != NULL)
          memcpy(samplesVoid[aarx], samplesVoid[0], nsamps * sizeof(sample_t));
      }
    }
  } else {
    for (int aarx = 0; aarx < nbAnt; aarx++) {
      int global_dl_ant = vrtsim_state->ue.rx_offset + aarx;
      int ret =
          shm_td_iq_channel_rx(vrtsim_state->channel, vrtsim_state->last_received_sample, nsamps, global_dl_ant, samplesVoid[aarx]);
      if (ret == CHANNEL_ERROR_TOO_LATE) {
        vrtsim_state->rx_samples_late += nsamps;
      } else if (ret == CHANNEL_ERROR_TOO_EARLY) {
        vrtsim_state->rx_early += 1;
      }
    }
  }
  vrtsim_state->rx_samples_total += nsamps;
  *ptimestamp = vrtsim_state->last_received_sample;
  vrtsim_state->last_received_sample += nsamps;
  return nsamps;
}

static void vrtsim_end(openair0_device_t *device)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->run_timing_thread) {
    vrtsim_state->run_timing_thread = false;
    int ret = pthread_join(vrtsim_state->timing_thread, NULL);
    AssertFatal(ret == 0, "pthread_join() failed: errno: %d, %s\n", errno, strerror(errno));
  }

  tx_timing_t *tx_timing = vrtsim_state->tx_timing;
  if (vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb) {
    for (int i = 0; i < vrtsim_state->peer_info.num_rx_antennas; i++) {
      shutdown_actor(&vrtsim_state->channel_modelling_actors[i]);
    }
    free(vrtsim_state->channel_modelling_actors);
    if (vrtsim_state->use_cirdb && vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) {
      pthread_mutex_destroy(&vrtsim_state->cirdb_mutex);
    }
    for (int i = 1; i < vrtsim_state->peer_info.num_rx_antennas; i++) {
      histogram_merge(&tx_timing->tx_histogram, &tx_timing[i].tx_histogram);
      tx_timing->tx_early += tx_timing[i].tx_early;
      tx_timing->tx_samples_late += tx_timing[i].tx_samples_late;
      tx_timing->average_tx_budget += tx_timing[i].average_tx_budget;
      tx_timing->tx_samples_total += tx_timing[i].tx_samples_total;
    }
    tx_timing->average_tx_budget /= vrtsim_state->peer_info.num_rx_antennas;
    free_noise_device();
    if (vrtsim_state->use_cirdb) {
      cirdb_stop();
    } else if (vrtsim_state->taps_socket) {
      taps_client_stop();
    }
  }
  if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->ul_combine_buffer) {
    free(vrtsim_state->ul_combine_buffer);
    vrtsim_state->ul_combine_buffer = NULL;
  }
  shm_td_iq_channel_abort(vrtsim_state->channel);
  sleep(1);
  shm_td_iq_channel_destroy(vrtsim_state->channel);

  LOG_I(HW,
        "VRTSIM: Realtime issues: TX %.2f%%, RX %.2f%%\n",
        tx_timing->tx_samples_late / (float)tx_timing->tx_samples_total * 100,
        vrtsim_state->rx_samples_late / (float)vrtsim_state->rx_samples_total * 100);
  LOG_I(HW,
        "VRTSIM: Read/write too early (suspected radio implementaton error) TX: %lu, RX: %lu\n",
        tx_timing->tx_early,
        vrtsim_state->rx_early);
  LOG_I(HW, "VRTSIM: Average TX budget %.3lf uS (more is better)\n", tx_timing->average_tx_budget);
  histogram_print(&tx_timing->tx_histogram);
  free(vrtsim_state->tx_timing);

  if (vrtsim_state->role == ROLE_SERVER) {
    int ret = remove(vrtsim_state->connection_descriptor);
    if (ret != 0) {
      LOG_E(HW, "Failed to remove connection descriptor file %s: %s\n", vrtsim_state->connection_descriptor, strerror(errno));
    } else {
      LOG_A(HW, "Removed connection descriptor file %s\n", vrtsim_state->connection_descriptor);
    }
  }
}

static int vrtsim_stub(openair0_device_t *device)
{
  return 0;
}

static int vrtsim_stub2(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  return 0;
}

static int vrtsim_set_freq(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  vrtsim_state_t *s = device->priv;
  s->rx_freq = openair0_cfg->rx_freq[0];
  return 0;
}

static int vrtsim_set_beams(openair0_device_t *device, uint64_t beam_map, openair0_timestamp_t timestamp)
{
  return 0;
}

static int vrtsim_set_beams2(openair0_device_t *device, int *beam_ids, int num_beams, openair0_timestamp_t timestamp)
{
  return 0;
}

openair0_timestamp_t vrtsim_get_timestamp(openair0_device_t *device, struct timespec *ts)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  uint64_t diff = (ts->tv_sec - vrtsim_state->start_ts.tv_sec) * 1000000000 + (ts->tv_nsec - vrtsim_state->start_ts.tv_nsec);
  double diff_samples = vrtsim_state->sample_rate * vrtsim_state->timescale * diff / 1e9;
  return diff_samples > 0 ? diff_samples : 0;
}

__attribute__((__visibility__("default"))) int device_init(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  randominit();
  vrtsim_state_t *vrtsim_state = calloc_or_fail(1, sizeof(vrtsim_state_t));
  vrtsim_readconfig(vrtsim_state);
  LOG_I(HW,
        "Running as %s\n",
        vrtsim_state->role == ROLE_SERVER ? "server: waiting for client to connect" : "client: will connect to a vrtsim server");
  device->trx_start_func = vrtsim_connect;
  device->trx_reset_stats_func = vrtsim_stub;
  device->trx_end_func = vrtsim_end;
  device->trx_stop_func = vrtsim_stub;
  device->trx_set_freq_func = vrtsim_set_freq;
  device->trx_set_gains_func = vrtsim_stub2;
  device->trx_write_func = vrtsim_write;
  device->trx_read_func = vrtsim_read;
  device->trx_write_beams_func = vrtsim_write_beams;
  device->trx_set_beams = vrtsim_set_beams;
  device->trx_set_beams2 = vrtsim_set_beams2;
  if (vrtsim_state->role == ROLE_SERVER) {
    device->get_timestamp = vrtsim_get_timestamp;
  }

  device->type = RFSIMULATOR;
  device->openair0_cfg = &openair0_cfg[0];
  device->priv = vrtsim_state;
  device->trx_write_init = vrtsim_stub;
  vrtsim_state->last_received_sample = 0;
  vrtsim_state->sample_rate = openair0_cfg->sample_rate;
  vrtsim_state->rx_freq = openair0_cfg->rx_freq[0];
  vrtsim_state->tx_bw = openair0_cfg->tx_bw;
  vrtsim_state->tx_num_channels = openair0_cfg->tx_num_channels;
  vrtsim_state->rx_num_channels = openair0_cfg->rx_num_channels;

  if (vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb) {
    init_channelmod();
    randominit();
    int noise_power_dBFS = get_noise_power_dBFS();
    int16_t noise_power = noise_power_dBFS == INVALID_DBFS_VALUE ? 0 : (int16_t)(32767.0 / powf(10.0, .05 * -noise_power_dBFS));
    LOG_A(HW, "VRTSIM: Noise power %d sample value\n", noise_power);
    init_noise_device(noise_power);
  }
  return 0;
}
