# mcutcl implementation plan

The 0.4.0 runtime supplies the bounded digital and sampled analog automation
profile. Both Tcl models run in a standalone host built against the installed
package. [Model authoring guide](../docs/model-authoring.md)

## PLN-0001 Independent runtime, profiles and host build
@link(REQ-0001, REQ-0019, REQ-0020)
@status(in_progress)
- [ ] Build/install core and CLI, qualify macOS/Linux/Windows, and maintain profile
  and dependency identities. The initial macOS build does not close this gate.

## PLN-0002 Parsing, substitutions and independent conformance
@link(REQ-0002, REQ-0003, REQ-0014, REQ-0018)
@status(in_progress)
- [ ] Finish Tcl 9 word parsing, complete backslash/Unicode escapes, list expansion
  and binary values; compare edge cases with identified reference Tcl releases.
- [ ] Extend the partial UTF-8 parser tests; add malformed-input fuzzing and value
  representation/conversion coverage before claiming full syntax compatibility.

## PLN-0003 Variables, frames and namespaces
@link(REQ-0004, REQ-0011, REQ-0012)
@status(in_progress)
- [ ] Extend initial scalar storage to arrays, procedures/default arguments,
  completion-aware frames, scope links, namespace and command resolution.

## PLN-0004 Lists, dictionaries and expression engine
@link(REQ-0005, REQ-0006)
@status(in_progress)

Progress 2026-10-30: the 0.2.0 bootstrap profile implements bounded UTF-8 list
encoding/decoding, `list`, `llength`, one canonical decimal `lindex`, and atomic
host initialization of `argv0`/`argc`/`argv`. Independent/reference cases cover
round trips and malformed forms. Full lists/dictionaries, general indices,
numeric escapes and the expression engine remain open.
- [ ] Implement value/list/dictionary contracts and Tcl expression evaluation,
  including exact numeric conversion, bignums and short-circuit behavior.

## PLN-0005 Completion, control flow and bounded execution
@link(REQ-0007, REQ-0008, REQ-0009)
@status(in_progress)
- [ ] Extend initial typed errors/dispatch bounds to all Tcl completions, control
  commands, shared frame budgets, interruption and comprehensive resource tests.

## PLN-0006 Host command SDK and deterministic integration
@link(REQ-0010, REQ-0017)
@status(in_progress)
- [ ] Qualify initial native command vectors/results and lifetime rules; define
  resumable operations and host-clock/capability policies needed by mcuemu.

## PLN-0007 Batch CLI and interactive console
@link(REQ-0015, REQ-0016)
@status(in_progress)
- [ ] Extend batch commands to persistent multiline REPL with parse-only
  completeness, interruption and no replay of already-executed commands.

Progress 2026-10-30: runtime 0.1.1 adds parse-only, bounded input inspection and
its CLI development entry point. Grouping/continuation, non-execution, invalid
syntax and limit tests cover the bootstrap profile. The independent mcuemu
consumer uses it for multiline input; standalone REPL/interruption and full
Tcl lexical compatibility remain open.

## PLN-0008 Full core inventory and family packages
@link(REQ-0013)
@status(planned)
- [ ] Inventory Tcl 9 commands/options and split remaining string/regexp,
  channel/filesystem, introspection, package, interpreter and TclOO work into
  independent implementation/verification packages. No unimplemented family is
  excluded from the core target merely to improve reported compatibility.

## PLN-0009 Reusable automation profile and supported host embedding
@link(REQ-0021, REQ-0022, REQ-0023, REQ-0024)

Added 2026-11-17 for the multi-project simulation rework. This parent owns
REQ-0021–0024; existing PLN-0001–0008 retain full language/host scope
and current statuses. Children deliver finite parts of those foundations where
linked, without duplicating an independent implementation. Parent progress is
derived from the leaves. mcutcl owns no board, fieldbus or system clock.
The new mcucom project owns controller communication/synchronization; network and
clock-domain scenarios remain mcuemu host commands using these embedding contracts.
The first modeling checkpoint covers stateful digital and sampled analog models.
PLN-0017 owns the separately qualified byte-oriented follow-on under REQ-0025;
it does not block this parent's acceptance or the early embedding package.
See the [cross-project handoff](../../mcuhexapod/doc/multi-project-simulation-plan.md).

