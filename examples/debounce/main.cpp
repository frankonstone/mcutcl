#include <mcutcl/profile.hpp>

#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>

namespace {
using Words = std::span<const std::string>;
using mcutcl::Interpreter;
using mcutcl::Result;

std::unexpected<mcutcl::Error> host_error(std::string message) {
    return std::unexpected(mcutcl::Error{mcutcl::ErrorCode::host, std::move(message), {}});
}

int report(const mcutcl::Error& error) {
    std::cerr << error.source << ':' << error.line << ':' << error.column << ": "
              << mcutcl::error_name(error.code) << ": " << error.message << '\n';
    return 1;
}

std::optional<unsigned> ticks(Words words) {
    if (words.size() != 2) {
        return {};
    }
    unsigned value{};
    const auto& arg = words[1];
    auto end = arg.data() + arg.size();
    auto [next, ec] = std::from_chars(arg.data(), end, value);
    if (ec != std::errc{} || next != end || value > 100) {
        return {};
    }
    return value;
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: mcutcl_debounce MODEL.tcl [deadline ...]\n";
        return 2;
    }
    constexpr std::array<std::string_view, 3> needs{"mcutcl.fixed-proc/1", "mcutcl.invoke/1",
                                                    "mcutcl.run-control/1"};
    if (auto ready = mcutcl::runtime_profile().require(needs); !ready) {
        return report(ready.error());
    }
    std::ifstream file{argv[1], std::ios::binary | std::ios::ate};
    const std::streamoff length = file.tellg();
    if (length < 0 || length > 1024 * 1024) {
        std::cerr << "Cannot read model file\n";
        return 2;
    }
    std::string source(static_cast<std::size_t>(length), '\0');
    if (!file.seekg(0).read(source.data(), length)) {
        std::cerr << "Incomplete model file\n";
        return 2;
    }

    unsigned now = 0;
    std::optional<unsigned> timer;
    std::string pin = "0";
    mcutcl::RunControl run{{.commands = 256}};
    Interpreter model;
    auto registered =
        model.register_command("restart_timer", [&](Interpreter&, Words words) -> Result {
            auto delay = ticks(words);
            if (!delay || *delay == 0) {
                return host_error("timer delay must be 1..100 ticks");
            }
            timer = now + *delay;
            return std::string{};
        });
    if (!registered) {
        return report(registered.error());
    }
    registered = model.register_command("drive_pin", [&](Interpreter&, Words words) -> Result {
        if (words.size() != 2 || (words[1] != "0" && words[1] != "1")) {
            return host_error("expected a digital level");
        }
        pin = words[1];
        std::cout << now << " drive " << pin << '\n';
        return pin;
    });
    if (!registered) {
        return report(registered.error());
    }
    auto loaded = model.evaluate(source, argv[1], &run);
    if (!loaded) {
        return report(loaded.error());
    }
    auto initialized = model.invoke(std::array<std::string, 2>{"init", "4"}, "<init>", &run);
    if (!initialized) {
        return report(initialized.error());
    }

    Interpreter scenario;
    registered = scenario.register_command("advance_to", [&](Interpreter&, Words words) -> Result {
        auto deadline = ticks(words);
        if (!deadline || *deadline < now) {
            return host_error("expected a monotonic deadline up to 100");
        }
        while (now < *deadline) {
            ++now;
            if (now == 2 || now == 4 || now == 6) {
                const std::array<std::string, 2> event{"input_edge", now == 4 ? "0" : "1"};
                if (auto result = model.invoke(event, "<input edge>"); !result) {
                    return result;
                }
            }
            if (timer == now) {
                timer.reset();
                if (auto result =
                        model.invoke(std::array<std::string, 1>{"timer_elapsed"}, "<timer>");
                    !result) {
                    return result;
                }
            }
            if (now == 12) {
                std::cout << now << " sample " << pin << '\n';
            }
        }
        return std::to_string(now);
    });
    if (!registered) {
        return report(registered.error());
    }
    if (argc == 2) {
        auto result = scenario.evaluate("advance_to 100", "scenario.tcl", &run);
        if (!result) {
            return report(result.error());
        }
    } else {
        for (int i = 2; i < argc; ++i) {
            const std::array<std::string, 2> slice{"advance_to", argv[i]};
            if (auto result = scenario.invoke(slice, "<slice>", &run); !result) {
                return report(result.error());
            }
        }
    }
    return 0;
}
