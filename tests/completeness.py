"""Development-only bootstrap input inspection comparison; no runtime Tcl use."""
import argparse
import itertools
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--mcutcl', required=True)
parser.add_argument('--tclsh', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
cases = ['', '# comment', '# continued\\\n', '# continued\\\n  ', 'unknown $missing', 'set x $',
         'set x \\', 'set x \\\n', 'set x \\\n  ', 'set x ${name', 'set x ${name}', 'set x a{b', 'set x ]',
         'set x [# comment\n]', 'set x [# comment', 'set x {a\\}b}', 'set x {a\\{b}',
         'set x [set y {]}]', 'set x [set y "]"]', 'set x {Grüße 🚀}']
for prefix in ['set x ', 'unknown; set x ', '# comment\nset x ']:
    for word in ['{value}', '{value', '"value"', '"value', '[set y value]', '[set y value', '${name}', '${name']:
        cases.append(prefix + word)
for left, right in itertools.product(['{', '"', '[set y '], ['a', 'a\n', 'a\\\n']):
    cases.append('set x ' + left + right)
records = []
version = subprocess.run([args.tclsh], input='puts [info patchlevel]\n', text=True, capture_output=True, check=True).stdout.strip()
for source in dict.fromkeys(cases):
    encoded = source.encode().hex()
    oracle = f'set source [encoding convertfrom utf-8 [binary decode hex {{{encoded}}}]]\nputs [info complete $source]\n'
    reference = subprocess.run([args.tclsh], input=oracle, text=True, capture_output=True, check=True)
    expected = 'complete' if reference.stdout.strip() == '1' else 'incomplete'
    actual = subprocess.run([args.mcutcl, 'complete', '--script', source], text=True, capture_output=True)
    passed = actual.returncode == 0 and actual.stdout.strip() == expected
    records.append({'source': source, 'reference': expected, 'actual': actual.stdout.strip(),
                    'status': actual.returncode, 'stderr': actual.stderr, 'passed': passed})
result = {'format': 'mcutcl-completeness-differential/1', 'reference': version, 'tests': len(records),
          'failures': sum(not r['passed'] for r in records), 'records': records}
Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print(f'Tcl {version}: {len(records)} input-inspection cases, {result["failures"]} mismatches')
for record in records:
    if not record['passed']: print(record)
raise SystemExit(bool(result['failures']))
