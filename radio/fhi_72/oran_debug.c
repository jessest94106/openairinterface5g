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

#include "oran_debug.h"
#include "openair1/PHY/TOOLS/tools_defs.h"

void dump_nonzero_symbol(c16_t *txdataF, uint32_t ofdm_symbol_size, int frame, int slot, int symbol, const char* loc)
{
  float signal_energy = signal_energy_nodc(txdataF, ofdm_symbol_size);
  if (signal_energy > 1) {
    // Prepare a buffer to hold the formatted string for the symbol
    const int num_chars_per_sample = 4 + 6 * 2;
    char symbol_buf[ofdm_symbol_size * num_chars_per_sample]; // Enough for "(r,i) " per sample 
    int offset = 0;
    bool is_zero_block = true;
    for (int i = 0; i < ofdm_symbol_size; i++) {
      bool is_zero = txdataF[i].r == 0 && txdataF[i].i == 0;
      if (is_zero_block && !is_zero) {
        offset += snprintf(symbol_buf + offset, sizeof(symbol_buf) - offset, "[sc %d]: ", i);
        is_zero_block = false;
      }
      if (!is_zero_block && is_zero) {
        is_zero_block = true;
      }
      if (!is_zero) {
        offset += snprintf(symbol_buf + offset, sizeof(symbol_buf) - offset, "(%d,%d) ", txdataF[i].r, txdataF[i].i);
      }
    }
    symbol_buf[offset] = '\0';
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    LOG_D(HW, "dump_nonzero_symbol: Frame.Slot.Symbol %d.%d.%d (%s) signal_energy %.3f time %ld.%09ld samples: %s\n", frame, slot, symbol, loc, 10 * log10(signal_energy), ts.tv_sec, ts.tv_nsec, symbol_buf);
  }
}
