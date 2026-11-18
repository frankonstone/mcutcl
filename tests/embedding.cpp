#include <mcutcl/list.hpp>
#include <mcutcl/profile.hpp>
#include <mcutest/mcutest.hpp>

#include <array>
#include <fstream>
#include <iterator>
#include <optional>

using namespace mcutcl;

namespace {
template <class T> bool fails_with(const std::expected<T, Error>& result, ErrorCode code) {
    return !result && result.error().code == code;
}
}

TEST(embedding, structured_arguments_remain_values_in_procedure_frames, "REQ-0011", "REQ-0021",
     "REQ-0023") {
    Interpreter model;
    ASSERT_TRUE(
        model
            .evaluate("proc update {level} {set local $level; set ::last $local; return $::last}",
                      "pin.tcl")
            .has_value());
    for (const std::string value :
         {"", "two words", "}", "[error boom]; set hacked yes", "$absent", "Grüße"}) {
        const auto result = model.invoke(std::array<std::string, 2>{"::update", value});
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(*result, value);
        ASSERT_TRUE(model.variable("last") == value);
    }
    const std::string nul{"a\0b", 3};
    ASSERT_TRUE(model.invoke(std::array<std::string, 2>{"update", nul}) == nul);
    const auto invalid =
        model.invoke(std::array<std::string, 2>{"update", std::string(1, char(0xff))}, "wire");
    ASSERT_TRUE(fails_with(invalid, ErrorCode::unsupported));
    ASSERT_EQ(invalid.error().source, "wire");
    ASSERT_TRUE(model.variable("last") == nul);
    ASSERT_FALSE(model.variable("level").has_value());
    ASSERT_FALSE(model.variable("local").has_value());
    ASSERT_FALSE(model.variable("hacked").has_value());
    ASSERT_TRUE(fails_with(model.invoke({}), ErrorCode::wrong_arguments));
}

TEST(embedding, catch_observes_completions_and_host_faults_keep_their_owner, "REQ-0007", "REQ-0009",
     "REQ-0022") {
    Interpreter instance;
    bool fault = false;
    const auto fault_bound = instance.register_command("fault", [&](Interpreter&, auto) -> Result {
        fault = true;
        return std::unexpected(Error{ErrorCode::host, "pin fault", "device"});
    });
    ASSERT_TRUE(fault_bound.has_value());
    const auto result = instance.evaluate(
        "proc exit_early {} {set ::wrong [return early]; error later}\n"
        "list [exit_early] [catch {return text} r] $r [catch break] [catch continue] "
        "[catch {catch {fault} why}] [catch {error second}]",
        "scenario.tcl");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, "early 2 text 3 4 0 1");
    ASSERT_FALSE(instance.variable("wrong").has_value());
    ASSERT_TRUE(instance.variable("why") == "pin fault");
    ASSERT_TRUE(fault);
    ASSERT_TRUE(instance.first_caught_error().has_value());
    ASSERT_EQ(instance.first_caught_error()->code, ErrorCode::host);
    ASSERT_EQ(instance.first_caught_error()->source, "device");
    ASSERT_TRUE(instance.evaluate("set next ok") == "ok");
    ASSERT_FALSE(instance.first_caught_error().has_value());
    ASSERT_TRUE(fault);
    ASSERT_TRUE(fails_with(instance.evaluate("catch {set wrong yes} result options"),
                           ErrorCode::unsupported));
    ASSERT_FALSE(instance.variable("wrong").has_value());
    ASSERT_TRUE(instance.evaluate("proc bad {} {set ok 1\nset x [break]}", "pin.tcl").has_value());
    auto escaped = instance.invoke(std::array<std::string, 1>{"bad"});
    ASSERT_FALSE(escaped.has_value());
    ASSERT_EQ(escaped.error().source, "pin.tcl::bad");
    ASSERT_EQ(escaped.error().line, 2U);
    ASSERT_EQ(escaped.error().column, 8U);
}

