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
cases = list(dict.fromkeys(cases))
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
    records.append({"source": source, "passed": matches, "reference_code": code,
                    "actual_code": actual.returncode, "actual_stderr": actual.stderr.decode(errors="replace")})
output = {"format": "mcutcl-differential/1", "reference": version, "tests": len(records),
          "failures": sum(not r["passed"] for r in records), "records": records}
Path(args.output).parent.mkdir(parents=True, exist_ok=True)
Path(args.output).write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n")
print(f"Tcl {version}: {output['tests']} differential cases, {output['failures']} mismatches")
for record in records:
    if not record["passed"]: print(record)
raise SystemExit(bool(output["failures"]))
