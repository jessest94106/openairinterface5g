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
#include <stdint.h>

#include <libbladeRF.h>
#include "common_lib.h"

/*! \brief BladeRF specific data structure */
typedef struct {
  //! opaque BladeRF device struct. An empty ("") or NULL device identifier will result in the first encountered device being opened
  //! (using the first discovered backend)
  struct bladerf *dev;

  //! Number of buffers
  unsigned int num_buffers;
  //! Buffer size
  unsigned int buffer_size;
  //! Number of transfers
  unsigned int num_transfers;
  //! RX timeout
  unsigned int rx_timeout_ms;
  //! TX timeout
  unsigned int tx_timeout_ms;
  //! Metadata for RX
  struct bladerf_metadata meta_rx;
  //! Metadata for TX
  struct bladerf_metadata meta_tx;
  //! Sample rate
  unsigned int sample_rate;

  // --------------------------------
  // Debug and output control
  // --------------------------------
  //! Number of underflows
  int num_underflows;
  //! Number of overflows
  int num_overflows;
  //! number of RX errors
  int num_rx_errors;
  //! Number of TX errors
  int num_tx_errors;

  //! timestamp of current TX
  uint64_t tx_current_ts;
  //! timestamp of current RX
  uint64_t rx_current_ts;
  //! number of actual samples transmitted
  uint64_t tx_actual_nsamps;
  //! number of actual samples received
  uint64_t rx_actual_nsamps;
  //! number of TX samples
  uint64_t tx_nsamps;
  //! number of RX samples
  uint64_t rx_nsamps;
  //! number of TX count
  uint64_t tx_count;
  //! number of RX count
  uint64_t rx_count;
} brf_state_t;

int brf_error(int status);

/** @addtogroup _BLADERF_PHY_RF_INTERFACE_
 * @{
 */

#define BLADERF_CHECK(COND, FMT, ...)                                                        \
  if (COND) {                                                                                \
    LOG_I(HW, FMT "\n" __VA_OPT__(, ) __VA_ARGS__);                                          \
  } else {                                                                                   \
    LOG_E(HW, "Failed: " FMT ": %s\n" __VA_OPT__(, ) __VA_ARGS__, bladerf_strerror(status)); \
    return -1;                                                                               \
  }

const bladerf_format format = BLADERF_FORMAT_SC16_Q11_META;

/*! \brief Start BladeRF
 * \param device the hardware to use
 * \returns 0 on success
 */
static int trx_brf_start(openair0_device_t *device)
{
  brf_state_t *brf = device->priv;
  int status;

  brf->meta_tx.flags = 0;

  /* Configure the device's TX module for use with the sync interface.
   * SC16 Q11 samples *with* metadata are used. */
  status = bladerf_sync_config(brf->dev, BLADERF_MODULE_TX, format,
                               brf->num_buffers, brf->buffer_size, brf->num_transfers, brf->tx_timeout_ms);
  BLADERF_CHECK(status == 0,
                "Set TX sync interface num_buffers %u, buffer_size %u, num_transfer %u, tx_timeout_ms %u",
                brf->num_buffers, brf->buffer_size, brf->num_transfers, brf->tx_timeout_ms);

  /* Configure the device's RX module for use with the sync interface.
   * SC16 Q11 samples *with* metadata are used. */
  status = bladerf_sync_config(brf->dev, BLADERF_MODULE_RX, format,
                               brf->num_buffers, brf->buffer_size, brf->num_transfers, brf->rx_timeout_ms);
  BLADERF_CHECK(status == 0,
                "Set RX sync interface num_buffers %u, buffer_size %u, num_transfer %u, rx_timeout_ms %u",
                brf->num_buffers, brf->buffer_size, brf->num_transfers, brf->rx_timeout_ms);

  /* We must always enable the TX module after calling bladerf_sync_config(), and
   * before  attempting to TX samples via  bladerf_sync_tx(). */
  status = bladerf_enable_module(brf->dev, BLADERF_MODULE_TX, true);
  BLADERF_CHECK(status == 0, "Enable TX module");

  /* We must always enable the RX module after calling bladerf_sync_config(), and
   * before  attempting to RX samples via  bladerf_sync_rx(). */
  status = bladerf_enable_module(brf->dev, BLADERF_MODULE_RX, true);
  BLADERF_CHECK(status == 0, "Enable RX module");

  return 0;
}


