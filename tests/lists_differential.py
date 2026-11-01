"""Development oracle: list values/commands, not Tcl list string canonicalization."""
import argparse
import itertools
import json
import subprocess
import tempfile
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--mcutcl',required=True);parser.add_argument('--tclsh',required=True);parser.add_argument('--output',required=True)
args=parser.parse_args()
def quote(value):
    if not value: return '{}'
    mapping={'\n':r'\n','\t':r'\t','\r':r'\r','\v':r'\v','\f':r'\f','\a':r'\a','\b':r'\b'}
    return ''.join(mapping.get(c, '\\'+c if c in ' {}[]$;\\"#' else c) for c in value)
def oracle(source):
    code=f'set source [encoding convertfrom utf-8 [binary decode hex {{{source.encode().hex()}}}]]\nset code [catch $source result]\nputs $code\nputs [binary encode hex [encoding convertto utf-8 $result]]\n'
    run=subprocess.run([args.tclsh],input=code.encode(),capture_output=True,check=True)
    lines=run.stdout.splitlines();return int(lines[0]),bytes.fromhex(lines[1].decode())
version=subprocess.run([args.tclsh],input=b'puts [info patchlevel]\n',capture_output=True,check=True).stdout.decode().strip()
values=['','plain','two words','{unbalanced','}close','"quoted"',r'a\b','line\nsecond\t','[unknown];$missing','# comment','Grüße 🚀','a\x00b']
cases=['llength {}','llength {{one two} {} three}', 'lindex {{one two} {} three} -1','lindex {a b} 99']
for left,right in itertools.product(values, repeat=2):
    cases.append('set x [list '+quote(left)+' '+quote(right)+'];lindex $x 1')
for value in ['{open','"open','{a}tail','"a"tail','{a\\\n b}','"a\\\n b"','a\\\n b','a\u00a0b']:
    cases.append('lindex '+quote(value)+' 0')
records=[]
with tempfile.TemporaryDirectory(prefix='mcutcl-list-oracle-') as directory:
    path=Path(directory)/'case.tcl'
    for source in cases:
        code,expected=oracle(source)
        # puts is registered by the actual CLI; source files preserve NUL bytes.
        path.write_bytes(('puts ['+source+']\n').encode())
        actual=subprocess.run([args.mcutcl,'run','--file',str(path)],capture_output=True)
        output=actual.stdout[:-1] if actual.stdout.endswith(b'\n') else actual.stdout
        passed=(actual.returncode==0)==(code==0) and (code!=0 or output==expected)
        records.append({'source':source,'passed':passed,'reference_code':code,'actual_code':actual.returncode,'stderr':actual.stderr.decode(errors='replace')})
    # Independently decode actual encoder output using reference Tcl.
    for value in values:
        path.write_bytes(('puts [list '+quote(value)+']\n').encode())
        actual=subprocess.run([args.mcutcl,'run','--file',str(path)],capture_output=True)
        encoded=actual.stdout[:-1]
        source='lindex [encoding convertfrom utf-8 [binary decode hex {'+encoded.hex()+'}]] 0'
        code,decoded=oracle(source)
        passed=actual.returncode==0 and code==0 and decoded==value.encode()
        records.append({'encoder_value':value,'passed':passed,'reference_code':code,'actual_code':actual.returncode})
result={'format':'mcutcl-list-differential/1','reference':version,'tests':len(records),'failures':sum(not r['passed'] for r in records),'records':records}
Path(args.output).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(f'Tcl {version}: {len(records)} list cases, {result["failures"]} mismatches')
for record in records:
    if not record['passed']: print(record)
raise SystemExit(bool(result['failures']))
