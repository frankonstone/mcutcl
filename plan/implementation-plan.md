# mcutcl implementation plan

Plan status is metadata, not compatibility evidence. The initial implementation
delivers partial parsing/substitution/scalar/embedding behavior only.

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