### PLN-0010 Specify automation profiles and public embedding boundaries
@link(REQ-0021, REQ-0022, REQ-0023, REQ-0024, REQ-0020)
@status(done)

18 November 2026. The 0.4.0 contract lists operators, math functions, indices, dictionary paths,
callback outcomes, work units and numeric boundaries. It specifies the digital,
analog and future byte-responder vectors, the planned binary fields and the
host-owned scheduling and device interfaces. [Profile and fixtures](../docs/model-authoring.md)

**Entry:** Current 0.2.0 bootstrap, existing language requirements and mcuemu staging extensions.

**Deliverable:** Inventory exact commands/options/index and numeric forms/completion outcomes and publish staged embedding, digital/analog automation and later byte-oriented profile contracts. Specify persistent root-qualified model state across local procedure frames, the minimum collection/math operations in REQ-0021, exact integer timestamp transport, floating-point tolerances and work/storage accounting. Assign each feature to its existing owner and expose capability negotiation design. Document a scenario interpreter plus isolated model interpreters on one execution thread, including timely callbacks during a run, same-instance reentry rejection and host-owned run budgets. Include representative multi-network/clock-domain host commands without making fieldbus protocols, controller synchronization or plant integration interpreter features.

**Completion gate:** Specify independent positive/negative vectors for the debounced input, sampled actuator/sensor and later byte-register responder. Include two-instance state/reset, tick-10 update/tick-12 observation within a run to tick 100, byte representation and resource-limit cases. Fix callback input/result/state ownership and failure handling before API implementation. Separate test-host evidence from real mcuemu guest integration, and confirm digital/analog acceptance does not depend on binary support. No requirement is narrowed by the checkpoint; no upstream Tcl ABI or host I/O is implicitly promised.

### PLN-0011 Implement supported cancellation and structured catch integration
@link(REQ-0022, REQ-0008, REQ-0009, REQ-0007, REQ-0017)
@status(done)

18 November 2026. The installed consumer exercises cancellation inside a host callback and retains
a device fault through `catch`. Runtime tests exercise empty infinite loops,
long expressions, collection scans, frame limits and cumulative allowances.
PLN-0017 will qualify its own binary operations against the same public contract.

**Entry:** PLN-0010; reviewed mcuemu dispatch-cancel/catch semantics.

**Deliverable:** Add public cooperative cancellation and completion/error interfaces with shared nested budgets and host-visible cumulative-work accounting/control. Define common work/storage charging and cancellation checkpoints for the expression/collection/frame implementations in PLN-0013/0014 and binary work in PLN-0017. Bound temporary growth and aggregate retained script state/procedure bodies. Replace the need for consumer-side interpreter source patches without coupling the runtime to emulator fault types.

**Completion gate:** Cancellation boundaries, nested catch/completion, host errors and bounded callbacks pass independent tests. Catch, callback return and instance replacement cannot reset host failure state or refund a host run allowance. Verify accounting across repeated invocations; each later language leaf tests expensive operations and storage saturation against this contract before advertising its capability. Retained bootstrap cases pass, and blocking native callbacks retain their explicit cooperative limitation.

### PLN-0012 Provide structured invocation and isolated embedding lifecycle
@link(REQ-0023, REQ-0010, REQ-0017)
@status(done)

18 November 2026. The digital and sampled fixtures invoke Tcl while the scenario interpreter is
active. Continuous and sliced runs agree on updates and reads. A generation
check discards a replaced instance's queued event while its peer continues.
Callback faults reach the host before its next read. Direct invocation, data
boundaries and rejected reentry are also covered. [Embedding tests](../tests/embedding.cpp)

**Entry:** PLN-0010/0011; existing command registry and instance ownership.

**Deliverable:** Expose direct command/procedure invocation with structured values and source context, isolated user state and explicit reentry/teardown rules. Supply a test host with a scenario interpreter and separate model interpreters on one owner thread. Permit dispatch to an inactive model during a scenario's host run command; reject direct or indirect reentry into an active instance. The host owns scheduling, timely deferral, lifecycle generations and cumulative run/event budgets. Model callbacks do not recursively advance time.

