/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 */

#include "oai_bfp_compression.h"

#include "assertions.h"
#include "xran_fh_o_du.h"
#include <limits.h>
#include <string.h>

#define OAI_BFP_UPLANE_ELEMENTS 24

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

static uint32_t bfp_payload_bytes(uint32_t iq_width, uint32_t num_data_elements)
{
  return (num_data_elements * iq_width + 7) >> 3;
}

static uint32_t bfp_block_bytes(uint32_t iq_width, uint32_t num_data_elements)
{
  return 1 + bfp_payload_bytes(iq_width, num_data_elements);
}

static int valid_iq_width(uint32_t iq_width)
{
  return iq_width == 8 || iq_width == 9 || iq_width == 10 || iq_width == 12 || iq_width == 14 || iq_width == 16;
}

void oai_bfp_compression_n(uint32_t iq_width, uint32_t n_blocks, uint32_t num_data_elements, const int16_t *src, int8_t *dst)
{
  AssertFatal(valid_iq_width(iq_width), "Unsupported IQ width for BFP compression: %u\n", iq_width);
  AssertFatal(num_data_elements > 0, "BFP compression needs at least one data element\n");

  const uint32_t block_bytes = bfp_block_bytes(iq_width, num_data_elements);
  const uint32_t payload_bytes = block_bytes - 1;
  const uint32_t iq_mask = (1U << iq_width) - 1U;

  for (uint32_t block = 0; block < n_blocks; block++) {
    const int16_t *block_src = src + block * num_data_elements;
    uint8_t *block_dst = (uint8_t *)dst + block * block_bytes;
    memset(block_dst, 0, block_bytes);

    uint16_t max_abs = 0;
    for (uint32_t re = 0; re < num_data_elements; re++) {
      const uint16_t abs_value = (uint16_t)saturating_abs16(block_src[re]);
      if (abs_value > max_abs)
        max_abs = abs_value;
    }

    const int exponent = 16 - (int)iq_width + 1 - leading_zero_count16(max_abs);
    block_dst[0] = exponent > 0 ? (uint8_t)exponent : 0;

    uint32_t out_idx = 1;
    uint32_t bits_in_byte = 0;
    uint8_t out_byte = 0;
    for (uint32_t re = 0; re < num_data_elements; re++) {
      const uint32_t packed = ((uint32_t)(block_src[re] >> block_dst[0])) & iq_mask;
      for (int bit = (int)iq_width - 1; bit >= 0; bit--) {
        out_byte = (uint8_t)((out_byte << 1) | ((packed >> bit) & 1U));
        bits_in_byte++;
        if (bits_in_byte == 8) {
          AssertFatal(out_idx <= payload_bytes, "BFP compression overflow for iq_width %u elements %u\n", iq_width, num_data_elements);
          block_dst[out_idx++] = out_byte;
          out_byte = 0;
          bits_in_byte = 0;
        }
      }
    }
    if (bits_in_byte != 0) {
      AssertFatal(out_idx <= payload_bytes, "BFP compression final-byte overflow for iq_width %u elements %u\n", iq_width, num_data_elements);
      block_dst[out_idx] = (uint8_t)(out_byte << (8 - bits_in_byte));
    }
  }
}

void oai_bfp_decompression_n(uint32_t iq_width, uint32_t n_blocks, uint32_t num_data_elements, const int8_t *src, int16_t *dst)
{
  AssertFatal(valid_iq_width(iq_width), "Unsupported IQ width for BFP decompression: %u\n", iq_width);
  AssertFatal(num_data_elements > 0, "BFP decompression needs at least one data element\n");

  const uint32_t block_bytes = bfp_block_bytes(iq_width, num_data_elements);
  const uint32_t payload_bytes = block_bytes - 1;
  const uint32_t sign_bit = 1U << (iq_width - 1);
  const uint32_t sign_extend = 1U << iq_width;

  for (uint32_t block = 0; block < n_blocks; block++) {
    const uint8_t *block_src = (const uint8_t *)src + block * block_bytes;
    int16_t *block_dst = dst + block * num_data_elements;
    const uint8_t exponent = block_src[0];

    uint32_t in_idx = 1;
    uint32_t bits_left = 8;
    uint8_t in_byte = payload_bytes > 0 ? block_src[in_idx++] : 0;

    for (uint32_t re = 0; re < num_data_elements; re++) {
      uint32_t packed = 0;
      for (uint32_t bit = 0; bit < iq_width; bit++) {
        packed = (packed << 1) | ((in_byte >> (bits_left - 1)) & 1U);
        bits_left--;
        if (bits_left == 0 && (re != num_data_elements - 1 || bit != iq_width - 1)) {
          in_byte = in_idx <= payload_bytes ? block_src[in_idx++] : 0;
          bits_left = 8;
        }
      }

      const int32_t sample = (packed & sign_bit) ? (int32_t)(packed - sign_extend) : (int32_t)packed;
      block_dst[re] = (int16_t)(sample << exponent);
    }
  }
}

