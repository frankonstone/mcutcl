function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT result STREQUAL "0")
        if(IS_DIRECTORY "${WORK}/relocated" AND NOT EXISTS "${WORK}/prefix")
            file(RENAME "${WORK}/relocated" "${WORK}/prefix")
        endif()
        message(FATAL_ERROR "Installed consumer failed (${result})\n${output}${errors}")
    endif()
endfunction()

# Copy only the consumer and its expected traces. All target headers, libraries,
# model sources and example projects must come from the installation.
if(IS_DIRECTORY "${WORK}/relocated" AND NOT EXISTS "${WORK}/prefix")
    file(RENAME "${WORK}/relocated" "${WORK}/prefix")
endif()
file(MAKE_DIRECTORY "${WORK}/source")
file(COPY "${TESTS}/installed/" DESTINATION "${WORK}/source")
file(COPY "${TESTS}/check_actuator.cmake" "${TESTS}/check_debounce.cmake" DESTINATION "${WORK}/source")
if(CONFIG)
    set(build_config --config "${CONFIG}")
    set(test_config -C "${CONFIG}")
endif()
run("${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${WORK}/prefix" ${build_config})
# A rename removes the original install path while the external host builds.
file(RENAME "${WORK}/prefix" "${WORK}/relocated" RESULT moved)
if(NOT moved STREQUAL "0")
    message(FATAL_ERROR "Could not relocate the installation: ${moved}")
endif()
run("${CMAKE_COMMAND}" -S "${WORK}/source" -B "${WORK}/consumer" -G "${GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${COMPILER}" "-DCMAKE_BUILD_TYPE=${CONFIG}"
    "-Dmcutcl_DIR=${WORK}/relocated/${LIBDIR}/cmake/mcutcl"
    "-DEXAMPLES=${WORK}/relocated/${DATADIR}/mcutcl/examples")
run("${CMAKE_COMMAND}" --build "${WORK}/consumer" ${build_config})
run("${CMAKE_CTEST_COMMAND}" --test-dir "${WORK}/consumer" ${test_config} --output-on-failure)
file(RENAME "${WORK}/relocated" "${WORK}/prefix")
