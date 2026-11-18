#include <mcutcl/interpreter.hpp>
#include <mcutest/mcutest.hpp>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>

using namespace mcutcl;
namespace {
bool stopped(const Result& result, ErrorCode code) {
    return !result && result.error().code == code;
}
}
TEST(automation, integer_ticks_masks_and_floor_division_are_exact, "REQ-0006", "REQ-0021") {
    Interpreter t;
    ASSERT_TRUE(t.evaluate("set tick 9007199254740993; expr {$tick+1}") == "9007199254740994");
    ASSERT_TRUE(t.evaluate("expr {0xffffffffffffffff ^ (1 << 63)}") == "9223372036854775807");
    ASSERT_TRUE(t.evaluate("expr {wide(0xffffffffffffffff)}") == "-1");
    ASSERT_TRUE(t.evaluate("expr {-9223372036854775808 / -1}") == "9223372036854775808");
    ASSERT_TRUE(t.evaluate("list [expr {-7/3}] [expr {-7%3}] [expr {7%-3}] [expr {-3>>1}]") ==
                "-3 2 -2 -2");
    ASSERT_TRUE(t.evaluate("expr {$tick > 9007199254740992.0 && $tick < 9007199254740994.0}") ==
                "1");
    ASSERT_TRUE(t.evaluate("expr {0x8000_0000_0000_0000 - 1}") == "9223372036854775807");
    for (auto expression : {"18446744073709551616", "18446744073709551615+1", "1<<64",
                            "~0xffffffffffffffff", "0xffffffffffffffff ^ -1 >> 64", "2**64"}) {
        ASSERT_TRUE(
            stopped(t.invoke(std::array<std::string, 2>{"expr", expression}), ErrorCode::limit));
    }
    ASSERT_TRUE(stopped(t.evaluate("catch {expr {1<<64}}; set wrong yes"), ErrorCode::limit));
    ASSERT_FALSE(t.variable("wrong").has_value());
}
TEST(automation, real_conversions_domains_and_string_ordering, "REQ-0006", "REQ-0021") {
    Interpreter t;
    ASSERT_TRUE(t.evaluate("list [expr {round(-2.5)}] [expr {floor(-1.1)}] [expr {ceil(0.1)}] "
                           "[expr {wide(-1.9)}]") == "-3 -2.0 1.0 -1");
    ASSERT_TRUE(t.evaluate("expr {max(-2, min(2.5, abs(-3.0)))}") == "2.5");
    ASSERT_TRUE(t.evaluate("expr {sqrt(4.0) + exp(0.0)}") == "3.0");
    ASSERT_TRUE(t.evaluate("expr {double(9007199254740993)}") == "9007199254740992.0");
    ASSERT_TRUE(t.evaluate("list [expr {1.0/0.0}] [expr {-1.0/0.0}] [expr {NaN != 7}]") ==
                "Inf -Inf 1");
    for (auto e : {"sqrt(-1)", "0.0/0.0", "wide(Inf)", "1/0", "1.5%1", "1 << -1"}) {
        ASSERT_TRUE(stopped(t.invoke(std::array<std::string, 2>{"expr", e}), ErrorCode::script));
    }
    ASSERT_TRUE(t.evaluate("list [expr {02 eq 2}] [expr {02 == 2}] [expr {{海} gt {a}}]") ==
                "0 1 1");
    ASSERT_TRUE(stopped(t.evaluate("expr {sin(0)}"), ErrorCode::unsupported));
}
TEST(automation, lazy_expressions_validate_syntax_and_preserve_completions, "REQ-0003", "REQ-0006",
     "REQ-0007", "REQ-0018") {
    Interpreter t;
    ASSERT_TRUE(t.evaluate("set hit 0; expr {0 && [set hit 1] || 1}; expr {1 ? $hit : $absent}") ==
                "0");
    ASSERT_TRUE(t.evaluate("expr {0 ? [error wrong] : (1 ? [set hit 3] : [error later])}") == "3");
    ASSERT_TRUE(stopped(t.evaluate("expr {[set hit 9] + (}", "lazy.tcl"), ErrorCode::syntax));
    ASSERT_TRUE(t.variable("hit") == "3");
    ASSERT_TRUE(t.evaluate("set data {[set hit 10]}; expr {$data}") == "[set hit 10]");
    ASSERT_TRUE(t.variable("hit") == "3");
    ASSERT_TRUE(t.evaluate("proc f {} {expr {1 + [return early]}; error wrong}; f") == "early");
    ASSERT_TRUE(t.evaluate("list [catch {expr {[break]}}] [catch {expr {[continue]}}]") == "3 4");
    auto nested = t.evaluate("expr {0 || [error fault]}", "sample.tcl");
    ASSERT_TRUE(stopped(nested, ErrorCode::script));
    ASSERT_EQ(nested.error().source, "sample.tcl::<expr>");
}
TEST(automation, dictionary_and_list_writes_commit_after_validation, "REQ-0004", "REQ-0005",
     "REQ-0008", "REQ-0021") {
    Interpreter t;
    ASSERT_TRUE(t.evaluate("dict create a 1 b 2 a 3") == "a 3 b 2");
    ASSERT_TRUE(t.evaluate("proc setpin {key v} {dict set ::state pins $key $v}; setpin {a b} "
                           "{[bad]}; dict get $state pins {a b}") == "[bad]");
    ASSERT_TRUE(t.evaluate("dict unset state pins {a b}; dict exists $state pins") == "1");
    ASSERT_TRUE(t.evaluate("set d {a malformed}; catch {dict set d a b 1}; set d") ==
                "a malformed");
    ASSERT_TRUE(t.evaluate("dict exists $d a b") == "0");
    ASSERT_TRUE(
        t.evaluate("set l {{a b} c}; lset l {0 1} {a b}; lappend l z; lindex [lindex $l 0] 1") ==
        "a b");
    ASSERT_TRUE(t.evaluate("catch {lset l 5 x}; llength $l") == "3");
    ASSERT_TRUE(t.evaluate("lset l 3 more; lindex $l 3") == "more");
    ASSERT_TRUE(t.evaluate("set invalid \\{; catch {lappend invalid}") == "1");
    Interpreter small{{.state_bytes = 15}};
    ASSERT_TRUE(small.evaluate("dict set d a 1") == "a 1");
    ASSERT_TRUE(stopped(small.evaluate("dict set d key toolargevalue"), ErrorCode::limit));
    ASSERT_TRUE(small.variable("d") == "a 1");
    Interpreter shallow{{.nesting = 2}};
    ASSERT_TRUE(stopped(shallow.evaluate("dict set d a b c v"), ErrorCode::limit));
    ASSERT_FALSE(shallow.variable("d").has_value());
}
TEST(automation, parameter_sweeps_loops_and_recursion_use_the_callers_frame, "REQ-0007", "REQ-0008",
     "REQ-0011", "REQ-0021") {
    Interpreter t;
    ASSERT_TRUE(
        t.evaluate(
            "proc sweep {values} {set out {}; foreach v $values {if {$v < 0} continue; if {$v > 3} "
            "break; lappend out [expr {$v*2}]}; return $out}; sweep {-1 0 1 2 3 4 2}") ==
        "0 2 4 6");
    ASSERT_FALSE(t.variable("v").has_value());
    ASSERT_TRUE(t.evaluate("proc factorial {n} {if {$n <= 1} {return 1}; expr {$n * [factorial "
                           "[expr {$n-1}]]}}; factorial 5") == "120");
    ASSERT_TRUE(t.evaluate("set outer 0; foreach i {1 2 3} {while true {set outer [expr "
                           "{$outer+1}]; break}}; set outer") == "3");
    ASSERT_TRUE(t.evaluate("if 0 then {error bad} elseif 1 then {set x good} else {error bad}") ==
                "good");
    ASSERT_TRUE(t.evaluate("catch {while 1 {error stop}} message; set message") == "stop");
    ASSERT_TRUE(stopped(t.evaluate("foreach {x y} {1 2} {}"), ErrorCode::unsupported));
}
TEST(automation, empty_loops_and_expression_work_exhaust_shared_run, "REQ-0008", "REQ-0021",
     "REQ-0022") {
    Interpreter t;
    RunControl run{{.commands = 5}};
    ASSERT_TRUE(stopped(t.evaluate("catch {while true {}}; set wrong yes", "loop", &run),
                        ErrorCode::limit));
    ASSERT_EQ(run.remaining().commands, 0U);
    ASSERT_FALSE(t.variable("wrong").has_value());
    RunControl work{{.commands = 100, .work_bytes = 22}};
    ASSERT_TRUE(stopped(t.invoke(std::array<std::string, 2>{"expr", "1+2+3+4"}, "equation", &work),
                        ErrorCode::limit));
    ASSERT_TRUE(stopped(t.evaluate("expr 1", "again", &work), ErrorCode::limit));
    unsigned polls = 0;
    RunControl stop{{}, [&] { return ++polls == 60; }};
    ASSERT_TRUE(stopped(t.evaluate("catch {while 1 {}}; set wrong yes", "loop", &stop),
                        ErrorCode::cancelled));
    ASSERT_FALSE(t.variable("wrong").has_value());
    polls = 0;
    RunControl scan{{}, [&] { return ++polls == 16; }};
    const std::string long_literal = std::string(12000, '0') + "1";
    auto cancelled = t.invoke(std::array<std::string, 2>{"expr", long_literal}, "scan", &scan);
    ASSERT_TRUE(stopped(cancelled, ErrorCode::cancelled));
    ASSERT_TRUE(cancelled.error().offset > 0 && cancelled.error().offset < long_literal.size());
}
TEST(automation, collection_normalization_and_mutation_share_work_and_cancellation, "REQ-0005",
     "REQ-0008", "REQ-0022") {
    Interpreter t;
    std::string dictionary;
    for (unsigned i = 0; i < 100; ++i) {
        dictionary += "pin" + std::to_string(i) + " 1 ";
    }
    ASSERT_TRUE(t.set_variable("state", dictionary).has_value());
    unsigned probes = 0;
    RunControl stop{{}, [&] { return ++probes == 45; }};
    ASSERT_TRUE(stopped(
        t.invoke(std::array<std::string, 5>{"dict", "set", "state", "new", "2"}, "dict", &stop),
        ErrorCode::cancelled));
    ASSERT_TRUE(t.variable("state") == dictionary);
    RunControl work{{.work_bytes = 400}};
    ASSERT_TRUE(
        stopped(t.evaluate("catch {dict exists $state missing}; set wrong yes", "exists", &work),
                ErrorCode::limit));
    ASSERT_FALSE(t.variable("wrong").has_value());
}
TEST(models, sampled_feedback_matches_independent_values_and_stops_before_the_next_publish,
     "REQ-0021", "REQ-0022", "REQ-0023") {
    std::ifstream fixture{MCUTCL_ACTUATOR_FIXTURE};
    ASSERT_TRUE(fixture.is_open());
    const std::string source{std::istreambuf_iterator<char>(fixture), {}};
    Interpreter model;
    double level = 0;
    unsigned code = 0, calls = 0;
    ASSERT_TRUE(
        model
            .register_command(
                "publish_adc",
                [&](Interpreter&, std::span<const std::string> words) -> Result {
                    if (words.size() != 3) {
                        return std::unexpected(Error{ErrorCode::host, "invalid sample", {}});
                    }
                    const auto parse = [](const std::string& s, auto& n) {
                        const auto end = s.data() + s.size();
                        auto [p, ec] = std::from_chars(s.data(), end, n);
                        return ec == std::errc{} && p == end;
                    };
                    if (!parse(words[1], level) || !parse(words[2], code)) {
                        return std::unexpected(Error{ErrorCode::host, "invalid sample number", {}});
                    }
                    ++calls;
                    return std::string{};
                })
            .has_value());
    ASSERT_TRUE(model.evaluate(source, "actuator.tcl").has_value());
    ASSERT_TRUE(model.evaluate("init 0.1; drive 1.0; sample; sample; sample").has_value());
    ASSERT_TRUE(std::abs(level - 0.271) < 1e-12);
    ASSERT_EQ(code, 1110U);
    ASSERT_EQ(calls, 3U);
    RunControl short_run{{.commands = 4}};
    ASSERT_TRUE(stopped(model.invoke(std::array<std::string, 1>{"sample"}, "expired", &short_run),
                        ErrorCode::limit));
    RunControl cancel{{}, [] { return true; }};
    ASSERT_TRUE(stopped(model.invoke(std::array<std::string, 1>{"sample"}, "cancel", &cancel),
                        ErrorCode::cancelled));
    ASSERT_EQ(calls, 3U);
    ASSERT_TRUE(model.evaluate("drive -30; sample").has_value());
    ASSERT_TRUE(std::abs(level) < 1e-12);
    ASSERT_EQ(code, 0U);
}
