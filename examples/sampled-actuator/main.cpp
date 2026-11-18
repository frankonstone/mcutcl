#include <mcutcl/profile.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <optional>

namespace {
using mcutcl::Interpreter;
using mcutcl::Result;
using Words = std::span<const std::string>;

std::unexpected<mcutcl::Error> failure(std::string message) {
    return std::unexpected(mcutcl::Error{mcutcl::ErrorCode::host, std::move(message), {}});
}
int report(const mcutcl::Error& error) {
    std::cerr << error.source << ':' << error.line << ':' << error.column << ": "
              << mcutcl::error_name(error.code) << ": " << error.message << '\n';
    return 1;
}
template <class T> std::optional<T> number(std::string_view s) {
    T value{};
    const auto end = s.data() + s.size();
    auto [next, ec] = std::from_chars(s.data(), end, value);
    if (ec != std::errc{} || next != end) {
        return {};
    }
    return value;
}
struct Model {
    std::optional<Interpreter> interpreter;
    unsigned generation{};
    unsigned adc{};
};
struct Sample {
    unsigned due;
    std::size_t model;
    unsigned generation;
};
class Bench final {
public:
    Bench(std::string source, std::string path, bool reset)
        : source_(std::move(source)), path_(std::move(path)), reset_(reset) {}
    Result initialize(std::size_t id) {
        auto& model = models_[id];
        auto& instance = model.interpreter.emplace();
        ++model.generation;
        model.adc = 0;
        auto bound = instance.register_command(
            "publish_adc", [this, id](Interpreter&, Words words) -> Result {
                if (words.size() != 3) {
                    return failure("expected normalized position and ADC code");
                }
                auto position = number<double>(words[1]);
                auto adc = number<unsigned>(words[2]);
                if (!position || !std::isfinite(*position) || *position < 0 || *position > 1 ||
                    !adc || *adc > 4095) {
                    return failure("sample outside the configured ADC range");
                }
                models_[id].adc = *adc;
                std::cout << now_ << " sample " << (id ? 'B' : 'A') << ' ' << words[1] << ' '
                          << *adc << '\n';
                return std::string{};
            });
        if (!bound) {
            return std::unexpected(bound.error());
        }
        if (auto load = instance.evaluate(source_, path_, &run); !load) {
            return load;
        }
        if (auto init = instance.invoke(std::array<std::string, 2>{"init", id ? "0.25" : "0.5"},
                                        "<init>", &run);
            !init) {
            return init;
        }
        if (auto drive =
                instance.invoke(std::array<std::string, 2>{"drive", "1.0"}, "<drive>", &run);
            !drive) {
            return drive;
        }
        samples_.push_back({now_ + 10, id, model.generation});
        return std::string{};
    }
    Result advance(Words words) {
        if (words.size() != 2) {
            return failure("expected one deadline");
        }
        auto end = number<unsigned>(words[1]);
        if (!end || *end < now_ || *end > 100) {
            return failure("expected a monotonic deadline up to tick 100");
        }
        while (now_ < *end) {
            ++now_;
            if (reset_ && now_ == 18) {
                if (auto reset = initialize(0); !reset) {
                    return reset;
                }
            }
            if (now_ == 25) {
                for (auto& model : models_) {
                    if (auto r = model.interpreter->invoke(
                            std::array<std::string, 2>{"drive", "3.0"}, "<drive>");
                        !r) {
                        return r;
                    }
                }
            }
            while (!samples_.empty() && samples_.front().due == now_) {
                const auto sample = samples_.front();
                samples_.pop_front();
                auto& model = models_[sample.model];
                if (sample.generation != model.generation) {
                    continue;
                }
                if (auto r =
                        model.interpreter->invoke(std::array<std::string, 1>{"sample"}, "<sample>");
                    !r) {
                    return r;
                }
                samples_.push_back({now_ + 10, sample.model, sample.generation});
            }
            if (now_ == 12 || now_ == 22 || now_ == 32) {
                std::cout << now_ << " read " << models_[0].adc << ' ' << models_[1].adc << '\n';
            }
        }
        return std::to_string(now_);
    }
    mcutcl::RunControl run;

private:
    std::string source_, path_;
    bool reset_;
    unsigned now_{};
    std::array<Model, 2> models_;
    std::deque<Sample> samples_;
};
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: mcutcl_actuator MODEL.tcl [--reset] [deadline ...]\n";
        return 2;
    }
    constexpr std::array<std::string_view, 2> needs{"mcutcl.automation/1", "mcutcl.invoke/1"};
    if (auto ready = mcutcl::runtime_profile().require(needs); !ready) {
        return report(ready.error());
    }
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    const std::streamoff size = file.tellg();
    if (size < 0 || size > 1024 * 1024) {
        std::cerr << "Cannot read model file\n";
        return 2;
    }
    std::string source(static_cast<std::size_t>(size), '\0');
    if (!file.seekg(0).read(source.data(), size)) {
        std::cerr << "Incomplete model file\n";
        return 2;
    }
    const bool reset = argc > 2 && std::string_view(argv[2]) == "--reset";
    Bench bench{std::move(source), argv[1], reset};
    for (std::size_t id = 0; id < 2; ++id) {
        if (auto init = bench.initialize(id); !init) {
            return report(init.error());
        }
    }
    Interpreter scenario;
    auto bound = scenario.register_command(
        "advance_to", [&](Interpreter&, Words words) { return bench.advance(words); });
    if (!bound) {
        return report(bound.error());
    }
    const int first = reset ? 3 : 2;
    if (argc == first) {
        if (auto r = scenario.evaluate("advance_to 32", "scenario.tcl", &bench.run); !r) {
            return report(r.error());
        }
    } else {
        for (int i = first; i < argc; ++i) {
            if (auto r = scenario.invoke(std::array<std::string, 2>{"advance_to", argv[i]},
                                         "<slice>", &bench.run);
                !r) {
                return report(r.error());
            }
        }
    }
    return 0;
}