TEST(embedding, complete_argument_vector_is_bounded_before_dispatch, "REQ-0008", "REQ-0023") {
    Interpreter instance{{.value_bytes = 8, .words = 3, .argument_bytes = 8}};
    unsigned calls{};
    const auto pin_bound = instance.register_command("pin", [&](Interpreter&, auto) -> Result {
        ++calls;
        return std::string{};
    });
    ASSERT_TRUE(pin_bound.has_value());
    ASSERT_TRUE(instance.invoke(std::array<std::string, 3>{"pin", "ab", "cde"}).has_value());
    auto oversized = instance.invoke(std::array<std::string, 3>{"pin", "abc", "def"}, "event");
    ASSERT_FALSE(oversized.has_value());
    ASSERT_TRUE(fails_with(oversized, ErrorCode::limit));
    ASSERT_EQ(oversized.error().source, "event");
    ASSERT_TRUE(instance.set_variable("x", "abc").has_value());
    ASSERT_FALSE(instance.evaluate("pin $x $x").has_value());
    ASSERT_FALSE(instance.invoke(std::array<std::string, 4>{"pin", "", "", ""}).has_value());
    ASSERT_FALSE(
        instance.invoke(std::array<std::string, 3>{"pin", "ok", std::string(1, char(0xff))})
            .has_value());
    ASSERT_EQ(calls, 1U);
}

TEST(embedding, unset_releases_only_the_selected_frame, "REQ-0004", "REQ-0008", "REQ-0011") {
    Interpreter instance{{.variables = 2}};
    ASSERT_TRUE(instance.evaluate("set x outer; proc clear {x} {unset x; set y next; return $::x}")
                    .has_value());
    ASSERT_TRUE(instance.evaluate("clear local") == "outer");
    ASSERT_FALSE(instance.variable("y").has_value());
    ASSERT_TRUE(
        instance.evaluate("unset -nocomplain absent ::x; set first 1; set second 2").has_value());
    ASSERT_FALSE(instance.variable("x").has_value());
    ASSERT_FALSE(instance.evaluate("set third 3").has_value());
}

TEST(embedding, nested_evaluation_inherits_depth_and_preserves_source, "REQ-0008", "REQ-0009",
     "REQ-0022") {
    Interpreter instance{{.nesting = 4}};
    const auto body_bound = instance.register_command(
        "body", [](Interpreter& self, std::span<const std::string> words) -> Result {
            if (words.size() != 2) {
                return std::unexpected(Error{ErrorCode::wrong_arguments, "expected a body", {}});
            }
            auto completion = self.evaluate_nested(words[1], "body.tcl");
            if (!completion) {
                return std::unexpected(completion.error());
            }
            return std::to_string(static_cast<int>(completion->code)) + "/" + completion->value;
        });
    ASSERT_TRUE(body_bound.has_value());
    ASSERT_TRUE(instance.evaluate("body {return value}") == "2/value");
    auto error = instance.evaluate("body {set ok 1\nunknown}");
    ASSERT_FALSE(error.has_value());
    ASSERT_EQ(error.error().source, "body.tcl");
    ASSERT_EQ(error.error().line, 2U);
    ASSERT_TRUE(fails_with(instance.evaluate_nested("", "outside"), ErrorCode::reentrant));
    ASSERT_TRUE(instance.evaluate("proc recurse {} {body {recurse}}").has_value());
    ASSERT_TRUE(fails_with(instance.evaluate("catch {recurse}"), ErrorCode::limit));
    ASSERT_TRUE(instance.evaluate("set next ok") == "ok");
}

