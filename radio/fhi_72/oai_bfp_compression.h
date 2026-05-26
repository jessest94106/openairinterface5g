/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 */

#ifndef OAI_BFP_COMPRESSION_H
#define OAI_BFP_COMPRESSION_H

#include <stdint.h>

void oai_bfp_compression(uint32_t iq_width, uint32_t n_prb, const int16_t *src, int8_t *dst);
void oai_bfp_decompression(uint32_t iq_width, uint32_t n_prb, const int8_t *src, int16_t *dst);

#endif
