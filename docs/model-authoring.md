# Writing a model callback

The 0.4.0 automation profile supports stateful digital devices and sampled
analog models written in Tcl. The host loads each instance's source once,
calls its initialization procedure and invokes a named procedure for each event.

## Debounced digital input

The [debounce model](../examples/debounce/debounce.tcl) accepts input edges.
An edge saves the new level and restarts the instance's timer. When the timer
expires, Tcl drives the pin to the saved level. The
[example host](../examples/debounce/main.cpp) supplies the timer and the pin.

```sh
cmake --preset host
cmake --build --preset host
./build/host/mcutcl_debounce examples/debounce/debounce.tcl
```

Edges arrive at ticks 2, 4 and 6. With a four-tick delay, the program prints
`10 drive 1` followed by `12 sample 1`. To try a longer delay, change
`set ::settle_ticks $delay` in the Tcl file to `set ::settle_ticks 8` and run
the same executable. It prints `12 sample 0` followed by `14 drive 1`.
Pass `5 10 12 100` after the file name to advance the same example in slices.
The installed package includes the source and standalone CMake project under
`share/mcutcl/examples/debounce`.

## Sampled actuator and feedback ADC

The actuator takes a normalized drive input and moves a fraction of the
remaining distance toward it on each sample. Its Tcl procedure stores the new
position in an instance dictionary, clamps the position to [0, 1] and publishes
the position and rounded 12-bit ADC code. The host supplies inputs, schedules
samples every ten ticks and reads the most recently published code. [Tcl model](../examples/sampled-actuator/actuator.tcl) · [Test host](../examples/sampled-actuator/main.cpp)

```sh
./build/host/mcutcl_actuator examples/sampled-actuator/actuator.tcl
./build/host/mcutcl_actuator examples/sampled-actuator/actuator.tcl --reset 10 18 22 32
```

Both instances start at zero with input 1. The input changes to 3 at tick 25.

| Sample tick | A, gain 0.5 | A ADC | B, gain 0.25 | B ADC | Host read tick |
| --- | --- | --- | --- | --- | --- |
| 10 | 0.5 | 2048 | 0.25 | 1024 | 12 |
| 20 | 0.75 | 3071 | 0.4375 | 1792 | 22 |
| 30 | 1.0 | 4095 | 1.0 | 4095 | 32 |

With `--reset`, A restarts at tick 18 and its old tick-20 event expires.
The host reads 0 for A and 1792 for B at tick 22. A publishes its next sample
at tick 28. Passing intermediate deadlines yields the same samples as a single
run to tick 32. Tests use an absolute tolerance of 1e-12 for normalized analog
calculations and exact ticks and ADC codes. The installed example lives in
`share/mcutcl/examples/sampled-actuator`.

## Interpreter and callback contract

Each model instance owns an interpreter. The host also owns the scenario
interpreter, timer queue, virtual time, wiring, fault policy and lifetime of any
C++ objects captured by registered commands. All calls run on one owner thread.
While the scenario's run command is executing, its host may call an inactive
model interpreter. The model returns a value or error before the host processes
the next dependent event. Direct and indirect public reentry into an active
interpreter fails. To reset or replace a model, cancel its queued work or compare
the event's saved instance generation to the live generation before invocation.

Call `model.invoke(words, source, &run)` with a vector of UTF-8 strings. The first
element names the command. Whitespace, braces, NUL, Unicode and substitution
characters in arguments survive as data. The caller keeps the strings alive
and stable until the synchronous call returns. Commands such as `catch` explicitly
interpret their script argument. A trusted native command can call
`evaluate_nested(script, source)` on its current interpreter and inspect the
structured completion. The nested call shares its caller's local variables,
depth and budgets. Model procedures report body-relative locations under the
source identity `definition-file::procedure`. Catch bodies use `source::<catch>`.