TEST(embedding, cancellation_prevents_substitution_and_propagates_out_of_catch, "REQ-0008",
     "REQ-0022") {
    Interpreter instance;
    bool stop = true;
    unsigned calls = 0;
    RunControl cancelled{{}, [&] { return stop; }};
    const auto native_bound =
        instance.register_command("native", [&](Interpreter& self, auto) -> Result {
            ++calls;
            stop = true;
            if (!self.stop_requested()) {
                return std::unexpected(Error{ErrorCode::host, "stop not visible", {}});
            }
            return std::string{"stopped"};
        });
    ASSERT_TRUE(native_bound.has_value());
    ASSERT_TRUE(fails_with(instance.evaluate("set x [native]", "pre.tcl", &cancelled),
                           ErrorCode::cancelled));
    ASSERT_EQ(calls, 0U);
    stop = false;
    ASSERT_TRUE(
        fails_with(instance.invoke(std::array<std::string, 1>{"native"}, "event", &cancelled),
                   ErrorCode::cancelled));
    ASSERT_EQ(calls, 0U);
    RunControl fresh{{}, [&] { return stop; }};
    auto result = instance.evaluate("catch {native} message; set late yes", "run.tcl", &fresh);
    ASSERT_TRUE(fails_with(result, ErrorCode::cancelled));
    ASSERT_EQ(result.error().source, "run.tcl::<catch>");
    ASSERT_EQ(calls, 1U);
    ASSERT_FALSE(instance.variable("message").has_value());
    ASSERT_FALSE(instance.variable("late").has_value());
}

TEST(embedding, dispatch_and_byte_budgets_survive_calls_and_replacement, "REQ-0008", "REQ-0022",
     "REQ-0023") {
    Interpreter first;
    RunControl commands{{.commands = 2}};
    auto denied = first.evaluate("catch {set x [set inner ok]}; set later no", "budget", &commands);
    ASSERT_TRUE(fails_with(denied, ErrorCode::limit));
    ASSERT_TRUE(first.variable("inner") == "ok");
    ASSERT_FALSE(first.variable("x").has_value());
    ASSERT_FALSE(first.variable("later").has_value());
    ASSERT_EQ(commands.remaining().commands, 0U);
    Interpreter replacement;
    ASSERT_TRUE(
        fails_with(replacement.evaluate("set x yes", "again", &commands), ErrorCode::limit));

    RunControl bytes{{.commands = 10, .work_bytes = 6}};
    ASSERT_TRUE(first.invoke(std::array<std::string, 3>{"set", "x", "v"}, "initial", &bytes) ==
                "v");
    ASSERT_EQ(bytes.remaining().work_bytes, 0U);
    ASSERT_TRUE(
        fails_with(replacement.invoke(std::array<std::string, 3>{"set", "x", "v"}, "next", &bytes),
                   ErrorCode::limit));
    ASSERT_FALSE(replacement.variable("x").has_value());
    ASSERT_TRUE(fails_with(bytes.consume_work(0), ErrorCode::limit));
}

TEST(embedding, storage_limits_release_frames_and_preserve_old_definitions_and_arguments,
     "REQ-0008", "REQ-0011", "REQ-0022") {
    Interpreter instance{{.variables = 1, .procedures = 1, .state_bytes = 50}};
    ASSERT_TRUE(instance.evaluate("proc f {x y} {set x}", "s").has_value());
    ASSERT_TRUE(fails_with(instance.evaluate("f a b"), ErrorCode::limit));
    ASSERT_TRUE(instance.evaluate("proc f {x} {set x}", "s").has_value());
    ASSERT_TRUE(instance.evaluate("f yes") == "yes");
    ASSERT_TRUE(fails_with(instance.evaluate("proc g {} {}", "s"), ErrorCode::limit));
    ASSERT_TRUE(fails_with(
        instance.invoke(std::array<std::string, 4>{"proc", "f", "", std::string(60, 'x')}),
        ErrorCode::limit));
    ASSERT_TRUE(instance.evaluate("f preserved") == "preserved");
    ASSERT_TRUE(
        fails_with(instance.evaluate("proc f {{x default}} {}", "s"), ErrorCode::unsupported));
    ASSERT_TRUE(fails_with(instance.evaluate("proc f args {}", "s"), ErrorCode::unsupported));
    ASSERT_TRUE(fails_with(instance.evaluate("proc set {} {}", "s"), ErrorCode::unsupported));
    ASSERT_TRUE(instance.evaluate("set global yes") == "yes");

    Interpreter args{{.state_bytes = 18}};
    ASSERT_TRUE(args.set_arguments("old", std::array<std::string, 1>{"v"}).has_value());
    ASSERT_TRUE(fails_with(args.set_arguments("longer", {}), ErrorCode::limit));
    ASSERT_TRUE(args.variable("argv0") == "old");
    ASSERT_TRUE(args.variable("argv") == "v");
    ASSERT_TRUE(args.set_arguments("new", {}).has_value());
}

