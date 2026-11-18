function(check_trace model expected)
    execute_process(COMMAND "${RUNNER}" "${model}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE errors)
    string(REPLACE "\r\n" "\n" actual "${actual}")
    if(NOT status STREQUAL "0" OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "Debounce run exited ${status}\nExpected:\n${expected}Observed:\n${actual}${errors}")
    endif()
endfunction()

check_trace("${MODEL}" "10 drive 1\n12 sample 1\n")
check_trace("${MODEL}" "10 drive 1\n12 sample 1\n" 5 10 12 100)

file(MAKE_DIRECTORY "${WORK}")
file(READ "${MODEL}" source)
# A longer debounce interval changes what the host samples, using the same binary.
file(WRITE "${WORK}/slow.tcl" "${source}\nproc init {delay} {set ::settle_ticks 8; set ::pending 0}\n")
check_trace("${WORK}/slow.tcl" "12 sample 0\n14 drive 1\n")

file(WRITE "${WORK}/fault.tcl" "${source}\nproc timer_elapsed {} {error {pin offline}}\n")
execute_process(COMMAND "${RUNNER}" "${WORK}/fault.tcl"
    RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE errors)
if(NOT status STREQUAL "1" OR NOT actual STREQUAL "" OR NOT errors MATCHES "fault.tcl::timer_elapsed:1:1: script: pin offline")
    message(FATAL_ERROR "A model error must stop the run before the tick-12 sample: ${status}\n${actual}${errors}")
endif()
