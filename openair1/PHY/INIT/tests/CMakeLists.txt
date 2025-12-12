add_executable(test_nr_frame_params test_nr_frame_params.cpp)
target_link_libraries(test_nr_frame_params PRIVATE LOG minimal_lib PHY_NR -lm GTest::gtest)