void oai_bfp_compression(uint32_t iq_width, uint32_t n_prb, const int16_t *src, int8_t *dst)
{
  oai_bfp_compression_n(iq_width, n_prb, OAI_BFP_UPLANE_ELEMENTS, src, dst);
}

void oai_bfp_decompression(uint32_t iq_width, uint32_t n_prb, const int8_t *src, int16_t *dst)
{
  oai_bfp_decompression_n(iq_width, n_prb, OAI_BFP_UPLANE_ELEMENTS, src, dst);
}

static int modulation_bits_per_symbol(int16_t iq_width)
{
  return iq_width * 2;
}

static int modulation_output_len(uint32_t num_symbols, int bits_per_symbol)
{
  return (num_symbols * (uint32_t)bits_per_symbol + 7) >> 3;
}

static int16_t positive_unit(int16_t unit, int shift)
{
  int16_t value = unit >> shift;
  return value == 0 ? 1 : value;
}

static int scalar_mod_compress(int16_t *data_in, int8_t *data_out, int16_t unit, int bits_per_symbol, int32_t num_symbols)
{
  memset(data_out, 0, modulation_output_len(num_symbols, bits_per_symbol));

  switch (bits_per_symbol) {
    case 2:
      for (int32_t i = 0; i < num_symbols; i++) {
        uint8_t bit_pos = i & 0x3;
        uint8_t bit_i = data_in[i * 2] >= 0 ? 0 : 1;
        uint8_t bit_q = data_in[i * 2 + 1] >= 0 ? 0 : 1;
        data_out[i >> 2] |= (int8_t)((bit_i << (7 - bit_pos * 2)) | (bit_q << (6 - bit_pos * 2)));
      }
      return 0;
    case 4: {
      const int16_t bit_unit = positive_unit(unit, 1);
      for (int32_t i = 0; i < num_symbols; i++) {
        uint8_t bit_pos = i & 0x1;
        int8_t bit_i = data_in[i * 2] / bit_unit;
        int8_t bit_q = data_in[i * 2 + 1] / bit_unit;
        if (data_in[i * 2] < 0)
          bit_i = 3 + bit_i;
        if (data_in[i * 2 + 1] < 0)
          bit_q = 3 + bit_q;
        data_out[i >> 1] |= (int8_t)((bit_i << (6 - bit_pos * 4)) | (bit_q << (4 - bit_pos * 4)));
      }
      return 0;
    }
    case 6: {
      const int16_t bit_unit = positive_unit(unit, 2);
      for (int32_t i = 0; i < num_symbols; i++) {
        int32_t bit_pos = i & 0x3;
        int8_t bit_i = data_in[i * 2] / bit_unit;
        int8_t bit_q = data_in[i * 2 + 1] / bit_unit;
        if (data_in[i * 2] < 0)
          bit_i = 7 + bit_i;
        if (data_in[i * 2 + 1] < 0)
          bit_q = 7 + bit_q;
        int8_t *out = &data_out[(i / 4) * 3];
        if (bit_pos == 0) {
          out[0] |= (int8_t)((bit_i << 5) | (bit_q << 2));
        } else if (bit_pos == 1) {
          out[0] |= (int8_t)(bit_i >> 1);
          out[1] |= (int8_t)((bit_i << 7) | (bit_q << 4));
        } else if (bit_pos == 2) {
          out[1] |= (int8_t)((bit_i << 1) | (bit_q >> 2));
          out[2] |= (int8_t)(bit_q << 6);
        } else {
          out[2] |= (int8_t)((bit_i << 3) | bit_q);
        }
      }
      return 0;
    }
    case 8: {
      const int16_t bit_unit = positive_unit(unit, 3);
      for (int32_t i = 0; i < num_symbols; i++) {
        int8_t bit_i = data_in[i * 2] / bit_unit;
        int8_t bit_q = data_in[i * 2 + 1] / bit_unit;
        if (data_in[i * 2] < 0)
          bit_i = 15 + bit_i;
        if (data_in[i * 2 + 1] < 0)
          bit_q = 15 + bit_q;
        data_out[i] = (int8_t)((bit_i << 4) | bit_q);
      }
      return 0;
    }
    default:
      return -1;
  }
}

