#include <mcutcl/profile.hpp>
#include <array>
#include <iostream>

int main() {
    using namespace mcutcl;
    const auto profile = runtime_profile();
    constexpr std::array<std::string_view, 5> required{"mcutcl.invoke/1", "mcutcl.run-control/1",
                                                       "mcutcl.catch/1", "mcutcl.fixed-proc/1",
                                                       "mcutcl.automation/1"};
    if (!profile.require(required) ||
        profile.require(std::array<std::string_view, 1>{"mcutcl.binary/1"})) {
        std::cerr << "unexpected installed capabilities\n";
        return 1;
    }
    Interpreter t;
    bool fault = false, cancel = false;
    auto bound = t.register_command("fault", [&](Interpreter&, auto) -> Result {
        fault = true;
        return std::unexpected(Error{ErrorCode::host, "device fault", "device"});
    });
    if (!bound || t.evaluate("catch {fault} message; set message") != "device fault" || !fault ||
        !t.first_caught_error() || t.first_caught_error()->code != ErrorCode::host) {
        std::cerr << "caught host fault was lost\n";
        return 1;
    }
    bound = t.register_command("stop", [&](Interpreter&, auto) -> Result {
        cancel = true;
        return std::string{};
    });
    RunControl run{{}, [&] { return cancel; }};
    auto stopped = t.evaluate("catch {stop; set later 1}", "stop.tcl", &run);
    if (!bound || stopped || stopped.error().code != ErrorCode::cancelled || t.variable("later") ||
        !fault) {
        std::cerr << "cancellation failed to stop evaluation\n";
        return 1;
    }
    return 0;
}
