# Each instance retains its own normalized output, gain and drive input.
proc init {gain} {
    set ::state [dict create gain $gain input 0.0 position 0.0]
}

proc drive {input} {
    dict set ::state input $input
}

proc sample {} {
    set position [dict get $::state position]
    set input [dict get $::state input]
    set gain [dict get $::state gain]
    set next [expr {$position + $gain * ($input - $position)}]
    if {$next > 1.0} {
        set next 1.0
    } elseif {$next < 0.0} {
        set next 0.0
    }
    dict set ::state position $next
    publish_adc $next [expr {round(4095 * $next)}]
}
