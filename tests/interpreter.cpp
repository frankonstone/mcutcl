#include <mcutcl/interpreter.hpp>
#include <mcutest/mcutest.hpp>
#include <array>
#include <string>

using namespace mcutcl;

TEST(parser, separators_comments_and_final_value, "REQ-0002") {
    Interpreter i;
    ASSERT_EQ(*i.evaluate(" # comment\n set x one;set y two\nset x"), "one");
    ASSERT_EQ(*i.evaluate(";\n #empty\n"), "");
    ASSERT_EQ(*i.variable("y"), "two");
    ASSERT_EQ(*i.evaluate("set x foo#bar"), "foo#bar");
}
TEST(parser, quoting_braces_and_escapes, "REQ-0002", "REQ-0003") {
    struct Vector { const char* source; const char* value; };
    constexpr Vector vectors[]{
        {"set x {a {b} c}", "a {b} c"}, {"set x {}", ""}, {"set x \"\"", ""},
        {"set x a\\ b", "a b"}, {"set x {a\\nb}", "a\\nb"},
        {"set x \"a\\nb\\tc\"", "a\nb\tc"}, {"set x {a\\{b}", "a\\{b"},
        {"set x a\"b", "a\"b"}, {"set x a{b", "a{b"}, {"set x ]", "]"},
        {"set x {a\\\n   b}", "a b"}, {"set x \"a\\\n   b\"", "a b"},
        {"set x a\\\n b", nullptr}};
    for (const auto& v : vectors) {
        Interpreter i;
        const auto r = i.evaluate(v.source);
        if (v.value == nullptr) { ASSERT_FALSE(r.has_value()); ASSERT_EQ(r.error().code, ErrorCode::wrong_arguments); }
        else { ASSERT_TRUE(r.has_value()); ASSERT_EQ(*r, v.value); }
    }
}
TEST(substitution, values_are_not_reparsed_or_split, "REQ-0003", "REQ-0004") {
    Interpreter i;
    ASSERT_TRUE(i.evaluate("set a {hello world; [unknown] $x}").has_value());
    ASSERT_EQ(*i.evaluate("set b $a"), "hello world; [unknown] $x");
    ASSERT_EQ(*i.evaluate("set b \"<$a>\""), "<hello world; [unknown] $x>");
    ASSERT_EQ(*i.evaluate("set b {$a}"), "$a");
    ASSERT_EQ(*i.evaluate("set b $"), "$");
}
TEST(substitution, nested_commands_left_to_right_and_empty_result, "REQ-0003") {
    Interpreter i;
    ASSERT_EQ(*i.evaluate("set result [set x a][set x b][set x c]"), "abc");
    ASSERT_EQ(*i.variable("x"), "c");
    ASSERT_EQ(*i.evaluate("set result [set x a; set x b]"), "b");
    ASSERT_EQ(*i.evaluate("set result [#empty\n]"), "");
    ASSERT_EQ(*i.evaluate("set result [set x [set y nested]]"), "nested");
}
TEST(parser, syntax_rejected_before_same_command_substitutions, "REQ-0002", "REQ-0009") {
    for (const auto* source : {"set x [set y changed] \"unterminated", "set x [set y changed;set z {unterminated]",
        "set x \"a\"b", "set x {a}b", "set x [set y x", "set x ${missing"}) {
        Interpreter i;
        ASSERT_TRUE(i.set_variable("y", "original").has_value());
        const auto result = i.evaluate(source, "test.tcl");
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error().code, ErrorCode::syntax);
        ASSERT_EQ(result.error().source, "test.tcl");
        ASSERT_EQ(*i.variable("y"), "original");
    }
}
TEST(parser, earlier_complete_commands_survive_later_syntax_error, "REQ-0002") {
    Interpreter i;
    const auto result = i.evaluate("set x first\nset x [set y changed]; set z \"unterminated");
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(*i.variable("x"), "changed");
    ASSERT_EQ(*i.variable("y"), "changed");
}
TEST(parser, continuation_in_comment_and_between_words, "REQ-0002", "REQ-0003", "REQ-0018") {
    Interpreter i;
    ASSERT_EQ(*i.evaluate("# continued\\\n set x wrong\nset x right"), "right");
    ASSERT_EQ(*i.evaluate("set\\\n x\\\n {two words}"), "two words");
    ASSERT_EQ(*i.evaluate("set {x}\\\n \"value\""), "value");
    ASSERT_EQ(*i.evaluate("set \"x\"\\\n {value}"), "value");
    ASSERT_EQ(*i.evaluate("set x {*}"), "*");
}
TEST(variables, empty_braced_and_root_qualified_names, "REQ-0004") {
    Interpreter i;
    ASSERT_EQ(*i.evaluate("set {} empty; set y ${}"), "empty");
    ASSERT_EQ(*i.evaluate("set {two words} value; set y ${two words}"), "value");
    ASSERT_EQ(*i.evaluate("::set ::x root; set y $::x"), "root");
    ASSERT_EQ(*i.variable("x"), "root");
    const auto missing = i.evaluate("set y $missing");
    ASSERT_FALSE(missing.has_value()); ASSERT_EQ(missing.error().code, ErrorCode::undefined_variable);
}
TEST(profile, unfinished_features_reject_explicitly, "REQ-0020") {
    for (const auto* s : {"set x \\x41", "set x \\u0041", "set x \\101", "set x $a(b)", "set a(b) x", "set a::b x", "set x {*}{one two}"}) {
        Interpreter i;
        const auto r = i.evaluate(s);
        ASSERT_FALSE(r.has_value()); ASSERT_EQ(r.error().code, ErrorCode::unsupported);
    }
    Interpreter i;
    const auto r = i.evaluate("expr {1 + 2}");
    ASSERT_FALSE(r.has_value()); ASSERT_EQ(r.error().code, ErrorCode::unknown_command);
}
TEST(native, typed_arguments_results_and_registration_policy, "REQ-0010") {
    Interpreter i;
    unsigned calls{};
    ASSERT_TRUE(i.register_command("capture", [&](Interpreter&, std::span<const std::string> args) -> Result {
        ++calls;
        if (args.size() != 3) { return std::unexpected(Error{ErrorCode::host, "expected two values", {}}); }
        return args[1] + "|" + args[2];
    }).has_value());
    ASSERT_EQ(*i.evaluate("capture {one two} [set x three]"), "one two|three");
    ASSERT_EQ(calls, 1U);
    ASSERT_FALSE(i.register_command("capture", [](Interpreter&, auto) -> Result { return std::string{}; }).has_value());
    ASSERT_FALSE(i.register_command("empty", {}).has_value());
    const auto bad = i.evaluate("capture x");
    ASSERT_FALSE(bad.has_value()); ASSERT_EQ(bad.error().code, ErrorCode::host);
}
TEST(native, public_reentry_and_registry_mutation_rejected, "REQ-0010", "REQ-0008") {
    Interpreter i;
    ASSERT_TRUE(i.register_command("probe", [](Interpreter& self, auto) -> Result {
        const auto nested = self.evaluate("set x no");
        if (nested || nested.error().code != ErrorCode::reentrant) { return std::unexpected(Error{ErrorCode::host, "reentry not rejected", {}}); }
        const auto changed = self.register_command("new", [](Interpreter&, auto) -> Result { return std::string{}; });
        if (changed || changed.error().code != ErrorCode::reentrant) { return std::unexpected(Error{ErrorCode::host, "registry mutation not rejected", {}}); }
        return std::string{"ok"};
    }).has_value());
    ASSERT_EQ(*i.evaluate("probe"), "ok");
    ASSERT_EQ(*i.evaluate("set x after"), "after");
}
TEST(limits, nested_dispatches_share_one_command_budget, "REQ-0008") {
    Interpreter i{{.commands=1}};
    const auto result = i.evaluate("set x [set y inner]");
    ASSERT_FALSE(result.has_value()); ASSERT_EQ(result.error().code, ErrorCode::limit);
    ASSERT_EQ(i.commands_executed(), 1U);
    ASSERT_EQ(*i.variable("y"), "inner");
    ASSERT_FALSE(i.variable("x").has_value());
    ASSERT_EQ(*i.evaluate("set x next"), "next");
}
TEST(limits, source_values_variables_words_and_nesting, "REQ-0008") {
    Interpreter source{{.source_bytes=2}}; ASSERT_FALSE(source.evaluate("set x a").has_value());
    Interpreter value{{.value_bytes=4}}; ASSERT_TRUE(value.evaluate("set x abcd").has_value());
    ASSERT_FALSE(value.evaluate("set y $x$x").has_value());
    Interpreter vars{{.variables=1}}; ASSERT_TRUE(vars.evaluate("set x a").has_value());
    ASSERT_FALSE(vars.evaluate("set y b").has_value()); ASSERT_EQ(*vars.variable("x"), "a");
    Interpreter words{{.words=2}}; ASSERT_FALSE(words.evaluate("set x a").has_value());
    Interpreter depth{{.nesting=1}}; ASSERT_FALSE(depth.evaluate("set x [set y [set z a]]").has_value());
    Interpreter zero{{.commands=0}}; ASSERT_FALSE(zero.evaluate("set x a").has_value());
    ASSERT_EQ(zero.commands_executed(), 0U);
}
TEST(errors, locations_retain_nested_command_origin, "REQ-0009") {
    Interpreter i;
    const auto result = i.evaluate("set x one\nset y [unknown]", "script.tcl");
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().source, "script.tcl");
    ASSERT_EQ(result.error().line, 2U); ASSERT_EQ(result.error().column, 8U);
    ASSERT_EQ(result.error().code, ErrorCode::unknown_command);
    ASSERT_EQ(*i.variable("x"), "one");
}
TEST(strings, utf8_literals_preserved_and_invalid_source_rejected, "REQ-0014") {
    Interpreter i;
    ASSERT_EQ(*i.evaluate("set {名前} {Grüße 🚀};set x ${名前}"), "Grüße 🚀");
    const std::array<unsigned char, 4> invalid{0xc0, 0xaf, 0xed, 0xa0};
    for (auto c : invalid) {
        std::string text{"set x "}; text += static_cast<char>(c);
        const auto r = i.evaluate(text);
        ASSERT_FALSE(r.has_value()); ASSERT_EQ(r.error().code, ErrorCode::unsupported);
    }
}

