# AutoMix Engine Tests

# For now, just a placeholder
# Full test suite would use Google Test or Catch2

# Basic API tests
add_executable(test_basic test_basic.cpp)
target_link_libraries(test_basic PRIVATE automix)
add_test(NAME test_basic COMMAND test_basic)

# Phase 2 module tests (Store, Decoder, Analyzer)
add_executable(test_phase2 test_phase2.cpp)
target_include_directories(test_phase2 PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_phase2 PRIVATE automix)
add_test(NAME test_phase2 COMMAND test_phase2)
