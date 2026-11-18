# mcutcl requirements

The accepted target is Tcl 9 core language semantics, implemented incrementally
in original C++23. A passing initial subset is not full Tcl compatibility.
Reference Tcl may be used as a development oracle, never as the runtime backend.
Tk, binary compatibility with the upstream Tcl C API, and Tcl 8.6 compatibility
are not selected targets. Each checkpoint publishes an explicit supported profile.

### REQ-0001 Project-owned interpreter
mcutcl shall implement parsing and evaluation itself without linking to,
embedding, or invoking an existing Tcl interpreter for operational execution.
Acceptance shall run the library/CLI without a Tcl runtime dependency and
distinguish optional reference-conformance tooling from product dependencies.

### REQ-0002 Tcl script and word parsing
mcutcl shall follow Tcl 9 command/word syntax, separators, quoting, nested braces,
comments and continuation rules. It shall validate each command's complete syntax
before executing its substitutions while preserving earlier completed commands.
Acceptance shall cover grouping, empty words, literal delimiters, malformed
syntax, nested command boundaries, and source-position diagnostics.

### REQ-0003 Substitution and argument expansion @depends(REQ-0002)
mcutcl shall implement Tcl 9 variable, command, backslash and argument-expansion
semantics with the correct evaluation order and word boundaries. Substituted
values shall not be implicitly reparsed. Acceptance shall compare nested effects,
escaped delimiters, numeric/Unicode escapes, expansion lists and error cases.

### REQ-0004 Variable storage and access @depends(REQ-0003)
mcutcl shall implement scalar and array variables, Tcl naming/resolution, reads,
writes and removal with defined scope interactions. Acceptance shall verify
empty/unusual names, arrays, undefined reads, updates and scope behavior. Early
scalar-only checkpoints shall reject unfinished array/namespace operations.

### REQ-0005 Lists and dictionaries @depends(REQ-0003)
mcutcl shall implement Tcl list/dictionary values and core operations with correct
quoting, round trips, indexing, mutation and error behavior. Acceptance shall
compare nested values and malformed input with independent/reference cases.

### REQ-0006 Expressions and numeric values @depends(REQ-0003)
mcutcl shall implement Tcl 9 expression parsing, precedence, conversions,
short-circuiting and numeric semantics, including bignum obligations in the
compatibility inventory. Acceptance shall verify independent boundary vectors,
lazy evaluation, divide/error cases and reference results. Resource limits shall
not silently turn overflow into wrapped or floating-point approximations.

### REQ-0007 Control flow and completion @depends(REQ-0003, REQ-0006)
mcutcl shall implement conditional/loop commands and Tcl completion outcomes,
including error, return, break and continue propagation and catching. Acceptance
shall cover nested and nonlocal completion, lazy bodies and limits on loops.

### REQ-0008 Explicit resource and execution limits
mcutcl shall expose per-interpreter execution/resource limits and report their
exhaustion explicitly. Nested evaluation shall share the appropriate budget;
host callbacks shall have a declared bounded/cooperative contract. Acceptance
shall cover nested dispatch, large values, source/word/depth limits and later
infinite-loop cancellation. Allocation failure shall not be reported as success.
Limits shall also bound work within individual expression/collection operations,
temporary value growth and aggregate retained script storage, including procedure
bodies and model state. Document accounting units and cancellation checkpoints;
a command count alone shall not establish bounded execution. Hosts shall be able
to enforce cumulative work across successive invocations without resetting their
run allowance when a callback returns or an interpreter instance is replaced.

### REQ-0009 Structured errors and source context
mcutcl shall preserve diagnostic categories, source identity/location and Tcl
error/completion information as implemented. Acceptance shall locate nested
errors, retain their underlying causes and distinguish syntax, unsupported
behavior, script errors, resource limits and host-binding failures.

### REQ-0010 Embeddable C++ command API
mcutcl shall provide an independent C++ library with isolated interpreter state
and registration of named host commands accepting structured argument vectors
and returning values/errors. Ownership, reentry and callback lifetime shall be
explicit. Acceptance shall verify argument boundaries, host effects/results,
multiple instances, failures and rejected unsafe reentry/registry mutation.

### REQ-0011 Procedures and call frames @depends(REQ-0004, REQ-0007)
mcutcl shall implement procedure arguments/defaults, local frames, result/return
behavior and required frame access commands. Acceptance shall cover recursion,
scope/alias interactions, invalid calls and depth/budget exhaustion.

