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
#include <cuda_runtime.h>
#include "oai_cuda.h"

#define CHECK_CUDA(val) checkCuda((val), #val, __FILE__, __LINE__)
static void checkCuda(cudaError_t result, const char *const func, const char *const file, const int line)
{
  if (result != cudaSuccess) {
    fprintf(stderr,
            "CUDA Error at %s:%d code=%d(%s) \"%s\" \n",
            file,
            line,
            static_cast<unsigned int>(result),
            cudaGetErrorString(result),
            func);
    cudaDeviceReset();
    exit(EXIT_FAILURE);
  }
}

__global__ void sum_outputs_kernel(const short2 *__restrict__ *__restrict__ individual_outputs,
                                   short2 *__restrict__ final_summed_output,
                                   int num_channels,
                                   int num_samples_per_antenna)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= num_samples_per_antenna)
    return;

  float2 sum = make_float2(0.0f, 0.0f);

  for (int c = 0; c < num_channels; c++) {
    sum.x += individual_outputs[c][i].x;
    sum.y += individual_outputs[c][i].y;
  }

  final_summed_output[i].x = (short)fmaxf(-32768.0f, fminf(32767.0f, sum.x));
  final_summed_output[i].y = (short)fmaxf(-32768.0f, fminf(32767.0f, sum.y));
}

extern "C" {
void run_channel_pipeline_cuda(c16_t **output_signal,
                               int nb_tx,
                               int nb_rx,
                               int channel_length,
                               uint32_t num_samples,
                               float *h_channel_coeffs,
                               float sigma2,
                               double ts,
                               uint16_t pdu_bit_map,
                               uint16_t ptrs_bit_map,
                               int slot_offset,
                               int delay,
                               void *d_tx_sig_void,
                               void *d_intermediate_sig_void,
                               void *d_final_output_void,
                               void *d_curand_states_void,
                               void *h_tx_sig_pinned_void,
                               void *h_final_output_pinned_void,
                               void *d_channel_coeffs_void)
{
  // --- Cast void pointers ---
  float2 *d_intermediate_sig = (float2 *)d_intermediate_sig_void;
  short2 *d_final_output = (short2 *)d_final_output_void;
  curandState_t *d_curand_states = (curandState_t *)d_curand_states_void;
  float2 *d_channel_coeffs = (float2 *)d_channel_coeffs_void;

  const int padding_len = channel_length - 1;
  const size_t padded_stride_bytes = (num_samples + padding_len) * 2 * sizeof(float);
  const size_t total_padded_tx_bytes = nb_tx * padded_stride_bytes;

  float *kernel_input_ptr;
#if defined(USE_UNIFIED_MEMORY) || defined(USE_ATS_MEMORY)
  kernel_input_ptr = (float *)h_tx_sig_pinned_void;
#else
  float *d_tx_sig = (float *)d_tx_sig_void;
  float *h_tx_sig_pinned = (float *)h_tx_sig_pinned_void;
  CHECK_CUDA(cudaMemcpy(d_tx_sig, h_tx_sig_pinned, total_padded_tx_bytes, cudaMemcpyHostToDevice));

  kernel_input_ptr = d_tx_sig;
#endif

  size_t channel_size_bytes = nb_tx * nb_rx * channel_length * sizeof(float2);
  CHECK_CUDA(cudaMemcpy(d_channel_coeffs, h_channel_coeffs, channel_size_bytes, cudaMemcpyHostToDevice));

  dim3 threads_multipath(512, 1);
  dim3 blocks_multipath((num_samples + threads_multipath.x - 1) / threads_multipath.x, nb_rx);
  size_t sharedMemSize = (threads_multipath.x + channel_length - 1) * sizeof(float2);
  multipath_channel_kernel<<<blocks_multipath, threads_multipath, sharedMemSize>>>(d_channel_coeffs,
                                                                                   kernel_input_ptr,
                                                                                   d_intermediate_sig,
                                                                                   num_samples,
                                                                                   channel_length,
                                                                                   nb_tx,
                                                                                   nb_rx);

  dim3 threads_noise(256, 1);
  dim3 blocks_noise((num_samples + threads_noise.x - 1) / threads_noise.x, nb_rx);
  float pn_variance = 1e-5f * 2.0f * 3.1415926535f * 300.0f * (float)ts;
  bool apply_phase_noise = (pdu_bit_map & ptrs_bit_map);
  add_noise_and_phase_noise_kernel<<<blocks_noise, threads_noise>>>(d_intermediate_sig,
                                                                    d_final_output,
                                                                    d_curand_states,
                                                                    num_samples,
                                                                    sqrtf(sigma2 / 2.0f),
                                                                    sqrtf(pn_variance),
                                                                    apply_phase_noise);

  cudaDeviceSynchronize();

  // If output_signal is NULL, the caller intends to keep the data on the GPU
  // for further processing (e.g., summing outputs). Otherwise, copy back to host.
  if (output_signal != NULL) {
#if defined(USE_UNIFIED_MEMORY)
    short2 *h_final_output_pinned = (short2 *)h_final_output_pinned_void;
    for (int ii = 0; ii < nb_rx; ii++) {
      memcpy(output_signal[ii] + slot_offset + delay, h_final_output_pinned + ii * num_samples, num_samples * sizeof(short2));
    }
#else
    short2 *h_final_output_pinned = (short2 *)h_final_output_pinned_void;
    CHECK_CUDA(cudaMemcpy(h_final_output_pinned, d_final_output, nb_rx * num_samples * sizeof(short2), cudaMemcpyDeviceToHost));
    for (int ii = 0; ii < nb_rx; ii++) {
      memcpy(output_signal[ii] + slot_offset + delay, h_final_output_pinned + ii * num_samples, num_samples * sizeof(short2));
    }
#endif
  }
}

void sum_channel_outputs_cuda(void **d_individual_outputs, void *d_final_output, int num_channels, int nb_rx, int num_samples)
{
  void **d_ptr_array;
  size_t ptr_array_size = num_channels * sizeof(void *);
  CHECK_CUDA(cudaMalloc(&d_ptr_array, ptr_array_size));

  CHECK_CUDA(cudaMemcpy(d_ptr_array, d_individual_outputs, ptr_array_size, cudaMemcpyHostToDevice));

  int num_total_samples = nb_rx * num_samples;
  dim3 threads(256, 1);
  dim3 blocks((num_total_samples + threads.x - 1) / threads.x, 1);

  sum_outputs_kernel<<<blocks, threads>>>((const short2 **)d_ptr_array, (short2 *)d_final_output, num_channels, num_total_samples);

  CHECK_CUDA(cudaFree(d_ptr_array));
}
} // extern "C"