**Completion gate:** Injection-looking arguments remain data, independent instances do not share state, active mutation/reentry rejects and stale instance-generation work cannot invoke a replacement interpreter. While a scenario runs toward tick 100, a model update at tick 10 affects an observation at tick 12 before the run returns; whole/sliced runs agree. Callback failure is reported before a dependent observation proceeds. Reset/destroy one instance with queued work and preserve peers. Initial cases use registered commands; procedure/state cases run when PLN-0014 supplies that capability, and binary forms are qualified separately by PLN-0017.

### PLN-0013 Complete automation collection and expression operations
@link(REQ-0021, REQ-0005, REQ-0006, REQ-0008, REQ-0018)
@status(done)

18 November 2026. The versioned automation profile implements list mutation, nested dictionaries,
lazy expressions, checked integer and binary64 arithmetic and the ten required
math functions. Independent and Tcl 9.0.1 cases cover exact comparison above
2^53, masks, floor division, growth limits, non-finite results, storage and
cancellation. [Runtime tests](../tests/automation.cpp) · [Reference cases](../tests/differential.py)

**Entry:** PLN-0010 and the PLN-0011 resource/cancellation contract; parser/value forms used by the selected profile, developed with existing PLN-0002/0004.

**Deliverable:** Implement the REQ-0021 list/dict mutation/query, numeric/string comparison, integer arithmetic/bitwise/shift, double-precision arithmetic and named math-function minimum. Preserve exact signed/nonnegative 64-bit inputs, explicit integer-growth and conversion rules, lazy evaluation and bounded operation/storage costs. Publish supported indices, nested keys, floating-point errors and numerical tolerances. Retain unsupported-form diagnostics and the remaining full-language inventory.

**Completion gate:** Independent boundary/round-trip/numeric cases and reference Tcl comparisons pass for every declared form. Include timestamps above 2^53, 64-bit masks/shifts, integer growth, floating-point conversion/non-finite/error cases, saturation and sampled-model calculations. Expensive expressions/collection operations and aggregate growth stop at declared work/storage limits with cooperative cancellation. No overflow approximation or hidden host evaluation supplies missing semantics.

### PLN-0014 Complete reusable procedures and bounded control flow
@link(REQ-0021, REQ-0004, REQ-0007, REQ-0008, REQ-0011, REQ-0018)
@status(done)

18 November 2026. Fixed-argument procedures, scoped state, `if`, single-variable `foreach` and
bounded `while` now run parameter sweeps and both Tcl models. The actuator
produces ADC codes 2048, 3071 and 4095. Its reset, sliced runs, faults and
independent fractional-gain calculation pass alongside the digital fixture.
[Actuator](../examples/sampled-actuator/actuator.tcl) · [Timing expectations](../tests/check_actuator.cmake)

**Entry:** PLN-0011/0013 and scoped frame work coordinated with PLN-0003/0005.

**Deliverable:** Implement the profile's if/foreach/proc/return, break/continue, argument/local-frame and nested completion semantics. Support persistent root-qualified global model state and dictionary mutation across procedure invocations, alongside scalar unset and independent local frames. Preserve shared budgets and host cancellation across bodies, recursion and repeated host callbacks; bound retained procedure/state storage.

**Completion gate:** Parameter sweeps, the debounced input and the sampled actuator/sensor each pass independent/reference cases with two differently parameterized instances. State survives repeated callbacks; resetting one instance does not affect its peer. Include delayed output, saturation/feedback, lazy branches, return/break/continue/errors, invalid calls, recursion, infinite-loop cancellation and retained state/procedure limit exhaustion. Re-run PLN-0012 timing and failure cases using procedures. Test-host results do not claim real guest GPIO/ADC effects.

### PLN-0015 Publish installed embedding capabilities for consumer migration
@link(REQ-0024, REQ-0019, REQ-0020, REQ-0022, REQ-0023)
@status(done)