/*! \brief Called to send samples to the BladeRF RF target
      \param device pointer to the device structure specific to the RF hardware target
      \param timestamp The timestamp at which the first sample MUST be sent
      \param buff Buffer which holds the samples
      \param nsamps number of samples to be sent
      \param cc index of the component carrier
      \param flags Ignored for the moment
      \returns 0 on success
*/
static int trx_brf_write(openair0_device_t *device, openair0_timestamp_t ptimestamp, void **buff, int nsamps, int cc, int flags)
{
    int status;
    brf_state_t *brf = (brf_state_t*)device->priv;
    /* BRF has only 1 rx/tx chaine : is it correct? */
    int16_t *samples = (int16_t*)buff[0];
    ptimestamp -= device->openair0_cfg->command_line_sample_advance - device->openair0_cfg->tx_sample_advance;
    //memset(&brf->meta_tx, 0, sizeof(brf->meta_tx));
    // When  BLADERF_META_FLAG_TX_NOW is used the timestamp is not used, so one can't schedule a tx
    if (brf->meta_tx.flags == 0 )
        brf->meta_tx.flags = (BLADERF_META_FLAG_TX_BURST_START);// | BLADERF_META_FLAG_TX_BURST_END);// |  BLADERF_META_FLAG_TX_NOW);


    brf->meta_tx.timestamp= (uint64_t) (ptimestamp);
    status = bladerf_sync_tx(brf->dev, samples, (unsigned int) nsamps, &brf->meta_tx, 2*brf->tx_timeout_ms);

    if (brf->meta_tx.flags == BLADERF_META_FLAG_TX_BURST_START)
        brf->meta_tx.flags =  BLADERF_META_FLAG_TX_UPDATE_TIMESTAMP;


    if (status != 0) {
        //fprintf(stderr,"Failed to TX sample: %s\n", bladerf_strerror(status));
        brf->num_tx_errors++;
        brf_error(status);
    } else if (brf->meta_tx.status & BLADERF_META_STATUS_UNDERRUN) {
        /* libbladeRF does not report this status. It is here for future use. */
        fprintf(stderr, "TX Underrun detected. %u valid samples were read.\n",  brf->meta_tx.actual_count);
        brf->num_underflows++;
    }
    //printf("Provided TX timestampe  %u, meta timestame %u\n", ptimestamp,brf->meta_tx.timestamp);

    //    printf("tx status %d \n",brf->meta_tx.status);
    brf->tx_current_ts=brf->meta_tx.timestamp;
    brf->tx_actual_nsamps+=brf->meta_tx.actual_count;
    brf->tx_nsamps+=nsamps;
    brf->tx_count++;


    return nsamps; //brf->meta_tx.actual_count;
}


/*! \brief Receive samples from hardware.
 * Read \ref nsamps samples from each channel to buffers. buff[0] is the array for
 * the first channel. *ptimestamp is the time at which the first sample
 * was received.
 * \param device the hardware to use
 * \param[out] ptimestamp the time at which the first sample was received.
 * \param[out] buff An array of pointers to buffers for received samples. The buffers must be large enough to hold the number of samples \ref nsamps.
 * \param nsamps Number of samples. One sample is 2 byte I + 2 byte Q => 4 byte.
 * \param cc  Index of component carrier
 * \returns number of samples read
*/
static int trx_brf_read(openair0_device_t *device, openair0_timestamp_t *ptimestamp, void **buff, int nsamps, int cc)
{
    int status=0;
    brf_state_t *brf = (brf_state_t*)device->priv;

    // BRF has only one rx/tx chain
    int16_t *samples = (int16_t*)buff[0];

    brf->meta_rx.actual_count = 0;
    brf->meta_rx.flags = BLADERF_META_FLAG_RX_NOW;
    status = bladerf_sync_rx(brf->dev, samples, (unsigned int) nsamps, &brf->meta_rx, 2*brf->rx_timeout_ms);

    //  printf("Current RX timestampe  %u, nsamps %u, actual %u, cc %d\n",  brf->meta_rx.timestamp, nsamps, brf->meta_rx.actual_count, cc);

    if (status != 0) {
        fprintf(stderr, "RX failed: %s\n", bladerf_strerror(status));
        //    printf("RX failed: %s\n", bladerf_strerror(status));
        brf->num_rx_errors++;
    } else if ( brf->meta_rx.status & BLADERF_META_STATUS_OVERRUN) {
        brf->num_overflows++;
        printf("RX overrun (%d) is detected. t=" "%" PRIu64 "Got %u samples. nsymps %d\n",
               brf->num_overflows,brf->meta_rx.timestamp,  brf->meta_rx.actual_count, nsamps);
    }

    if (brf->meta_rx.actual_count != nsamps) {
        printf("RX bad samples count, wanted %d, got %d\n", nsamps, brf->meta_rx.actual_count);
    }

    //printf("Current RX timestampe  %u\n",  brf->meta_rx.timestamp);
    //printf("[BRF] (buff %p) ts=0x%"PRIu64" %s\n",samples, brf->meta_rx.timestamp,bladerf_strerror(status));
    brf->rx_current_ts=brf->meta_rx.timestamp;
    brf->rx_actual_nsamps+=brf->meta_rx.actual_count;
    brf->rx_nsamps+=nsamps;
    brf->rx_count++;


    *ptimestamp = brf->meta_rx.timestamp;

    return nsamps; //brf->meta_rx.actual_count;

}


