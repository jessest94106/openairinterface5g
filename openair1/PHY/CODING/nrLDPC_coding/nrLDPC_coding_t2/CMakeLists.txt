##########################################################
# LDPC offload library - DPDK BBDEV
##########################################################

add_boolean_option(ENABLE_LDPC_T2 OFF "Build support for LDPC Offload to T2 library" OFF)

if (ENABLE_LDPC_T2)
  pkg_check_modules(LIBDPDK_T2 REQUIRED libdpdk>=20.11.9)

  if(LIBDPDK_T2_VERSION VERSION_LESS "21.11")
    add_compile_definitions(DPDK_VER_PRE_21_11)
  endif()

  find_library(PMD_T2 NAMES rte_baseband_accl_ldpc HINTS ${LIBDPDK_T2_LIBRARY_DIRS})
  if (PMD_T2)
    add_compile_definitions(LDPC_T2)
    message(STATUS "found poll-mode driver for AccelerComm T2 LDPC Offload (rte_baseband_accl_ldpc.so)")
  else()
    if (LIBDPDK_T2_VERSION VERSION_LESS "22.11")
      message(FATAL_ERROR "for non-T2 BBDEV PMDs, we support the Intel ACC100 and ACC200 (VRB1), using DPDK 22.11 or later.")
    endif()
  endif()

  add_library(ldpc_t2 MODULE nrLDPC_coding_t2.c)

  set_target_properties(ldpc_t2 PROPERTIES COMPILE_FLAGS "-DALLOW_EXPERIMENTAL_API")
  set_target_properties(ldpc_t2 PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  target_include_directories(ldpc_t2 PRIVATE ${LIBDPDK_T2_INCLUDE_DIRS})
  target_link_libraries(ldpc_t2 PRIVATE ldpc_gen_HEADERS ${LIBDPDK_T2_LDFLAGS})
  if (PMD_T2)
    target_link_libraries(ldpc_t2 PRIVATE ${PMD_T2})
  endif()

  add_dependencies(nr-softmodem ldpc_t2)
  add_dependencies(nr-uesoftmodem ldpc_t2)
  add_dependencies(nr_ulsim ldpc_t2)
  add_dependencies(nr_ulschsim ldpc_t2)
  add_dependencies(nr_dlsim ldpc_t2)
  add_dependencies(nr_dlschsim ldpc_t2)

endif()