static int scalar_mod_decompress(int8_t *data_in, int16_t *data_out, int16_t unit, int bits_per_symbol, int32_t num_symbols, int16_t re_mask)
{
  switch (bits_per_symbol) {
    case 2: {
      int16_t symbol_unit[2] = {positive_unit(unit, 1), (int16_t)-positive_unit(unit, 1)};
      for (int32_t i = 0; i < num_symbols; i++) {
        uint8_t mask_pos = i % 12;
        if (((re_mask >> mask_pos) & 0x1) == 0)
          continue;
        uint8_t symbol_pos = i & 0x3;
        uint32_t byte_pos = i >> 2;
        uint8_t bit_i = ((uint8_t)data_in[byte_pos] >> (7 - symbol_pos * 2)) & 0x1;
        uint8_t bit_q = ((uint8_t)data_in[byte_pos] >> (6 - symbol_pos * 2)) & 0x1;
        data_out[i * 2] = symbol_unit[bit_i];
        data_out[i * 2 + 1] = symbol_unit[bit_q];
      }
      return 0;
    }
    case 4: {
      int16_t step = positive_unit(unit, 2);
      int16_t symbol_unit[4] = {step, (int16_t)(step * 3), (int16_t)(step * -3), (int16_t)(step * -1)};
      for (int32_t i = 0; i < num_symbols; i++) {
        uint8_t symbol_pos = i & 0x1;
        uint32_t byte_pos = i >> 1;
        uint8_t bit_i = ((uint8_t)data_in[byte_pos] >> (6 - symbol_pos * 4)) & 0x3;
        uint8_t bit_q = ((uint8_t)data_in[byte_pos] >> (4 - symbol_pos * 4)) & 0x3;
        data_out[i * 2] = symbol_unit[bit_i];
        data_out[i * 2 + 1] = symbol_unit[bit_q];
      }
      return 0;
    }
    case 6: {
      int16_t step = positive_unit(unit, 3);
      int16_t symbol_unit[8] = {step, (int16_t)(step * 3), (int16_t)(step * 5), (int16_t)(step * 7),
                                (int16_t)(step * -7), (int16_t)(step * -5), (int16_t)(step * -3), (int16_t)(step * -1)};
      for (int32_t i = 0; i < num_symbols; i++) {
        uint8_t symbol_pos = i & 0x3;
        const uint8_t *in = (const uint8_t *)&data_in[(i / 4) * 3];
        uint8_t bit_i = 0;
        uint8_t bit_q = 0;
        if (symbol_pos == 0) {
          bit_i = (in[0] >> 5) & 0x7;
          bit_q = (in[0] >> 2) & 0x7;
        } else if (symbol_pos == 1) {
          bit_i = ((in[0] & 0x3) << 1) | ((in[1] >> 7) & 0x1);
          bit_q = (in[1] >> 4) & 0x7;
        } else if (symbol_pos == 2) {
          bit_i = (in[1] >> 1) & 0x7;
          bit_q = ((in[1] & 0x1) << 2) | ((in[2] >> 6) & 0x3);
        } else {
          bit_i = (in[2] >> 3) & 0x7;
          bit_q = in[2] & 0x7;
        }
        data_out[i * 2] = symbol_unit[bit_i];
        data_out[i * 2 + 1] = symbol_unit[bit_q];
      }
      return 0;
    }
    case 8: {
      int16_t step = positive_unit(unit, 4);
      int16_t symbol_unit[16] = {step, (int16_t)(step * 3), (int16_t)(step * 5), (int16_t)(step * 7),
                                 (int16_t)(step * 9), (int16_t)(step * 11), (int16_t)(step * 13), (int16_t)(step * 15),
                                 (int16_t)(step * -15), (int16_t)(step * -13), (int16_t)(step * -11), (int16_t)(step * -9),
                                 (int16_t)(step * -7), (int16_t)(step * -5), (int16_t)(step * -3), (int16_t)(step * -1)};
      for (int32_t i = 0; i < num_symbols; i++) {
        uint8_t byte = (uint8_t)data_in[i];
        data_out[i * 2] = symbol_unit[(byte >> 4) & 0xF];
        data_out[i * 2 + 1] = symbol_unit[byte & 0xF];
      }
      return 0;
    }
    default:
      return -1;
  }
}

