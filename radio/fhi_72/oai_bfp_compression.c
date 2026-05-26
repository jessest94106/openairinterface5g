/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 */

#include "oai_bfp_compression.h"

#include "assertions.h"
#include <limits.h>

static int leading_zero_count16(uint16_t value)
{
  if (value == 0)
    return 16;

  int count = 0;
  for (uint16_t bit = 0x8000; (value & bit) == 0; bit >>= 1)
    count++;
  return count;
}

static int16_t saturating_abs16(int16_t value)
{
  return value == INT16_MIN ? INT16_MAX : (int16_t)(value < 0 ? -value : value);
}

void oai_bfp_compression(uint32_t iq_width, uint32_t n_prb, const int16_t *src, int8_t *dst)
{
  AssertFatal(iq_width >= 1 && iq_width <= 16, "Unsupported IQ width for BFP compression: %u\n", iq_width);

  const uint32_t samples_per_prb = 24;
  const uint32_t payload_bytes_per_prb = 3 * iq_width;
  const uint32_t iq_mask = (1U << iq_width) - 1U;

  for (uint32_t rb = 0; rb < n_prb; rb++) {
    const int16_t *rb_src = src + rb * samples_per_prb;
    uint8_t *rb_dst = (uint8_t *)dst + rb * (payload_bytes_per_prb + 1);

    uint16_t max_abs = 0;
    for (uint32_t re = 0; re < samples_per_prb; re++) {
      const uint16_t abs_value = (uint16_t)saturating_abs16(rb_src[re]);
      if (abs_value > max_abs)
        max_abs = abs_value;
    }

    const int exponent = 16 - (int)iq_width + 1 - leading_zero_count16(max_abs);
    rb_dst[0] = exponent > 0 ? (uint8_t)exponent : 0;

    uint8_t out_byte = 0;
    uint32_t bits_in_byte = 0;
    uint32_t out_idx = 1;
    for (uint32_t re = 0; re < samples_per_prb; re++) {
      const uint32_t packed = ((uint32_t)(rb_src[re] >> rb_dst[0])) & iq_mask;
      for (int bit = (int)iq_width - 1; bit >= 0; bit--) {
        out_byte = (uint8_t)((out_byte << 1) | ((packed >> bit) & 1U));
        bits_in_byte++;
        if (bits_in_byte == 8) {
          rb_dst[out_idx++] = out_byte;
          out_byte = 0;
          bits_in_byte = 0;
        }
      }
    }
  }
}

void oai_bfp_decompression(uint32_t iq_width, uint32_t n_prb, const int8_t *src, int16_t *dst)
{
  AssertFatal(iq_width >= 1 && iq_width <= 16, "Unsupported IQ width for BFP decompression: %u\n", iq_width);

  const uint32_t samples_per_prb = 24;
  const uint32_t payload_bytes_per_prb = 3 * iq_width;
  const uint32_t sign_bit = 1U << (iq_width - 1);
  const uint32_t sign_extend = 1U << iq_width;

  for (uint32_t rb = 0; rb < n_prb; rb++) {
    const uint8_t *rb_src = (const uint8_t *)src + rb * (payload_bytes_per_prb + 1);
    int16_t *rb_dst = dst + rb * samples_per_prb;
    const uint8_t exponent = rb_src[0];

    uint32_t in_idx = 1;
    uint32_t bits_left = 8;
    uint8_t in_byte = rb_src[in_idx++];

    for (uint32_t re = 0; re < samples_per_prb; re++) {
      uint32_t packed = 0;
      for (uint32_t bit = 0; bit < iq_width; bit++) {
        packed = (packed << 1) | ((in_byte >> (bits_left - 1)) & 1U);
        bits_left--;
        if (bits_left == 0 && (re != samples_per_prb - 1 || bit != iq_width - 1)) {
          in_byte = rb_src[in_idx++];
          bits_left = 8;
        }
      }

      int32_t sample = (packed & sign_bit) ? (int32_t)(packed - sign_extend) : (int32_t)packed;
      rb_dst[re] = (int16_t)(sample << exponent);
    }
  }
}
