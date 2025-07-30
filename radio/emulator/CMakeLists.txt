add_library(rf_emulator MODULE rf_emulator.c)
set_target_properties(rf_emulator PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(rf_emulator generate_T)
