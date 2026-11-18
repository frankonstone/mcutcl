# The host delivers input edges and restarts this instance's timer.
proc init {delay} {
    set ::settle_ticks $delay
    set ::pending 0
}

proc input_edge {level} {
    set ::pending $level
    restart_timer $::settle_ticks
}

proc timer_elapsed {} {
    drive_pin $::pending
}
