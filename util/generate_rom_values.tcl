namespace eval ::fractal_utils {
  namespace export -clear generate_rom_values

  proc clamp {x a b} {
    return [expr min(max($x, $a), $b)]
  }

  proc f {x} {
    return [expr int([clamp $x 0.0 1.0] * 255)]
  }

  proc generate_rom_values {} {
    set l [list]
    for {set i 0} {$i <= 255} {incr i} {
      set t [expr {double($i) / 255}]
      set r [f [expr {9.0 * (1.0 - $t) * $t * $t * $t}]]
      set g [f [expr {15.0 * (1.0 - $t) * (1.0 - $t) * $t * $t}]]
      set b [f [expr {8.5 * (1.0 - $t) * (1.0 - $t) * (1.0 - $t) * $t}]]
      lappend l [format %02x%02x%02x $r $b $g]
    }
    return $l
  }
}
