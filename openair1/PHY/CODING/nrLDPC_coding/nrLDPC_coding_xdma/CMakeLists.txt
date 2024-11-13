##########################################################

# LDPC offload library - XDMA
##########################################################

add_boolean_option(ENABLE_LDPC_XDMA OFF "Build support for LDPC Offload to XDMA library" OFF)

if (ENABLE_LDPC_XDMA)

  set(PHY_NRLDPC_CODING_XDMA_SRC
    ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_coding/nrLDPC_coding_xdma/nrLDPC_coding_xdma_offload.c
    ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_coding/nrLDPC_coding_xdma/nrLDPC_coding_xdma.c
    ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_coding/nrLDPC_coding_segment/nrLDPC_coding_segment_encoder.c
    ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_load.c
  )

  add_library(ldpc_xdma MODULE ${PHY_NRLDPC_CODING_XDMA_SRC})
  set_target_properties(ldpc_xdma PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

  add_dependencies(nr-softmodem ldpc_xdma)
  add_dependencies(nr-uesoftmodem ldpc_xdma)
  add_dependencies(nr_ulsim ldpc_xdma)
  add_dependencies(nr_ulschsim ldpc_xdma)
  add_dependencies(nr_dlsim ldpc_xdma)
  add_dependencies(nr_dlschsim ldpc_xdma)

  if (${T_TRACER})
    add_dependencies(ldpc_xdma generate_T)
  endif(${T_TRACER})

  add_dependencies(ldpc_xdma ldpc_orig ldpc_optim ldpc_optim8seg ldpc_optim8segmulti)
  if (ENABLE_LDPC_CUDA)
    add_dependencies(ldpc_xdma ldpc_cuda)
  endif()

endif()