/*! \brief Terminate operation of the BladeRF transceiver -- free all associated resources
 * \param device the hardware to use
 */
void trx_brf_end(openair0_device_t *device)
{
    int status;
    brf_state_t *brf = (brf_state_t*)device->priv;
    // Disable RX module, shutting down our underlying RX stream
    if ((status=bladerf_enable_module(brf->dev, BLADERF_MODULE_RX, false))  != 0) {
        fprintf(stderr, "Failed to disable RX module: %s\n", bladerf_strerror(status));
    }
    if ((status=bladerf_enable_module(brf->dev, BLADERF_MODULE_TX, false))  != 0) {
        fprintf(stderr, "Failed to disable TX module: %s\n",  bladerf_strerror(status));
    }
    bladerf_close(brf->dev);
    exit(1);
}


/*! \brief print the BladeRF statistics
* \param device the hardware to use
* \returns  0 on success
*/
int trx_brf_get_stats(openair0_device_t *device)
{
    return(0);
}


/*! \brief Reset the BladeRF statistics
* \param device the hardware to use
* \returns  0 on success
*/
int trx_brf_reset_stats(openair0_device_t *device)
{
    return(0);
}


/*! \brief Stop BladeRF
 * \param card the hardware to use
 * \returns 0 in success
 */
int trx_brf_stop(openair0_device_t *device)
{
    return(0);
}


/*! \brief Set frequencies (TX/RX)
 * \param device the hardware to use
 * \param openair0_cfg1 openair0 Config structure (ignored. It is there to comply with RF common API)
 * \returns 0 in success
 */
