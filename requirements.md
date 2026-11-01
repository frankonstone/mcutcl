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
