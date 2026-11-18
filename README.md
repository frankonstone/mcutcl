# mcutcl

A project-owned C++23 interpreter targeting Tcl 9 core semantics, implemented
incrementally from scratch. It does not embed/link/invoke Tcl for runtime
execution. The current checkpoint is **mcutcl.tcl9-bootstrap/0.4.0** and includes
the **mcutcl.automation/1** profile for stateful digital and sampled analog models.

## Current checkpoint

Implemented: command/word separators, comments, nested braced/quoted words,
backslash continuations and basic character escapes, scalar `$name`/`${name}`,
nested command substitution, scalar `set`, initial `list`/`llength`/`lindex`,
host command registration, argument initialization, and typed
errors with source byte offset/line/byte column. Full commands are parsed before
their substitutions execute; earlier completed commands survive later errors.
Variables/interpreters retain independent state and have explicit source, value,
word, variable, command and nesting limits.

Version 0.3.0 introduced fixed-argument procedures, local variables and persistent
`::` scalars, `unset`, plain `return`, `break`, `continue`, `error message` and
`catch script ?resultVarName?`. C++ hosts can invoke a procedure with argument
values, cancel a run cooperatively, and share command and byte allowances
across callbacks. The [model authoring guide](docs/model-authoring.md) lists
the exact forms, storage limits and installed capabilities.

Version 0.4.0 adds checked integer and double expressions, lazy conditionals,
bounded loops, list updates and nested dictionaries. After building the host
preset with tests enabled, run either model in a virtual-time test host.

```sh
./build/host/mcutcl_debounce examples/debounce/debounce.tcl
./build/host/mcutcl_actuator examples/sampled-actuator/actuator.tcl
```

The digital model drives the pin at tick 10 after an input edge at tick 6.
The half-gain actuator and its 12-bit ADC produce 2048, 3071 and 4095 on
successive samples. Both examples ship as standalone CMake projects.

Open language work includes numeric/Unicode backslash escapes, `{*}` expansion,
arrays, general namespace resolution, remaining list/dict operations, arbitrary
precision integers, additional math functions, default/variadic procedures, full Tcl completion/error
options, binary-value operations, remaining core
commands and interactive console. UTF-8 literal values are supported; invalid
UTF-8 and unfinished forms are rejected. Basic `::set`/root scalar names work,
but do not establish namespace support. Unknown/unimplemented commands fail.

The C++ API returns `std::expected` and has no upstream Tcl C-ABI compatibility
promise. Public evaluation/registry mutation cannot re-enter an active instance;
internal command substitutions share its command budget. Native callbacks are
trusted, bounded, non-throwing operations; the interpreter cannot preempt a
blocking C++ callback. Limits do not provide a hard allocator ceiling, and host
allocation failure is not currently converted to a recoverable Tcl result.

## Simulation-authoring increment