int32_t oai_xranlib_compress_scalar(const struct xranlib_compress_request *request, struct xranlib_compress_response *response)
{
  if (request == NULL || response == NULL || request->data_in == NULL || response->data_out == NULL)
    return -1;

  if (request->compMethod == XRAN_COMPMETHOD_NONE) {
    const int32_t len = request->numRBs * request->numDataElements * (int32_t)sizeof(int16_t);
    memcpy(response->data_out, request->data_in, len);
    response->len = len;
    return 0;
  }

  if (request->compMethod == XRAN_COMPMETHOD_MODULATION) {
    const int bits_per_symbol = modulation_bits_per_symbol(request->iqWidth);
    const int32_t num_symbols = request->numRBs * 12;
    response->len = modulation_output_len(num_symbols, bits_per_symbol);
    return scalar_mod_compress(request->data_in, response->data_out, request->ScaleFactor, bits_per_symbol, num_symbols);
  }

  if (request->compMethod != XRAN_COMPMETHOD_BLKFLOAT)
    return -1;

  const uint32_t elements = request->numDataElements > 0 ? (uint32_t)request->numDataElements : OAI_BFP_UPLANE_ELEMENTS;
  oai_bfp_compression_n(request->iqWidth, request->numRBs, elements, request->data_in, response->data_out);
  response->len = request->numRBs * (int32_t)bfp_block_bytes(request->iqWidth, elements);
  return 0;
}

int32_t oai_xranlib_decompress_scalar(const struct xranlib_decompress_request *request, struct xranlib_decompress_response *response)
{
  if (request == NULL || response == NULL || request->data_in == NULL || response->data_out == NULL)
    return -1;

  if (request->compMethod == XRAN_COMPMETHOD_NONE) {
    const int32_t len = request->numRBs * request->numDataElements * (int32_t)sizeof(int16_t);
    memcpy(response->data_out, request->data_in, len);
    response->len = len;
    return 0;
  }

  if (request->compMethod == XRAN_COMPMETHOD_MODULATION) {
    const int bits_per_symbol = modulation_bits_per_symbol(request->iqWidth);
    const int32_t num_symbols = request->numRBs * 12;
    response->len = num_symbols * 2 * (int32_t)sizeof(int16_t);
    return scalar_mod_decompress(request->data_in, response->data_out, request->ScaleFactor, bits_per_symbol, num_symbols, request->reMask);
  }

  if (request->compMethod != XRAN_COMPMETHOD_BLKFLOAT)
    return -1;

  const uint32_t elements = request->numDataElements > 0 ? (uint32_t)request->numDataElements : OAI_BFP_UPLANE_ELEMENTS;
  oai_bfp_decompression_n(request->iqWidth, request->numRBs, elements, request->data_in, response->data_out);
  response->len = request->numRBs * elements * (int32_t)sizeof(int16_t);
  return 0;
}

static int valid_bfw_elements(int16_t num_data_elements)
{
  return num_data_elements == 16 || num_data_elements == 32 || num_data_elements == 64 || num_data_elements == 128;
}

int32_t oai_xranlib_compress_bfw_scalar(const struct xranlib_compress_request *request, struct xranlib_compress_response *response)
{
  if (request == NULL || request->numRBs != 1 || !valid_bfw_elements(request->numDataElements))
    return -1;
  return oai_xranlib_compress_scalar(request, response);
}

int32_t oai_xranlib_decompress_bfw_scalar(const struct xranlib_decompress_request *request, struct xranlib_decompress_response *response)
{
  if (request == NULL || request->numRBs != 1 || !valid_bfw_elements(request->numDataElements))
    return -1;
  return oai_xranlib_decompress_scalar(request, response);
}