| Language feature in 0.4.0 | Supported forms |
| --- | --- |
| State | `set varName ?value?` and `unset ?-nocomplain? ?--? ?varName ...?` on scalars. Inside a procedure, unqualified names are local and `::name` accesses persistent instance state. |
| Procedures | Define `proc name {arg ...} body` in the global frame. Names and parameters use ASCII identifiers starting with a letter or underscore. Procedure names may start with `::`. Definitions may replace another script procedure. Default arguments, a trailing variadic `args` and replacement of host or built-in commands reject. |
| Completion | The last command supplies a procedure's result. Plain `return ?value?`, `error message`, `break` and `continue` propagate through substitutions. `catch script ?resultVarName?` reports Tcl codes 0 through 4 for these forms. A break or continue escaping a procedure is a script error. Return options and the catch options variable are outside this checkpoint. |
| Control flow | `if test ?then? body`, repeated `elseif` arms and an optional final `?else? body`. Only selected tests and bodies run. `foreach varName list body` binds one scalar on each iteration. `while test body` repeats while the expression is true. Both loops consume body `break` and `continue` and propagate returns and errors. |
| Lists | `list`, `llength`, single-index `lindex`, `lappend varName ?value ...?` and `lset varName ?index ...? value`. Indices are canonical signed 64-bit decimal integers. `lset` accepts a list of indices or separate words, requires an existing variable, replaces the whole value for an empty path and appends at the list length. An out-of-range update fails before changing the list. |
| Dictionaries | `dict create ?key value ...?`, `get dictionary ?key ...?`, `exists dictionary key ?key ...?`, `set varName key ?key ...? value` and `unset varName key ?key ...?`. Nested keys may contain any supported text. Creation preserves first-key order and the last repeated value. Set creates missing parents. Unset tolerates an absent final key and diagnoses a missing parent. Updates commit after validation. |

An ordinary caught error has a code, source and location available through
`first_caught_error()` until the next accepted outer entry on that interpreter.
A host can record a fault independently of Tcl's catch result. Cancellation,
resource exhaustion and rejected reentry propagate through `catch` without
writing its result variable.

## Expressions and numeric limits

Pass one expression to `expr`. Braces let the expression engine control when
it reads a scalar or runs a bracketed command. It validates syntax before
evaluating any operand. `&&`, `||` and `?:` evaluate only the required operands.
Quoted and braced strings, grouping parentheses and function calls are supported.
Normal Tcl word substitution still runs first in an unbraced command argument.

| Operation | Qualified behavior |
| --- | --- |
| Integers | Decimal, hexadecimal (`0x`), binary (`0b`), octal (`0o`), explicit decimal (`0d`) and digit separators. Leading zeros alone are decimal. Both signs support magnitudes through 18446744073709551615. Integer overflow reports a resource limit. |
| Arithmetic | Unary `+` and `-`, addition, subtraction, multiplication, division, integer remainder and right-associative `**`. Integer division rounds down and a nonzero remainder has the divisor's sign. Mixed arithmetic converts to binary64. |
| Bitwise | `~`, `&`, `^`, `\|`, `<<` and arithmetic `>>` use integer operands and sign extension. A negative shift count is an error. A left shift growing beyond the magnitude bound reports a limit. Use an explicit mask for device-width fields. |
| Comparisons | `== != < <= > >=` compare numbers when both operands are numeric and otherwise compare strings. Mixed integer/double comparison preserves distinctions above 2^53. `eq ne lt le gt ge` compare string representations. `!`, `&&`, `\|\|` and `?:` accept numeric and Tcl boolean values. |
| Math | `abs`, `min` and `max` preserve the selected numeric type. `double` converts to binary64. `round` rounds halfway away from zero. `wide` truncates a finite double toward zero and explicitly interprets the low 64 bits as signed. An integer conversion outside the magnitude bound reports a limit. `floor`, `ceil`, `sqrt` and `exp` return doubles. |
| Floating-point boundaries | Signed zero and infinities can be represented. Nonzero double division by zero and overflowing `exp` produce signed infinity and positive infinity respectively. Integer division by zero, NaN arithmetic results, square roots of negatives and conversion of infinity to an integer report errors. NaN compares unordered. Decimal literals outside the binary64 parser's conversion range report a limit. |