18 November 2026. The 0.4.0 installation exports `mcutcl.automation/1` and the four embedding
capabilities. On macOS arm64 a standalone consumer builds from a relocated
package, runs both model projects, exercises cancellation and catch, and rejects
an unavailable binary profile. Migration instructions and expected port traces
are available for mcuemu PLN-0604/0605. [Installed consumer](../tests/installed/main.cpp) · [Handoff](../docs/model-authoring.md#installed-package-and-emulator-handoff)

**Entry:** PLN-0011/0012 for the embedding profile; automation capabilities are advertised only after PLN-0013/0014.

**Deliverable:** Export supported headers, package/profile identities and feature negotiation for the qualified state/numeric/value/resource contracts. Keep early embedding, digital/analog automation and later byte-oriented capabilities separately identifiable. Qualify an external consumer and coordinate mcuemu PLN-0604 to remove its source-layout-dependent staging. Hand the concrete model/timing fixtures to mcuemu PLN-0605 for actual guest integration.

**Completion gate:** A standalone host builds with only the installed package, rejects missing capabilities before evaluation, and reproduces the consumer's cancellation/catch cases through public services. Negotiation distinguishes unavailable automation/binary forms and never treats integer-list transport as binary support. Publish the early embedding artifact so mcuemu PLN-0604 can start without waiting for model-language or binary completion. The actual mcuemu migration is verified in PLN-0016; publishing this package cannot depend on that migration. State the tested host and version.

### PLN-0016 Accept the reusable automation and embedding increment
@link(REQ-0021, REQ-0022, REQ-0023, REQ-0024)
@status(in_progress)

18 November 2026. The [model guide](../docs/model-authoring.md) reconciles the
interpreter tests, reference corpus, installed consumption and expected port
traces. Acceptance still needs guest GPIO and DAC/PWM-to-ADC observations
from mcuemu PLN-0604/0605. Its current core-tcl/1 binding pins 0.2.0.

**Entry:** PLN-0010–0015; mcuemu PLN-0604 for the actual public-API migration and PLN-0605 for the script-model consumer checks. The basic two-board starter needs neither gate.

**Deliverable:** Reconcile profile inventory, independent/reference results, installed consumption and the concrete digital/analog model fixtures. Publish migration guidance from bootstrap plus consumer patches to supported features and document the separate byte-oriented follow-on.

**Completion gate:** Every REQ-0021–0024 acceptance obligation has identified evidence. The mcuemu PLN-0605 consumer demonstrates delayed/debounced GPIO and guest DAC or PWM output driving a sampled model whose feedback reaches the actual ADC model, with declared fidelity and numerical tolerances. Both fixtures cover two isolated instances, queued-work reset/destruction, timely feedback during a run, cancellation/exhaustion and whole/sliced equivalence. Preserve host-owned typed ports, units, event ordering and failure policy. Legacy bootstrap behavior and strict unsupported paths remain covered. PLN-0017 is not an entry or completion dependency; this parent does not mark full Tcl 9 or host-family requirements complete.

## PLN-0017 Qualify the byte-oriented modeling profile
@link(REQ-0025, REQ-0014, REQ-0005, REQ-0006, REQ-0008, REQ-0018, REQ-0020, REQ-0023, REQ-0024)
@status(planned)

**Entry:** Profile design from PLN-0010, public invocation/accounting and the automation capabilities from PLN-0011–0015, plus scoped binary-value work coordinated with PLN-0002. This independent follow-on does not gate PLN-0016 or require its actual-consumer migration to finish first.

**Deliverable:** Implement the enumerated Tcl 9 binary format/scan forms, lossless host byte conversion, collection/procedure round trips, byte length/slicing and explicit endian/signed-field interpretation from REQ-0025. Integrate work/storage limits and advertise a distinct installed capability. Preserve validated 0..255 integer-list adapters for earlier profiles without advertising binary support. Supply a byte-register responder using a test-host transport; model packaging, actual bus attachment and protocol fidelity remain mcuemu-owned.

**Completion gate:** Independent/reference cases cover every byte value, NUL, invalid UTF-8 payloads, substitution-looking data, endian/signed-field boundaries, malformed/truncated payloads and resource exhaustion. Two responder instances retain separate state, deliver delayed replies and invalidate queued work on reset/destruction under host time. An installed consumer negotiates supported/unsupported binary capability and invokes procedures with byte values without constructing source text. Report test-host transport limits; real UART/SPI/I2C/CAN attachment requires separately identified mcuemu transport/guest evidence and is not inferred from this language profile.
