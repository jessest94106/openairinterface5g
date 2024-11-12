add_subdirectory(5GMM/MSG)
add_subdirectory(5GMM/IES)
add_subdirectory(5GSM/MSG)

if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()
