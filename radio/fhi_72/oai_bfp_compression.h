/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 */

#ifndef OAI_BFP_COMPRESSION_H
#define OAI_BFP_COMPRESSION_H

#include "xran_compression.h"
#include <stdint.h>

void oai_bfp_compression(uint32_t iq_width, uint32_t n_prb, const int16_t *src, int8_t *dst);
void oai_bfp_decompression(uint32_t iq_width, uint32_t n_prb, const int8_t *src, int16_t *dst);
void oai_bfp_compression_n(uint32_t iq_width, uint32_t n_blocks, uint32_t num_data_elements, const int16_t *src, int8_t *dst);
void oai_bfp_decompression_n(uint32_t iq_width, uint32_t n_blocks, uint32_t num_data_elements, const int8_t *src, int16_t *dst);

int32_t oai_xranlib_compress_scalar(const struct xranlib_compress_request *request, struct xranlib_compress_response *response);
int32_t oai_xranlib_decompress_scalar(const struct xranlib_decompress_request *request, struct xranlib_decompress_response *response);
int32_t oai_xranlib_compress_bfw_scalar(const struct xranlib_compress_request *request, struct xranlib_compress_response *response);
int32_t oai_xranlib_decompress_bfw_scalar(const struct xranlib_decompress_request *request, struct xranlib_decompress_response *response);

#endif