### REQ-0012 Namespaces and command resolution @depends(REQ-0010, REQ-0011)
mcutcl shall implement Tcl namespace and command resolution, including qualified
names, command lifecycle and relevant alias/import behavior. Acceptance shall
cover conflicting names, nested namespaces and missing/removed commands.

### REQ-0013 Core-command compatibility inventory
mcutcl shall maintain an inventory of Tcl 9 core command families and their
implementation/verification state, including strings, regular expressions,
channels/files, introspection, packages, interpreter operations and TclOO.
Optional host-service availability shall be explicit. Acceptance shall identify
unsupported commands/options and prevent a small successful script from implying
full Tcl 9 core compatibility. Each family requires its own later work package.

### REQ-0014 Unicode, binary and value semantics
mcutcl shall follow Tcl 9 value semantics for Unicode strings and binary values
without representation-dependent results. Acceptance shall exercise encodings,
NUL/non-ASCII values, escapes, indexing and string/binary conversions. Partial
UTF-8-only checkpoints shall declare and reject unsupported representations.

### REQ-0015 Batch command-line execution @depends(REQ-0010)
mcutcl shall execute source strings and files through its library and report
completion/errors through documented output/exit status. Acceptance shall verify
shared language behavior, file/source errors, diagnostics and nonzero failures.

### REQ-0016 Interactive console @depends(REQ-0002, REQ-0015)
mcutcl shall provide a persistent interactive console with multiline completeness
detection, results/diagnostics and explicit interruption. Completeness checking
shall not execute partial input or replay side effects. Acceptance shall verify
multiline bodies, nested syntax, error recovery, state retention and cancellation.

### REQ-0017 Deterministic host integration @depends(REQ-0010, REQ-0008)
Hosts shall be able to control exposed commands, time and external services so
script effects can follow their own deterministic execution policy. Runtime
capabilities, pending-operation/resumption and cancellation behavior shall be
explicit. Acceptance shall integrate a bounded host operation without hidden
wall-time advancement or recursive ownership of the host scheduler.

### REQ-0018 Independent conformance harness
mcutcl shall verify implemented syntax/semantics with independent expected values
and optional differential execution against an identified Tcl 9 reference.
Acceptance shall record the reference version, supported test scope and actual
results, retain negative cases and keep Tcl absent from normal runtime needs.

### REQ-0019 Packaging and host support @depends(REQ-0010, REQ-0015)
mcutcl shall build as a reusable installed CMake package and CLI for macOS,
Linux and Windows with explicit dependency resolution. Acceptance shall compile
an independent installed consumer and qualify each supported host. Configuration
shall not perform implicit network downloads.

### REQ-0020 Published compatibility profiles
Each release/checkpoint shall identify the targeted Tcl 9 semantics, supported
commands/options/value forms, limits and missing features. Acceptance shall
check published profiles against actual behavior. Unimplemented features shall
fail explicitly instead of being silently approximated.

## Simulation-authoring and embedding increment — 2026-11-17

