#include <mcutcl/interpreter.hpp>
#include <mcutcl/profile.hpp>
#include <mcucli/mcucli.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

// @req REQ-0015
int main(int argc, char* argv[]) {
    mcucli::Application app{"mcutcl", "Project-owned Tcl 9 interpreter: initial documented subset"};
    const auto runtime = mcutcl::runtime_profile();
    app.set_version(runtime.version);
    const auto evaluate = app.add_command("eval", "Evaluate a source string and print its final result");
    const auto run = app.add_command("run", "Run a UTF-8 Tcl source file");
    const auto profile = app.add_command("profile", "Show compatibility and implementation status");
    const auto complete = app.add_command("complete", "Inspect supported syntax without evaluating it");
    if (!evaluate || !run || !profile || !complete) { return 3; }
    std::string_view script, path;
    const auto script_option = (*evaluate)->add_option("--script", "Tcl source", script);
    const auto path_option = (*run)->add_option("--file", "Tcl source file", path);
    const auto complete_option = (*complete)->add_option("--script", "Source to inspect", script);
    if (!script_option || !path_option || !complete_option) { return 3; }
    (*complete_option)->required();
    (*script_option)->required(); (*path_option)->required();
    const auto parsed = app.parse(argc, argv);
    if (!parsed) { std::cerr << parsed.error().detail << '\n'; return 2; }
    if (parsed->kind() == mcucli::ParseEventKind::version) { std::cout << app.version() << '\n'; return 0; }
    if (parsed->kind() == mcucli::ParseEventKind::help
        || (parsed->command() != (*evaluate)->id() && parsed->command() != (*run)->id() && parsed->command() != (*profile)->id() && parsed->command() != (*complete)->id())) {
        const auto help = app.help(parsed->command());
        if (!help) { return 3; } std::cout << *help; return 0;
    }
    if (parsed->command() == (*profile)->id()) {
        std::cout << "target: Tcl 9 core semantics\nimplementation: original C++ interpreter; no Tcl runtime dependency\n"
            "profile: " << runtime.language << "\ncapabilities:";
        for (const auto feature : runtime.capabilities) { std::cout << ' ' << feature; }
        std::cout << "\ncommands: set unset list llength lindex lappend lset dict (create/get/set/exists/unset)\n"
            "          expr if foreach (one variable) while proc (fixed arguments) return (plain) break continue catch error\n"
            "numbers: 64-bit integer magnitude, binary64, abs min max double wide round floor ceil sqrt exp\n"
            "pending: arbitrary precision, byte values, full Tcl 9 core, REPL\n";
        return 0;
    }
    if (parsed->command() == (*complete)->id()) {
        const auto checked = mcutcl::inspect_input(script, "<command>");
        if (checked.state == mcutcl::InputState::invalid) {
            std::cerr << mcutcl::error_name(checked.error->code) << ": " << checked.error->message << '\n'; return 1;
        }
        std::cout << (checked.state == mcutcl::InputState::complete ? "complete" : "incomplete") << '\n';
        return 0;
    }
    std::string owned;
    if (parsed->command() == (*run)->id()) {
        std::ifstream input{std::string(path), std::ios::binary | std::ios::ate};
        const auto length = input.tellg();
        if (!input || length < 0 || length > 1024 * 1024) { std::cerr << "cannot read source or source exceeds limit\n"; return 2; }
        input.seekg(0);
        owned.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (input.bad()) { std::cerr << "source read failed\n"; return 2; }
        script = owned;
    }
    mcutcl::Interpreter interpreter;
    const auto output = interpreter.register_command("puts", [](mcutcl::Interpreter&, std::span<const std::string> args) -> mcutcl::Result {
        if (args.size() != 2) { return std::unexpected(mcutcl::Error{mcutcl::ErrorCode::unsupported, "initial puts supports only one stdout argument", {}}); }
        std::cout << args[1] << '\n';
        if (!std::cout) { return std::unexpected(mcutcl::Error{mcutcl::ErrorCode::host, "stdout write failed", {}}); }
        return std::string{};
    });
    if (!output) { return 3; }
    const auto result = interpreter.evaluate(script, path.empty() ? "<command>" : path);
    if (!result) {
        const auto& error = result.error();
        std::cerr << error.source << ':' << error.line << ':' << error.column << ": "
            << mcutcl::error_name(error.code) << ": " << error.message << '\n';
        return 1;
    }
    if (parsed->command() == (*evaluate)->id()) { std::cout << *result << '\n'; }
    return 0;
}