TEST(completeness, delimiters_literals_comments_and_trailing_continuations, "REQ-0002", "REQ-0016") {
    for (const auto* source : {"", "# comment", "set x {a {b} c}", "set x [unknown $missing]", "set x ]",
        "set x a{b", R"(set x \)", "set x \\\n  "}) {
        ASSERT_EQ(inspect_input(source).state, InputState::complete);
    }
    for (const auto* source : {"set x {", "set x \"", "set x [unknown", "set x ${name", "# comment\\\n", "set x \\\n"}) {
        const auto checked = inspect_input(source, "pending.tcl");
        ASSERT_EQ(checked.state, InputState::incomplete);
        ASSERT_TRUE(checked.error.has_value());
        ASSERT_EQ(checked.error->source, "pending.tcl");
    }
}
TEST(completeness, inspection_never_executes_substitutions_or_changes_budget, "REQ-0016", "REQ-0010") {
    Interpreter interpreter;
    unsigned calls{};
    ASSERT_TRUE(interpreter.register_command("effect", [&](Interpreter&, auto) -> Result { ++calls; return std::string{"value"}; }).has_value());
    ASSERT_EQ(inspect_input("effect;set x [effect]\nset y {open").state, InputState::incomplete);
    ASSERT_EQ(inspect_input("effect;set x [effect]\nset y {closed}").state, InputState::complete);
    ASSERT_EQ(calls, 0U);
    ASSERT_EQ(interpreter.commands_executed(), 0U);
    ASSERT_FALSE(interpreter.variable("x").has_value());
}
TEST(completeness, invalid_and_unsupported_syntax_remain_distinct_from_pending_input, "REQ-0009", "REQ-0016") {
    for (const auto* source : {"set x {a}suffix", "set x \"a\"suffix"}) {
        const auto checked = inspect_input(source);
        ASSERT_EQ(checked.state, InputState::invalid);
        ASSERT_EQ(checked.error->code, ErrorCode::syntax);
    }
    const auto unsupported = inspect_input("set x $array(index)");
    ASSERT_EQ(unsupported.state, InputState::invalid);
    ASSERT_EQ(unsupported.error->code, ErrorCode::unsupported);
}
TEST(completeness, source_word_depth_limits_and_positions_are_preserved, "REQ-0008", "REQ-0009", "REQ-0016") {
    ASSERT_EQ(inspect_input("set x value", "limit.tcl", {.source_bytes=2}).error->code, ErrorCode::limit);
    ASSERT_EQ(inspect_input("set x value", "limit.tcl", {.words=2}).error->code, ErrorCode::limit);
    ASSERT_EQ(inspect_input("set x [set y [set z a]]", "limit.tcl", {.nesting=1}).error->code, ErrorCode::limit);
    const auto pending = inspect_input("set x value\nset y {", "source.tcl");
    ASSERT_EQ(pending.error->line, 2U);
    ASSERT_EQ(inspect_input("unknown $missing", "budget.tcl", {.commands=0}).state, InputState::complete);
}
