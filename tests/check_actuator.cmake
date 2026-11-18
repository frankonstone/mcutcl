function(check_trace model expected)
    execute_process(COMMAND "${RUNNER}" "${model}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE errors)
    string(REPLACE "\r\n" "\n" actual "${actual}")
    if(NOT status STREQUAL "0" OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "Sampled actuator exited ${status}\nExpected:\n${expected}Observed:\n${actual}${errors}")
    endif()
endfunction()

set(normal [=[10 sample A 0.5 2048
10 sample B 0.25 1024
12 read 2048 1024
20 sample A 0.75 3071
20 sample B 0.4375 1792
22 read 3071 1792
30 sample A 1.0 4095
30 sample B 1.0 4095
32 read 4095 4095
]=])
set(reset [=[10 sample A 0.5 2048
10 sample B 0.25 1024
12 read 2048 1024
20 sample B 0.4375 1792
22 read 0 1792
28 sample A 1.0 4095
30 sample B 1.0 4095
32 read 4095 4095
]=])
check_trace("${MODEL}" "${normal}")
check_trace("${MODEL}" "${normal}" 3 10 12 18 21 30 32)
check_trace("${MODEL}" "${reset}" --reset)
check_trace("${MODEL}" "${reset}" --reset 10 18 19 20 22 27 32)
file(MAKE_DIRECTORY "${WORK}")
file(READ "${MODEL}" source)
file(WRITE "${WORK}/fault.tcl" "${source}\nproc sample {} {error {sample failed}}\n")
execute_process(COMMAND "${RUNNER}" "${WORK}/fault.tcl"
    RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE errors)
if(NOT status STREQUAL "1" OR NOT actual STREQUAL "" OR NOT errors MATCHES "fault.tcl::sample:1:1: script: sample failed")
    message(FATAL_ERROR "Fault must reach the host before the first ADC read: ${status}\n${actual}${errors}")
endif()
