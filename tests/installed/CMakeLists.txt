cmake_minimum_required(VERSION 3.25)
project(mcutcl_installed_consumer LANGUAGES CXX)
enable_testing()
find_package(mcutcl 0.4 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE mcutcl::mcutcl)
add_test(NAME public_api COMMAND consumer)

foreach(example IN ITEMS debounce sampled-actuator)
    add_subdirectory("${EXAMPLES}/${example}" "${example}")
endforeach()
add_test(NAME digital COMMAND ${CMAKE_COMMAND} "-DRUNNER=$<TARGET_FILE:mcutcl_debounce>"
    "-DMODEL=${EXAMPLES}/debounce/debounce.tcl" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/digital"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_debounce.cmake")
add_test(NAME analog COMMAND ${CMAKE_COMMAND} "-DRUNNER=$<TARGET_FILE:mcutcl_actuator>"
    "-DMODEL=${EXAMPLES}/sampled-actuator/actuator.tcl" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/analog"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_actuator.cmake")
