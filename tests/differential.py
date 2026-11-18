"""Development oracle only: the shipped interpreter does not use Tcl/Python."""
import argparse
import itertools
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--mcutcl", required=True)
parser.add_argument("--tclsh", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()
cases = [
    "", "# comment", "set x one;set y two;set x", "set x {}", 'set x ""',
    "set x [set y nested]", "set x [set a 1;set b 2]", "set x [# comment\n]",
    "set x [set y {]}]", 'set x [set y "]"]',
    "set x [set y a][set y b][set y c]", "set x {one two};set y $x",
    "set x {; [missing] $missing};set y $x", "set {} empty;set x ${}",
    "set {two words} value;set x ${two words}", "::set ::x root;set y $::x",
    "set {名前} {Grüße 🚀};set x ${名前}",
    "# continued\\\n set wrong wrong\nset x right", "set\\\n x\\\n value",
    'set {x}\\\n "value"', 'set "x"\\\n {value}', "set x {*}\\\n",
    'set x "a"b', "set x {a}b", 'set x "unterminated', "set x {unterminated",
    "set x [set y incomplete", "set x ${missing", "set x $missing", "unknown",
    "set", "set x a b", "set x $", "set x a\\\n b", "set x ]", "set x a{b",
]
for literal in ["plain", "two words", "#literal", "a;b", "a[b]", "$x", "a{b}c", 'a"b', "", "é"]:
    cases.append("set x {" + literal + "};set y $x")
for escape in [r"\a", r"\b", r"\f", r"\n", r"\r", r"\t", r"\v", r"\$", r"\[", r"\\", r"\q", r"\ "]:
    cases.append('set x "a' + escape + 'b"')
for left, right in itertools.product(["a", "{}", '""', "{two words}", "[set y nested]"], repeat=2):
    cases.append("set x " + left + ";set y " + right + ";set z $x$y")
cases.extend([
    "proc p {} {}; p", "proc p {x} {set x}; p {two words}",
    "proc ::p {x} {return $x}; ::p {[unchanged];$missing}",
    "set x global; proc p {x} {set x local}; p value; set x",
    "proc p {v} {set ::pin $v}; p high; p low; set ::pin",
    "proc p {x x} {return $x}; p first second",
    "proc inner {v} {return $v}; proc outer {v} {inner $v}; outer 21",
    "proc p {} {set wrong [return early]; error later}; p",
    "proc p {} {return first}; proc p {} {return second}; p",
    "proc p {x} {return $x}; catch p", "proc p {} {break}; catch p",
    "proc p {} {continue}; catch p", "catch {return data} r; set r",
    "catch {error {pin fault}} r; set r", "catch {set x ok} r; set r",
    "list [catch {}] [catch {error x}] [catch {return x}] [catch break] [catch continue]",
    "catch {catch {error fault}}", "catch {set x [return z]} r; set r",
    "proc p {} {return}; p", "proc p {} {return -foo}; p",
    "proc p {} {set x 1; unset x; catch {set x}}; p",
    "set x 1; unset ::x; catch {set x}", "unset -nocomplain absent",
    "set -nocomplain yes; unset -nocomplain -nocomplain; catch {set -nocomplain}",
    "set -- yes; unset -- --; catch {set --}",
    "set x 1; catch {unset x missing}; catch {set x}",
    "proc p {} {set x changed}; p extra", "proc p {} {error message}; p",
])
expressions = [
    "1+2*3", "(1+2)*3", "-2**2", "2**3**2", "2**-1", "(-1)**-3",
    "0**0", "0**-1", "2.0**-3", "1.0/0.0", "-1.0/0.0", "0.0/0.0",
    "sqrt(-1)", "exp(1000)", "NaN", "NaN == 1", "NaN != 0", "Inf > 0",
    "0.0/-2", "-0.0", "1e-5", "0.0001", "1e16", "1e17", "1e-300",
    "0xffffffffffffffff & (1 << 63)", "0xffffffffffffffff ^ (1 << 63)",
    "wide(0xffffffffffffffff)", "wide(0x8000000000000000)", "wide(-1.9)",
    "round(-2.5)", "round(2.5)", "floor(-1.1)", "ceil(-1.1)", "abs(-4.0)",
    "abs(-9223372036854775808)", "double(9007199254740993)", "sqrt(4)", "exp(0)",
    "min(1,1.0)", "max(1.0,1)", "max(1.0,2)", "min(-0.0,0)",
    "min(9007199254740993,9007199254740992.0)", "max(0xffffffffffffffff,1e18)",
    "min()", "sqrt(1,2)", "1 % 0", "5.0 % 2", "1 << -1", "-2 >> 120",
    "010", "0d12", "0o20", "0b1011", "1__000", "0xA_F", "0.1_2e+0_1",
    "01eq1", "0eqfalse", "2gttrue", "1eq0x1", '0x10 eq "16"', '"024" == 24', '{two words} eq "two words"',
    '"z" > "a"', '"é" gt "a"', "true != 1", "true+1", "!OFF", "t && y", '" yes " && 1',
    "0 && [error unused]", "1 || $absent", "1 ? 0 ? 1 : 2 : [error bad]",
    "0 ? bogus(1) : 3", "0 && (1 / 0)", '1 ? "ok" : [set wrong yes]',
    "1 +", "3 + (2", "0 ? 4", "1 &&", "0 ? 1 : ()", "bogus", "1}extra{",
]
for op in ["+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", "<", ">", "==", "!=", "<=", ">="]:
    for a, b in [(13, 3), (-13, 3), (13, -3), (-13, -3), (0, 0)]:
        expressions.append(f"{a} {op} {b}")
for a, b in [("9007199254740993", "9007199254740992.0"),
             ("18446744073709551615", "18446744073709551616.0"),
             ("-9007199254740993", "-9007199254740992.0"), ("-1", "-1.1"), ("0", "-0.0")]:
    for op in ["==", "!=", "<", "<=", ">", ">="]:
        expressions.extend([f"{a} {op} {b}", f"{b} {op} {a}"])
cases.extend("expr {" + expr + "}" for expr in expressions)
cases.extend([
    'set x 3; expr {"hi $x" eq "hi [set x]"}', 'set x {1 + 2}; expr {$x}',
    "set x 2; expr {$x?[set x 3]:[error bad]}", "set flag 0; catch {expr {[set flag 1] + }}; set flag",
    "catch {expr {1 + [return hi]}} v; set v", "catch {expr {[break]}}",
    "catch {while {[break]} {error body}}", "if {0} {error nope}",
    "if 0 then {error bad} elseif {1} then {set x yes} else {error later}",
    "if 1 {set x 7} elseif {$missing} {error bad}", "if 0 a {set ok yes}",
    "if 1 {} else", "if 1 {} bad a b", "if true {} elseif",
    "foreach x {} {error wrong}", "set v old; foreach v {} {}; set v",
    "set out {}; foreach a {1 2 3 4} {if {$a == 2} continue; if {$a == 4} break; lappend out $a}; set out",
    "set n 3; while {$n > 0} {set n [expr {$n-1}]}; set n",
    "proc f {} {foreach v {1 2 3} {if {$v>1} {return $v}}}; f",
    "proc f {} {while true {return x}}; f", "foreach x {2 3} {break}; set x",
    "set a {one  two}; lappend a", "lappend a", "lappend a 1 2; lappend a 3",
    "set a {a b}; lset a 2 c", "set a {a b}; lset a 4 c", "lset missing new",
    "set a {a b}; lset a new", "set a {a b}; lset a {} new",
    "set a {{a b} {c d}}; lset a 1 0 changed; lindex [lindex $a 1] 0",
    "set a {}; lset a {0 0} {one two}; lindex [lindex $a 0] 0",
    "set a {}; catch {lset a 2 0 n}; set a",
    "dict create a 1 b 2 a 3", "dict create a", "dict get {a 1 b 2 a 3}",
    "dict exists {a} key", "dict exists {a x} a b", "dict get {a x} a b",
    "dict exists {} k", "dict set d a b c 3; dict get $d a b c",
    "dict set d {} {a b} 4; dict exists $d {} {a b}",
    "dict unset d missing", "set d {a 1}; dict unset d absent",
    "set d {a 1}; catch {dict unset d missing k}; dict get $d a",
    "set d {a 1}; catch {dict set d a b 3}; dict get $d a",
    "dict set d a b c 1; dict unset d a b c; dict exists $d a b",
    "proc f {} {dict set ::state k 3}; f; dict get $state k",
])
examples = Path(__file__).resolve().parents[1] / "examples"
actuator = (examples / "sampled-actuator" / "actuator.tcl").read_text()
for gain in ["0.5", "0.25", "0.1"]:
    cases.append(actuator + "\nproc publish_adc {position code} {lappend ::trace $position $code}\n"
                 f"init {gain}; drive 1.0; sample; sample; drive 3.0; sample; set trace")
debounce = (examples / "debounce" / "debounce.tcl").read_text()
for delay in ["4", "8"]:
    cases.append(debounce + "\nproc restart_timer {d} {set ::due [expr {$::tick+$d}]}\n"
                 "proc drive_pin {v} {set ::pin $v}\n"
                 f"init {delay}; set pin 0; set tick 2; input_edge 1; set tick 4; input_edge 0; "
                 "set tick 6; input_edge 1; if {$due <= 12} {timer_elapsed}; list $due $pin")
growth_limits = {
    "expr {0xffffffffffffffff ^ -1 >> 64}": "-18446744073709551616",
    "expr {18446744073709551615 + 1}": "18446744073709551616",
    "expr {(1 << 63) * 4}": "36893488147419103232",
    "expr {1 << 100}": "1267650600228229401496703205376",
}
cases = list(dict.fromkeys([*cases, *growth_limits]))
version = subprocess.run([args.tclsh], input=b"puts [info patchlevel]\n", capture_output=True, check=True).stdout.decode().strip()
records = []
for source in cases:
    oracle = (
        "set source [encoding convertfrom utf-8 [binary decode hex " + source.encode().hex() + "]]\n"
        "set code [catch $source result]\nputs $code\n"
        "puts [binary encode hex [encoding convertto utf-8 $result]]\n"
    ) if source else "set code [catch {} result]\nputs $code\nputs {}\n"
    reference = subprocess.run([args.tclsh], input=oracle.encode(), capture_output=True, check=True)
    lines = reference.stdout.splitlines()
    if len(lines) != 2:
        raise RuntimeError("unexpected oracle output: " + repr(reference.stdout))
    code, value = int(lines[0]), bytes.fromhex(lines[1].decode())
    actual = subprocess.run([args.mcutcl, "eval", "--script", source], capture_output=True)
    actual_value = actual.stdout[:-1] if actual.stdout.endswith(b"\n") else actual.stdout
    matches = (actual.returncode == 0) == (code == 0) and (code != 0 or actual_value == value)
    if source in growth_limits:
        matches = (code == 0 and value == growth_limits[source].encode()
                   and actual.returncode == 1 and b": limit: " in actual.stderr)
    records.append({"source": source, "passed": matches, "reference_code": code,
                    "scope": "expected growth limit" if source in growth_limits else "Tcl behavior",
                    "actual_code": actual.returncode, "actual_stderr": actual.stderr.decode(errors="replace")})
output = {"format": "mcutcl-differential/1", "reference": version, "tests": len(records),
          "growth_limit_cases": len(growth_limits),
          "failures": sum(not r["passed"] for r in records), "records": records}
Path(args.output).parent.mkdir(parents=True, exist_ok=True)
Path(args.output).write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n")
print(f"Tcl {version}: {output['tests'] - len(growth_limits)} language cases and "
      f"{len(growth_limits)} expected growth limits, {output['failures']} mismatches")
for record in records:
    if not record["passed"]: print(record)
raise SystemExit(bool(output["failures"]))