The automation function list above is exhaustive. Trigonometry, logarithms,
random number generation, floating-point classification and the other Tcl math
functions remain under PLN-0004. Tcl's list-membership expression operators,
general list indices and arbitrary-precision integers also remain there.
The supported grammar and field encodings use the Tcl 9 reference manuals.
[Expressions](https://www.tcl-lang.org/man/tcl9.0/TclCmd/expr.html) · [Math functions](https://www.tcl-lang.org/man/tcl9.0/TclCmd/mathfunc.html)

## Run and storage limits

Pass the same `RunControl` to successive evaluations and invocations to share
their allowance. A call into an inactive interpreter during a native callback
automatically inherits the current control. Passing a different control in that
call is an error. A fresh outer call with no control starts with the interpreter's
command limit and the default byte allowance.

- A dispatch consumes one command immediately before lookup. Each entered loop
  iteration also consumes one command unit, including an empty body. Nested substitutions,
  procedure bodies and callbacks share the run count. Each interpreter also
  enforces its own command limit and reports its count for that entry.
- Work is charged in bytes of evaluated source (including re-evaluated bodies),
  expanded words or host arguments, inputs to the list codec, and returned
  values at each dispatch. A failed byte debit latches exhaustion. A host may
  charge native work explicitly through `consume_work`. Command side effects
  that already occurred survive a later error delivering a result.
- Each expression pass charges its input and each parsed operand uses a work
  unit. Evaluation also charges substituted values, arithmetic operands and
  collection conversion, lookup and update work. Expression nesting and nested
  key/index paths share the interpreter's depth bound. Path traversal and math
  function arguments are subject to the aggregate argument-byte limit.
- `Limits::argument_bytes` caps the combined word sizes of a single command.
  `state_bytes` counts variable names and values in globals and live frames,
  plus procedure names, formal names, bodies and diagnostic source labels.
  `unset`, frame return and procedure replacement release their charges.
  Variable and procedure counts bound empty entries. These are logical sizes.
  Container overhead, parser allocations and host allocations require separate
  memory provisioning.
- The optional bounded cancellation probe latches its first true result.
  The evaluator polls before substitutions and dispatch, on successful return,
  at loop iterations, between expression operands, and during long source,
  expression and list scans. Long native operations can poll
  `self.stop_requested()`. Source parsing and UTF-8 validation are bounded by
  the configured input sizes. A running native call must cooperate to return.

## Installed package and emulator handoff

Configure a consumer with `find_package(mcutcl 0.4 CONFIG REQUIRED)` and link
`mcutcl::mcutcl`. The linked library exposes `runtime_profile()` in
`mcutcl/profile.hpp`. Call `profile.require(...)` before loading a model. The
language identity is `mcutcl.tcl9-bootstrap/0.4.0`. The independently requestable
capabilities are `mcutcl.invoke/1`, `mcutcl.run-control/1`, `mcutcl.fixed-proc/1`,
`mcutcl.catch/1` and `mcutcl.automation/1`. A request for `mcutcl.binary/1`
currently returns `ErrorCode::unsupported`.

The existing mcuemu binding stages patches against 0.2.0. Its
`MCUEMU_MCUTCL_SOURCE_DIR` can point to that checkout while the migration is
underway. For PLN-0604, provide the engine's stop probe through a shared
`RunControl`, use the built-in `catch` and retain engine failures in the host's
fault latch. For PLN-0605, bind the models' timer, pin, drive and ADC operations
to guest-visible ports and use the traces above to check timing. The host owns
units, clock domains, event ordering and generations. [mcuemu migration](../../mcuemu/plan/multi-board.md#pln-0604-consume-the-supported-mcutcl-embedding-and-profile-contract) · [Guest model acceptance](../../mcuemu/plan/multi-board.md#pln-0605-host-isolated-script-defined-external-models)

The same argument-vector API can carry a segment identifier and link state, or
a node, clock domain and signed drift. For example, a host could register
`segment_state can_a offline` and `clock_drift node_b local 25` (parts per
million). These names illustrate possible host bindings.

On 18 November 2026, the Release and ASan/UBSan presets on macOS arm64 passed
48 runtime tests and both model fixtures. The installed-consumer test relocates
the package, builds both standalone examples and checks compatible and missing
capabilities, a caught host fault and cooperative cancellation. Tcl 9.0.1
matched 388 evaluation cases, 51 input-inspection cases and 168 list cases,
including the shipped models at different gains and delays. Four further
vectors verify a typed limit against the corresponding larger Tcl integer.

## Planned byte-oriented model

PLN-0017 specifies a C++ byte-value argument and result, bounded byte length
and slicing, and lossless storage and retrieval in Tcl collections and frames.
Planned `binary format` and `binary scan` fields include `a*` for raw bytes,
`c*`/`cu*` for octet lists, and `s/S`, `i/I`, `w/W` for 16-, 32- and 64-bit
integers in little/big endian order. Signed scan fields sign-extend and the
`u` modifier selects unsigned interpretation. An explicit packing width selects
the low bits of the integer. The responder checks the byte length and number of
scanned fields before committing a register write. [Tcl binary field definitions](https://www.tcl-lang.org/man/tcl9.0/TclCmd/binary.html)

The responder fixture accepts `01 00 ff`, stores 65280 and replies `00 ff` two
ticks later. A truncated `01 ff` fails before changing the register. Resetting
its instance invalidates the queued reply while its peer continues. The
installed-consumer gate covers all 256 octets and invalid UTF-8 as byte values.
[Requirements](../requirements.md#req-0025-qualified-byte-oriented-modeling-profile) · [Implementation plan](../plan/implementation-plan.md#pln-0017-qualify-the-byte-oriented-modeling-profile)
