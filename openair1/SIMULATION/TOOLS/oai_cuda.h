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

#ifndef __OAI_CUDA_H__
#define __OAI_CUDA_H__

#include <stdint.h>


#ifdef __NVCC__
#else
    #include "PHY/TOOLS/tools_defs.h"
#endif

#ifdef __NVCC__
    #include <curand_kernel.h>
    // Kernels go here.
#endif // __NVCC__


#ifdef __cplusplus
extern "C" {
#endif

    // Cuda function declarations will go here.
    
#ifdef __cplusplus
}
#endif

#endif // __OAI_CUDA_H__