This increment supports the sibling mcuemu multi-board rework through reusable
language and host APIs. mcutcl shall remain independent of emulator types, board
identity, fieldbus protocols, plant physics and virtual-time scheduling.
The broader mcucom/mcuemu design uses host commands for network/clock-domain
scenarios; controller communication and synchronization are not Tcl runtime
services and add no required firmware dependency. Existing full Tcl 9
requirements remain; the profile below is a bounded checkpoint within that target.
The first modeling checkpoint covers digital behavior and simple sampled analog
models. REQ-0025 defines a separately qualified byte-oriented follow-on; it shall
not block the initial automation or installed embedding delivery. Model packaging,
typed ports, physical units, event ordering, deterministic random streams and
guest-device integration remain mcuemu services. The examples below qualify the
language/embedding handoff, not a plant solver or new emulated peripheral.
See [PLN-0009 and its leaves](plan/implementation-plan.md#pln-0009-reusable-automation-profile-and-supported-host-embedding)
and the [cross-project plan](../mcuhexapod/doc/multi-project-simulation-plan.md).

### REQ-0021 Qualified reusable-automation language profile
@evidence(test)
@depends(REQ-0001, REQ-0004, REQ-0005, REQ-0006, REQ-0007, REQ-0008, REQ-0011, REQ-0020)

mcutcl shall publish a versioned automation profile sufficient to author reusable
parameterized scenarios and stateful digital/sampled analog host models. Its exact
command/option/value inventory shall include `if`, `foreach`, `proc`, `return`,
`break`, `continue`, scalar `set`/`unset`, `list`, `llength`, `lindex`, `lappend`,
`lset`, and `dict create/get/set/exists/unset`. Publish supported argument, index
and nested-key forms; the selected forms shall support the fixtures below.

Procedures shall read and update persistent interpreter-global state through
root-qualified variable names, including a model-state dictionary. Procedure-local
variables shall retain Tcl frame semantics. Returning from a callback shall not
discard model state; separate model instances shall not share it. Full namespace
and scope-alias support remain under their existing requirements.

The expression profile shall include lazy logical/conditional evaluation,
numeric and string equality/comparison, integer arithmetic and bitwise/shift
operations, and double-precision floating-point arithmetic. Exact integer inputs
shall cover signed and nonnegative 64-bit quantities, including decimal/hexadecimal
forms, without conversion through floating point. Publish integer growth limits,
conversion/rounding rules and floating-point error/non-finite behavior. Include
`abs`, `min`, `max`, `double`, `wide`, `round`, `floor`, `ceil`, `sqrt` and `exp`;
inventory other math functions explicitly. Overflow shall not silently wrap or
lose precision; fixed-width device behavior shall use explicit masks/conversions.
Host timestamps remain exact integer data; time units and plant integration steps
belong to the host/model. Numeric, completion and scope behavior shall follow Tcl 9
for supported forms; unsupported forms shall reject rather than approximate.

Acceptance shall run a debounced digital input with delayed output and a simple
sampled actuator/sensor model with saturation and feedback, using registered
test-host commands and independently specified expected results. Run two copies
of each with different parameters, preserve state over repeated callbacks and
reset one without affecting the other. Cover nested completion, malformed input,
lazy branches, integer values above 2^53, 64-bit masks, floating-point boundaries,
declared numerical tolerances and exhausted budgets. Compare supported language
behavior with an identified Tcl 9 oracle. Real GPIO and DAC/PWM-to-ADC guest effects
are the mcuemu consumer gate under
[REQ-0233](../mcuemu/req/multi-board.md#req-0233-reusable-script-defined-external-models)
and [PLN-0605](../mcuemu/plan/multi-board.md#pln-0605-host-isolated-script-defined-external-models),
not claims from the test host.
Publish exact coverage and exclusions without claiming full Tcl, TclOO, packages,
filesystem services or upstream binary-extension compatibility.

### REQ-0022 Public cooperative cancellation and completion contract
@evidence(test)
@depends(REQ-0008, REQ-0009, REQ-0010, REQ-0017)

Hosts shall have supported public APIs for cooperative cancellation, bounded
nested dispatch and structured completion/error inspection, including the qualified
catch semantics needed by embeddings. Hosts shall not patch interpreter source
to obtain these services. Language completion, budget/cancellation stops and
host-originated errors shall remain distinguishable; catching a language error
shall not acknowledge or clear an external host's fault latch.

Acceptance shall cancel before substitutions, during bounded nested/loop work and
within expensive interpreter-owned expression/collection operations. Saturate
temporary and aggregate persistent-storage limits, including state/procedure
growth across repeated callbacks. Preserve shared nested budgets and provide the
public accounting/control needed for a host to enforce cumulative work across
successive invocations and instances. Callback return, caught errors and model
reset shall not refund a host run budget; cancellation/exhaustion shall not permit
unbounded continuation through `catch`.

Verify catch/result/error propagation and invoke a cooperative long-running host
command. Document that blocking native callbacks cannot be forcibly preempted.
Existing mcuemu dispatch-cancel/catch fixtures shall be ported to the public
interface with their behavior and limits explicitly qualified; general completion
semantics remain governed by REQ-0007.

### REQ-0023 Structured invocation and isolated host-managed instances
@evidence(test)
@depends(REQ-0010, REQ-0017, REQ-0022)

Hosts shall invoke registered commands and supported procedures using structured
argument values without constructing executable source text. Interpreter state,
host registrations and user context shall remain isolated between instances.
The API shall specify source identity, evaluation/registry ownership, callback
lifetime and explicit initialization/teardown. Unsafe public reentry into an
active instance shall reject. Support a scenario interpreter plus separate model
interpreters on one host execution thread: a scenario host command may dispatch
a bounded callback into an inactive model interpreter while the scenario remains
active. A callback targeting an already-active instance shall be deferred until
that instance returns, or fail explicitly when deferral cannot meet the host's
event-order contract. Cross-instance calls shall not bypass same-instance reentry
guards. Model callbacks shall return without recursively advancing host time.

Acceptance shall preserve whitespace, braces, substitution-looking text, empty
and supported binary/Unicode values as data, run two instances with different
state, reject active reentry/mutation and demonstrate orderly teardown with
queued host work. A host-owned instance-generation guard shall prevent callbacks
targeting a destroyed/recreated interpreter. The host owns scheduling and event
cancellation; mcutcl shall not create a second clock or background executor.

A test host shall advance toward tick 100 from an active scenario, invoke a model
update at tick 10 and observe its result at tick 12 before the run command returns.
Deferring all model work until tick 100 shall fail acceptance. Repeated continuous
and sliced execution on the same qualified host shall preserve event order and
model results; slicing shall not change model integration steps. Analog comparisons
to independent reference calculations or across qualified hosts shall use declared
numerical tolerances; event order and integer data remain exact. Exercise
reset/destruction with queued work and callback failure. Hosts shall receive
failures before they advance past a dependent observation and shall retain their
own failure policy. This test host supplies time and events; mcutcl supplies
invocation and ownership.

### REQ-0024 Installed feature negotiation for embedding consumers
@evidence(test)
@depends(REQ-0010, REQ-0019, REQ-0020, REQ-0022, REQ-0023)

The installed C++ package shall expose runtime/language/embedding capability
identities so consumers can require supported cancellation, completion and
invocation services without source-layout checks or private headers. Feature
availability shall be explicit per profile; basic host commands shall not require
the entire future Tcl language. Distinguish the early embedding package, the
digital/analog automation profile and the later byte-oriented profile. Advertise
the qualified state, numeric, value and resource contracts for each. Profile/version
mismatches shall fail before dependent evaluation with actionable diagnostics.

Acceptance shall build an independent installed consumer without the mcutcl
source tree, verify compatible and incompatible capability requests, and integrate
the sibling mcuemu binding through public APIs with no source-text rewriting.
Reference Tcl remains optional development tooling. Additional-host qualification
continues under REQ-0019, with every result identifying the host actually tested.
The independent consumer shall be sufficient to release an embedding package for
migration. Accept this requirement in full only after the actual mcuemu consumer
also passes; the installed example using the existing command subset may ship earlier.

### REQ-0025 Qualified byte-oriented modeling profile
@evidence(test)
@depends(REQ-0005, REQ-0006, REQ-0008, REQ-0014, REQ-0019, REQ-0020, REQ-0021, REQ-0023)

mcutcl shall provide a separately versioned byte-oriented modeling profile with
lossless byte values across host invocation, procedure arguments/results and
persistent collections. It shall qualify Tcl 9 `binary format`/`binary scan` forms
for byte sequences and signed/unsigned 16-, 32- and 64-bit field interpretation
with explicit little/big byte order, together with bounded byte length/slicing
and the bitwise operations from REQ-0021. Publish exact formats, conversion/range
rules and size/work limits. Raw host bytes shall not be implicitly decoded as
UTF-8. Host adapters shall distinguish binary data from Unicode text.

The initial automation profile may exchange validated integer lists with elements
in 0..255 through host adapters; that representation shall not advertise binary
capability or silently reinterpret text. This later profile shall be independently
negotiable and shall not gate the initial digital/analog models.

Acceptance shall round-trip all 256 byte values, embedded NUL, invalid UTF-8 byte
sequences and substitution-looking data through host/procedure/collection paths.
Verify endian and signed-field boundary vectors, malformed/truncated payloads and
resource exhaustion against independent expectations and an identified Tcl 9
oracle for the supported forms. A byte-register device responder shall maintain
independent state in two instances, handle a delayed reply and reset with queued
work. Its test-host transport does not establish a UART/SPI/I2C/CAN peripheral;
actual bus attachment and guest-observed exchange require a separately qualified
mcuemu transport. Fieldbus protocols remain outside the interpreter.