The 17 November 2026 [requirements](requirements.md#simulation-authoring-and-embedding-increment--2026-11-17)
and [PLN-0009](plan/implementation-plan.md#pln-0009-reusable-automation-profile-and-supported-host-embedding)
add a qualified automation profile, public cancellation/completion, structured
invocation and installed capability negotiation for consumers such as mcuemu.
The 0.4.0 checkpoint implements the model language and public embedding interfaces.
mcuemu owns model lifecycle and virtual time. Its binding currently pins 0.2.0.
The byte-oriented profile is tracked separately in PLN-0017.
The broader [mcucom architecture](../mcucom/doc/architecture.md) keeps controller
communication/synchronization in portable firmware services. Network and
clock-domain scenario commands use host bindings, with no new Tcl language
construct or target dependency required solely for another fieldbus.

## Build and use

Requires CMake 3.25+, C++23 and Ninja for the presets. Tests use sibling/installed
`mcutest`; the optional CLI uses sibling/installed `mcucli`. The runtime library
has no dependency beyond the C++ standard library. Set
`MCUTCL_MCUTEST_SOURCE_DIR` / `MCUTCL_MCUCLI_SOURCE_DIR` for other locations, or
disable the respective build options. CMake never downloads dependencies.

```sh
cmake --preset host
cmake --build --preset host --parallel
ctest --preset host
cmake --build --preset host --target evidence
./build/host/mcutcl eval --script 'set who {MCU world}; set message "hello $who"'
./build/host/mcutcl run --file example.tcl
./build/host/mcutcl profile
```

`eval` prints the final result; `run` prints script output only. The CLI registers
an initial `puts` accepting one stdout argument. Other `puts` forms remain
unsupported. Runtime errors return 1, argument/file errors return 2. Core library
evaluation performs no implicit I/O. `sanitizers` enables ASan/UBSan.

The installed CMake package exports `mcutcl::mcutcl`:

```cpp
#include <mcutcl/interpreter.hpp>
mcutcl::Interpreter interpreter;
auto result = interpreter.evaluate("set answer {hello world}", "example.tcl");
```

Hosts register a named C++ callback receiving an argument vector including the
command name. mcuemu will supply its own `emu` command family; no emulator,
hardware-model, wall-clock or filesystem commands are implicitly installed by
the runtime. Pending/long-running host operations need a later explicit bridge.

## Conformance and status

Tests use original source cases and independent expected results. The optional
development oracle requires Python and an explicitly selected reference Tcl:

```sh
python3 tests/differential.py --mcutcl build/host/mcutcl \
  --tclsh /path/to/tclsh --output build/host/artifacts/differential.json
```

This compares success/failure and successful UTF-8 result bytes for the selected
forms. Separate vectors check the runtime's integer growth limit against the
larger exact Tcl result. Python
and reference Tcl are not product/runtime dependencies. No Tcl implementation
source is vendored. The semantic reference is the
[Tcl 9 language manual](https://www.tcl-lang.org/man/tcl9.0/TclCmd/Tcl.html).

[Requirements](requirements.md), [plan](plan/implementation-plan.md), and
`mcutrace.toml` keep incomplete work visible. Source/line test observations are
not source coverage. Cross-platform, complete core-command inventory, installed
consumer qualification in mcuemu, cancellation in future language operations,
and full Tcl conformance remain verification gates.

## Parse-only input inspection (runtime 0.1.1)

`inspect_input(source, source_name, limits)` returns complete, incomplete, or
invalid within the published bootstrap language profile. It never evaluates
commands/substitutions, resolves variables, invokes host callbacks, or consumes
an interpreter dispatch budget. Incomplete results retain a missing-delimiter
or trailing-continuation diagnostic for end-of-input reporting. Invalid results
retain syntax/unsupported/limit diagnostics. Source/word/depth bounds apply.
This profile-aware API is not a complete implementation of Tcl `info complete`;
unimplemented lexical forms are rejected explicitly. The optional CLI exposes
`mcutcl complete --script SOURCE` for development inspection. The runtime API
supports the mcuemu multiline console; a standalone mcutcl REPL remains open.
The language profile stays `mcutcl.tcl9-bootstrap/0.1.0` because this release adds
an embedding API without expanding the language semantics.

Verified locally on 2026-10-30: 19 runtime tests in Release and ASan/UBSan;
83 evaluation and 51 input-inspection cases against Tcl 9.0.1, all passing.
The optional inspection comparison is `python3 tests/completeness.py --mcutcl
/path/to/mcutcl --tclsh /path/to/tclsh --output /path/to/completeness.json`.
These are bounded bootstrap-profile checks, not full Tcl conformance or host
qualification. The mcuemu consumer separately verifies its installed component.

## Initial list/argument profile (0.2.0)

The 0.2.0 release introduced `mcutcl.tcl9-bootstrap/0.2.0`. The parse-only
inspection API from 0.1.1 remains available unchanged. `list` serializes values;
`llength` counts parsed elements; initial `lindex list index` accepts one canonical
signed 64-bit decimal index. Negative/out-of-range indices return an empty value.
Other index forms (`end`, expressions, alternate radices, leading zeros), no-index
and multiple-index forms remain explicitly unsupported. This is not full Tcl
list/dictionary compatibility.

`make_list`/`parse_list` are declared in `mcutcl/list.hpp`. Lists are UTF-8 data,
with grouping and basic escapes; there are no variable/command substitutions.
Braced list values preserve backslash-newline literally, as Tcl 9 requires;
quoted/plain values perform the supported backslash substitutions. Numeric
escapes outside braced list values remain unsupported. Serialization preserves
empty values, unbalanced braces, quotes, backslashes, whitespace, NUL and Unicode.
The encoder's valid Tcl list representation need not match Tcl's canonical text.
Element count, encoded/input bytes and brace depth are bounded by the host limits.

`Interpreter::set_arguments(argv0, arguments)` initializes the standard `argv0`,
`argc`, `argv` variables without evaluating source. Recoverable validation occurs
before all three variables change; replacing arguments during evaluation is
rejected. Initialization uses the value/variable/list limits and no dispatch or
host services. Scripts may subsequently modify these ordinary Tcl variables.

Locally verified on 2026-10-30: 28 runtime tests, 83 evaluation cases, 51 input
inspection cases and 168 list cases against Tcl 9.0.1, all passing. The list
oracle additionally decodes actual encoder output with reference Tcl. It is
optional development tooling in `tests/lists_differential.py`, accepting the same
`--mcutcl`, `--tclsh`, `--output` options as the other differential tools.