TEST(embedding, direct_and_indirect_entry_share_limits_and_reject_suspended_owners, "REQ-0010",
     "REQ-0017", "REQ-0023") {
    Interpreter scenario, model;
    bool rejected = false;
    bool sampled = false;
    const auto update_bound = model.register_command("update", [&](Interpreter&, auto) -> Result {
        const auto direct = scenario.invoke(std::array<std::string, 1>{"run"});
        const auto nested = scenario.evaluate_nested("set wrong yes", "indirect");
        const auto write = scenario.set_variable("wrong", "yes");
        rejected = !direct && direct.error().code == ErrorCode::reentrant && !nested &&
                   nested.error().code == ErrorCode::reentrant && !write &&
                   write.error().code == ErrorCode::reentrant;
        return std::unexpected(Error{ErrorCode::host, "model failure", "pin"});
    });
    ASSERT_TRUE(update_bound.has_value());
    const auto run_bound = scenario.register_command("run", [&](Interpreter&, auto) -> Result {
        RunControl other;
        auto bypass = model.invoke(std::array<std::string, 1>{"update"}, "bypass", &other);
        if (bypass || bypass.error().code != ErrorCode::reentrant) {
            return std::string{"bypassed owner"};
        }
        auto update = model.invoke(std::array<std::string, 1>{"update"}, "tick 10");
        if (!update) {
            return update;
        }
        sampled = true;
        return std::string{"sample at tick 12"};
    });
    ASSERT_TRUE(run_bound.has_value());
    RunControl run{{.commands = 2}};
    const auto result = scenario.evaluate("run", "scenario", &run);
    ASSERT_TRUE(fails_with(result, ErrorCode::host));
    ASSERT_EQ(result.error().source, "pin");
    ASSERT_TRUE(rejected);
    ASSERT_FALSE(sampled);
    ASSERT_FALSE(scenario.variable("wrong").has_value());
    ASSERT_EQ(run.remaining().commands, 0U);
}

