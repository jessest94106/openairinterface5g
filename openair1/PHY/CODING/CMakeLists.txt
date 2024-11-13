set(PHY_TURBOSRC
  ${OPENAIR1_DIR}/PHY/CODING/3gpplte_sse.c
  ${OPENAIR1_DIR}/PHY/CODING/3gpplte.c
  ${OPENAIR1_DIR}/PHY/CODING/3gpplte_turbo_decoder_sse_8bit.c
  ${OPENAIR1_DIR}/PHY/CODING/3gpplte_turbo_decoder_sse_16bit.c
  ${OPENAIR1_DIR}/PHY/CODING/3gpplte_turbo_decoder_avx2_16bit.c
  ${OPENAIR1_DIR}/PHY/CODING/3gpplte_turbo_decoder.c
)

add_library(coding MODULE ${PHY_TURBOSRC} )
set_target_properties(coding PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(PHY_LDPC_ORIG_SRC
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_decoder/nrLDPC_decoder.c
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_encoder/ldpc_encoder.c
)
add_library(ldpc_orig MODULE ${PHY_LDPC_ORIG_SRC} )
set_target_properties(ldpc_orig PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(ldpc_orig PRIVATE ldpc_gen_HEADERS)

set(PHY_LDPC_OPTIM_SRC
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_decoder/nrLDPC_decoder.c
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_encoder/ldpc_encoder_optim.c
  )
add_library(ldpc_optim MODULE ${PHY_LDPC_OPTIM_SRC} )
set_target_properties(ldpc_optim PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(ldpc_optim PRIVATE ldpc_gen_HEADERS)

set(PHY_LDPC_OPTIM8SEG_SRC
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_decoder/nrLDPC_decoder.c
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_encoder/ldpc_encoder_optim8seg.c
)
add_library(ldpc_optim8seg MODULE ${PHY_LDPC_OPTIM8SEG_SRC} )
set_target_properties(ldpc_optim8seg PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(ldpc_optim8seg PRIVATE ldpc_gen_HEADERS)

set(PHY_LDPC_OPTIM8SEGMULTI_SRC
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_decoder/nrLDPC_decoder.c
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_encoder/ldpc_encoder_optim8segmulti.c
  )
add_library(ldpc_optim8segmulti MODULE ${PHY_LDPC_OPTIM8SEGMULTI_SRC} )
set_target_properties(ldpc_optim8segmulti PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(ldpc_optim8segmulti PRIVATE ldpc_gen_HEADERS)

set(PHY_LDPC_CUDA_SRC
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_decoder_LYC/nrLDPC_decoder_LYC.cu
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_encoder/ldpc_encoder_optim8segmulti.c
)

set(PHY_LDPC_CL_SRC
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_decoder/nrLDPC_decoder_CL.c
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_encoder/ldpc_encoder_optim8segmulti.c
)
add_custom_target( nrLDPC_decoder_kernels_CL
                   COMMAND gcc ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_decoder/nrLDPC_decoder_CL.c -dD -DNRLDPC_KERNEL_SOURCE -E -o ${CMAKE_CURRENT_BINARY_DIR}/nrLDPC_decoder_kernels_CL.clc
                   SOURCES ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_decoder/nrLDPC_decoder_CL.c
)
add_library(ldpc_cl MODULE ${PHY_LDPC_CL_SRC} )
set_target_properties(ldpc_cl PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(ldpc_cl OpenCL)
add_dependencies(ldpc_cl nrLDPC_decoder_kernels_CL)

##############################################
# Base CUDA setting
##############################################

add_boolean_option(ENABLE_LDPC_CUDA OFF "Build support for CUDA" OFF)
if (ENABLE_LDPC_CUDA)
  find_package(CUDA REQUIRED)
  SET(CUDA_NVCC_FLAG "${CUDA_NVCC_FLAGS};-arch=sm_60;")
  SET(CUDA_VERBOSE_BUILD ON)
  cuda_add_library(ldpc_cuda MODULE ${PHY_LDPC_CUDA_SRC})
  set_target_properties(ldpc_cuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
  set_target_properties(ldpc_cuda PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  if (NOT CUDA_FOUND)
    message(FATAL_ERROR "no CUDA found")
  endif()
endif()

add_dependencies(ldpctest ldpc_orig ldpc_optim ldpc_optim8seg ldpc_optim8segmulti)
if (ENABLE_LDPC_CUDA)
  add_dependencies(ldpctest ldpc_cuda)
endif()

#ensure that the T header files are generated before targets depending on them
if (${T_TRACER})
  foreach(i coding ldpc_orig ldpc_optim ldpc_optim8seg ldpc_optim8segmulti ldpc_cl ldpc_cuda)
    if (TARGET ${i})
      add_dependencies(${i} generate_T)
    endif()
  endforeach(i)
endif (${T_TRACER})

add_subdirectory(nrLDPC_coding)