int trx_brf_set_freq(openair0_device_t *device, openair0_config_t *openair0_cfg1)
{
    int status;
    brf_state_t *brf = (brf_state_t *)device->priv;
    openair0_config_t *openair0_cfg = (openair0_config_t *)device->openair0_cfg;


    if ((status=bladerf_set_frequency(brf->dev, BLADERF_MODULE_TX, (unsigned int) openair0_cfg->tx_freq[0])) != 0) {
        fprintf(stderr,"Failed to set TX frequency: %s\n",bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set TX Frequency to %u\n", (unsigned int) openair0_cfg->tx_freq[0]);

    if ((status=bladerf_set_frequency(brf->dev, BLADERF_MODULE_RX, (unsigned int) openair0_cfg->rx_freq[0])) != 0) {
        fprintf(stderr,"Failed to set RX frequency: %s\n",bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set RX frequency to %u\n",(unsigned int)openair0_cfg->rx_freq[0]);

    return(0);

}


/*! \brief Set Gains (TX/RX)
 * \param device the hardware to use
 * \param openair0_cfg openair0 Config structure
 * \returns 0 in success
 */
int trx_brf_set_gains(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
    return(0);
}
int trx_brf_write_init(openair0_device_t *device)
{
    return 0;
}

/*! \brief Initialize Openair BLADERF target. It returns 0 if OK
 * \param device the hardware to use
 * \param openair0_cfg RF frontend parameters set by application
 * \returns 0 on success
 */
int device_init(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
    int status;
    brf_state_t *brf = (brf_state_t*)malloc(sizeof(brf_state_t));
    memset(brf, 0, sizeof(brf_state_t));
    /* device specific */
    //openair0_cfg->txlaunch_wait = 1;//manage when TX processing is triggered
    //openair0_cfg->txlaunch_wait_slotcount = 1; //manage when TX processing is triggered

    // init required params
    switch ((int)openair0_cfg->sample_rate) {
    case 30720000:
        openair0_cfg->samples_per_packet    = 2048;
        openair0_cfg->tx_sample_advance     = 0;
        break;
    case 15360000:
        openair0_cfg->samples_per_packet    = 2048;
        openair0_cfg->tx_sample_advance     = 0;
        break;
    case 7680000:
        openair0_cfg->samples_per_packet    = 1024;
        openair0_cfg->tx_sample_advance     = 0;
        break;
    case 1920000:
        openair0_cfg->samples_per_packet    = 256;
        openair0_cfg->tx_sample_advance     = 50;
        break;
    default:
        printf("Error: unknown sampling rate %f\n",openair0_cfg->sample_rate);
        exit(-1);
        break;
    }

    //  The number of buffers to use in the underlying data stream
    brf->num_buffers   = 128;
    // the size of the underlying stream buffers, in samples
    brf->buffer_size   = (unsigned int) openair0_cfg->samples_per_packet;//*sizeof(int32_t); // buffer size = 4096 for sample_len of 1024
    brf->num_transfers = 16;
    brf->rx_timeout_ms = 0;
    brf->tx_timeout_ms = 0;
    brf->sample_rate=(unsigned int)openair0_cfg->sample_rate;

    memset(&brf->meta_rx, 0, sizeof(brf->meta_rx));
    memset(&brf->meta_tx, 0, sizeof(brf->meta_tx));

    printf("\n[BRF] sampling_rate %u, num_buffers %u,  buffer_size %u, num transfer %u, timeout_ms (rx %u, tx %u)\n",
           brf->sample_rate, brf->num_buffers, brf->buffer_size,brf->num_transfers, brf->rx_timeout_ms, brf->tx_timeout_ms);

    if ((status=bladerf_open(&brf->dev, "")) != 0 ) {
        fprintf(stderr,"Failed to open brf device: %s\n",bladerf_strerror(status));
        brf_error(status);
    }
    printf("[BRF] init dev %p\n", brf->dev);
    switch(bladerf_device_speed(brf->dev)) {
    case BLADERF_DEVICE_SPEED_SUPER:
        printf("[BRF] Device operates at max speed\n");
        break;
    default:
        printf("[BRF] Device does not operates at max speed, change the USB port\n");
        brf_error(BLADERF_ERR_UNSUPPORTED);
    }
    // RX
    // Example of CLI output: RX Frequency: 2539999999Hz


    if ((status=bladerf_set_gain_mode(brf->dev, BLADERF_MODULE_RX, BLADERF_GAIN_MGC))) {
        fprintf(stderr, "[BRF] Failed to disable AGC\n");
        brf_error(status);
    }

    if ((status=bladerf_set_frequency(brf->dev, BLADERF_MODULE_RX, (unsigned int) openair0_cfg->rx_freq[0])) != 0) {
        fprintf(stderr,"Failed to set RX frequency: %s\n",bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set RX frequency to %u\n",(unsigned int)openair0_cfg->rx_freq[0]);



    unsigned int actual_value=0;
    if ((status=bladerf_set_sample_rate(brf->dev, BLADERF_MODULE_RX, (unsigned int) openair0_cfg->sample_rate, &actual_value)) != 0) {
        fprintf(stderr,"Failed to set RX sample rate: %s\n", bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set RX sample rate to %u, %u\n", (unsigned int) openair0_cfg->sample_rate, actual_value);


    if ((status=bladerf_set_bandwidth(brf->dev, BLADERF_MODULE_RX, (unsigned int) openair0_cfg->rx_bw*2, &actual_value)) != 0) {
        fprintf(stderr,"Failed to set RX bandwidth: %s\n", bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set RX bandwidth to %u, %u\n",(unsigned int)openair0_cfg->rx_bw*2, actual_value);

    if ((status=bladerf_set_gain(brf->dev, BLADERF_MODULE_RX, (int) openair0_cfg->rx_gain[0]-openair0_cfg[0].rx_gain_offset[0])) != 0) {
        fprintf(stderr,"Failed to set RX gain: %s\n",bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set RX gain to %d (%d)\n",(int)(openair0_cfg->rx_gain[0]-openair0_cfg[0].rx_gain_offset[0]),(int)openair0_cfg[0].rx_gain_offset[0]);

    // TX

    if ((status=bladerf_set_frequency(brf->dev, BLADERF_MODULE_TX, (unsigned int) openair0_cfg->tx_freq[0])) != 0) {
        fprintf(stderr,"Failed to set TX frequency: %s\n",bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set TX Frequency to %u\n", (unsigned int) openair0_cfg->tx_freq[0]);

    if ((status=bladerf_set_sample_rate(brf->dev, BLADERF_MODULE_TX, (unsigned int) openair0_cfg->sample_rate, NULL)) != 0) {
        fprintf(stderr,"Failed to set TX sample rate: %s\n", bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set TX sampling rate to %u \n", (unsigned int) openair0_cfg->sample_rate);

    if ((status=bladerf_set_bandwidth(brf->dev, BLADERF_MODULE_TX,(unsigned int)openair0_cfg->tx_bw*2, NULL)) != 0) {
        fprintf(stderr, "Failed to set TX bandwidth: %s\n", bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set TX bandwidth to %u \n", (unsigned int) openair0_cfg->tx_bw*2);

    if ((status=bladerf_set_gain(brf->dev, BLADERF_MODULE_TX, (int) openair0_cfg->tx_gain[0])) != 0) {
        fprintf(stderr,"Failed to set TX gain: %s\n",bladerf_strerror(status));
        brf_error(status);
    } else
        printf("[BRF] set the TX gain to %d\n", (int)openair0_cfg->tx_gain[0]);


    /* set log to info, available log levels are:
     * - BLADERF_LOG_LEVEL_VERBOSE
     * - BLADERF_LOG_LEVEL_DEBUG
     * - BLADERF_LOG_LEVEL_INFO
     * - BLADERF_LOG_LEVEL_WARNING
     * - BLADERF_LOG_LEVEL_ERROR
     * - BLADERF_LOG_LEVEL_CRITICAL
     * - BLADERF_LOG_LEVEL_SILENT
     */
    bladerf_log_set_verbosity(BLADERF_LOG_LEVEL_INFO);

    printf("BLADERF: Initializing openair0_device_t\n");
    device->type             = BLADERF_DEV;
    device->trx_start_func = trx_brf_start;
    device->trx_end_func   = trx_brf_end;
    device->trx_read_func  = trx_brf_read;
    device->trx_write_func = trx_brf_write;
    device->trx_get_stats_func   = trx_brf_get_stats;
    device->trx_reset_stats_func = trx_brf_reset_stats;
    device->trx_stop_func        = trx_brf_stop;
    device->trx_set_freq_func    = trx_brf_set_freq;
    device->trx_set_gains_func   = trx_brf_set_gains;
    device->trx_write_init       = trx_brf_write_init;
    device->openair0_cfg = openair0_cfg;
    device->priv = (void *)brf;

    //  memcpy((void*)&device->openair0_cfg,(void*)&openair0_cfg[0],sizeof(openair0_config_t));

    if ((status=bladerf_enable_module(brf->dev, BLADERF_MODULE_TX, false)) != 0) {
        fprintf(stderr,"Failed to enable TX module: %s\n", bladerf_strerror(status));
        abort();
    }
    if ((status=bladerf_enable_module(brf->dev, BLADERF_MODULE_RX, false)) != 0) {
        fprintf(stderr,"Failed to enable RX module: %s\n", bladerf_strerror(status));
        abort();
    }

    return 0;
}


/*! \brief bladeRF error report
 * \param status
 * \returns 0 on success
 */
int brf_error(int status)
{
    fprintf(stderr, "[BRF] brf_error: %s\n", bladerf_strerror(status));
    exit(-1);
    return status; // or status error code
}
/*@}*/
