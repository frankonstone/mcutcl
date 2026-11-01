# Initial verification baseline

Recorded 2026-10-29 for `mcutcl.tcl9-bootstrap/0.1.0`.

- macOS arm64, Apple Clang 21.0.0, CMake 4.4.0, Ninja, C++23.
- 15 mcutest cases pass; the Release `mcutest.json` is produced by the real test
  executable. Three CTest checks pass, including successful/error CLI processes.
- The same original bootstrap sources pass AddressSanitizer/UBSan CTest checks.
- The explicit Tcl 9.0.1 reference corpus compares 83 selected scripts with zero
  mismatches in success/failure and successful UTF-8 result bytes. It does not
  establish complete syntax/core-command/error-message compatibility.
- A separately configured application builds against the installed
  `mcutcl::mcutcl` package and executes a registered native callback.
- Another installed-library probe links mcutcl and mcuemu, registers an original
  C++ command, runs the compiled C reset ELF for 100 functional ticks/instructions,
  and returns R0=42 through Tcl command/variable substitution. It is a development
  interoperability probe, not production emulator scripting bindings.
- The CLI's dynamic-library list contains system C++/system libraries and no Tcl
  runtime. The library itself uses original project C++ and the standard library.
- mcutrace CLI validation: 20 requirements, 8 outstanding work packages, zero
  validation errors and 13 warnings (11 missing-test, 2 missing-source-coverage).
  These gaps remain visible; no plan or full compatibility requirement is closed.

Linux/Windows, full Tcl 9 core semantics, complete command inventory, memory
allocation containment, cancellation, multiline REPL and asynchronous host
operation/resumption remain open. mcutrace configuration and its CLI producer
are present; a separate project-scoped MCP connection has not been configured.