TEST(models, queued_work_uses_host_generations_and_two_independent_models, "REQ-0021", "REQ-0023") {
    std::ifstream file{MCUTCL_DEBOUNCE_FIXTURE};
    ASSERT_TRUE(file.is_open());
    const std::string source{std::istreambuf_iterator<char>{file}, {}};
    struct Timer {
        unsigned generation;
        unsigned due;
    };
    struct Slot {
        unsigned generation{};
        unsigned drives{};
        std::string level{"0"};
        std::optional<Timer> timer;
        std::optional<Interpreter> model;
    };
    std::array<Slot, 2> pins;
    unsigned tick = 6;
    RunControl run;
    const auto reset = [&](Slot& pin, unsigned delay) -> Result {
        ++pin.generation;
        auto& instance = pin.model.emplace();
        auto bound = instance.register_command(
            "restart_timer", [&pin, &tick, delay](Interpreter&, auto words) -> Result {
                if (words.size() != 2 || words[1] != std::to_string(delay)) {
                    return std::unexpected(Error{ErrorCode::host, "incorrect instance delay", {}});
                }
                pin.timer = Timer{pin.generation, tick + delay};
                return std::string{};
            });
        if (!bound) {
            return std::unexpected(bound.error());
        }
        bound = instance.register_command("drive_pin", [&pin](Interpreter&, auto words) -> Result {
            if (words.size() != 2) {
                return std::unexpected(Error{ErrorCode::host, "expected a level", {}});
            }
            ++pin.drives;
            pin.level = words[1];
            return pin.level;
        });
        if (!bound) {
            return std::unexpected(bound.error());
        }
        if (auto loaded = instance.evaluate(source, "debounce.tcl", &run); !loaded) {
            return loaded;
        }
        return instance.invoke(std::array<std::string, 2>{"init", std::to_string(delay)}, "init",
                               &run);
    };
    ASSERT_TRUE(reset(pins[0], 4).has_value());
    ASSERT_TRUE(reset(pins[1], 8).has_value());
    for (auto& pin : pins) {
        ASSERT_TRUE(pin.model->invoke(std::array<std::string, 2>{"input_edge", "1"}, "edge", &run)
                        .has_value());
        ASSERT_TRUE(pin.timer.has_value());
    }
    ASSERT_EQ(pins[0].timer->due, 10U);
    ASSERT_EQ(pins[1].timer->due, 14U);
    tick = 8;
    ASSERT_TRUE(reset(pins[0], 2).has_value());
    // Timers stay in the host queue across replacement. Only the matching
    // generation may deliver its event to the current interpreter.
    for (; tick <= 14; ++tick) {
        for (auto& pin : pins) {
            if (!pin.timer || pin.timer->due != tick) {
                continue;
            }
            const auto generation = pin.timer->generation;
            pin.timer.reset();
            if (generation == pin.generation) {
                ASSERT_TRUE(
                    pin.model->invoke(std::array<std::string, 1>{"timer_elapsed"}, "timer", &run)
                        .has_value());
            }
        }
    }
    ASSERT_EQ(pins[0].drives, 0U);
    ASSERT_EQ(pins[0].level, "0");
    ASSERT_EQ(pins[1].drives, 1U);
    ASSERT_EQ(pins[1].level, "1");
    ASSERT_TRUE(pins[0].model->variable("settle_ticks") == "2");
    ASSERT_TRUE(pins[1].model->variable("settle_ticks") == "8");
}

TEST(embedding, a_large_single_list_operation_can_stop_between_elements_or_inside_one, "REQ-0008",
     "REQ-0022") {
    unsigned probes = 0;
    RunControl decoding{{}, [&] { return ++probes == 3; }};
    const auto decoded = parse_list(std::string(4096, 'x'), {}, &decoding);
    ASSERT_TRUE(fails_with(decoded, ErrorCode::cancelled));
    ASSERT_TRUE(decoded.error().offset > 0 && decoded.error().offset < 4096);
    probes = 0;
    RunControl encoding{{}, [&] { return ++probes == 4; }};
    const auto encoded =
        make_list(std::array<std::string, 1>{std::string(4096, ' ')}, {}, &encoding);
    ASSERT_TRUE(fails_with(encoded, ErrorCode::cancelled));
    RunControl small{{.work_bytes = 7}};
    ASSERT_TRUE(fails_with(parse_list("one two three", {}, &small), ErrorCode::limit));
}

TEST(embedding, negotiation_rejects_missing_model_profiles_before_evaluation, "REQ-0020",
     "REQ-0024") {
    const auto profile = runtime_profile();
    ASSERT_TRUE(profile.require(std::array{profile.language, std::string_view{"mcutcl.invoke/1"}})
                    .has_value());
    ASSERT_TRUE(
        profile.require(std::array<std::string_view, 1>{"mcutcl.automation/1"}).has_value());
    for (const std::string_view missing :
         {"mcutcl.automation/2", "mcutcl.binary/1", "mcutcl.invoke/2"}) {
        const auto result = profile.require(std::array{missing});
        ASSERT_TRUE(fails_with(result, ErrorCode::unsupported));
        ASSERT_TRUE(result.error().message.find(missing) != std::string::npos);
    }
}
