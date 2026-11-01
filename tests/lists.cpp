#include <mcutcl/list.hpp>
#include <mcutest/mcutest.hpp>
#include <array>

using namespace mcutcl;

TEST(lists, host_values_round_trip_without_evaluation, "REQ-0005", "REQ-0010") {
    const std::array<std::string, 9> values{{"", "two words", "{unbalanced", "[unknown];$missing", "a\\b",
        "line\nsecond\t", "Grüße 🚀", std::string("a\0b", 3), "# comment \"quoted\""}};
    const auto encoded = make_list(values);
    ASSERT_TRUE(encoded.has_value());
    const auto decoded = parse_list(*encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), values.size());
    for (std::size_t i = 0; i < values.size(); ++i) { ASSERT_EQ((*decoded)[i], values[i]); }
}
TEST(lists, grouping_and_basic_escapes_are_data_not_substitutions, "REQ-0005") {
    const auto decoded = parse_list(R"({one two} "three four" {} five\ six [unknown];$missing a"b a{b)");
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 7U);
    ASSERT_EQ((*decoded)[0], "one two"); ASSERT_EQ((*decoded)[1], "three four");
    ASSERT_EQ((*decoded)[2], ""); ASSERT_EQ((*decoded)[3], "five six");
    ASSERT_EQ((*decoded)[4], "[unknown];$missing");
    ASSERT_EQ((*decoded)[5], "a\"b"); ASSERT_EQ((*decoded)[6], "a{b");
}
TEST(lists, brace_continuations_remain_literal_while_other_continuations_collapse, "REQ-0005", "REQ-0018") {
    ASSERT_EQ((*parse_list("{a\\\n b}"))[0], "a\\\n b");
    ASSERT_EQ((*parse_list("\"a\\\n b\""))[0], "a b");
    ASSERT_EQ((*parse_list("a\\\n b"))[0], "a b");
}
TEST(lists, malformed_and_unfinished_forms_reject_explicitly, "REQ-0005", "REQ-0020") {
    for (const auto* source : {"{open", "\"open", "{closed}suffix", "\"closed\"suffix"}) {
        const auto result = parse_list(source);
        ASSERT_FALSE(result.has_value()); ASSERT_EQ(result.error().code, ErrorCode::syntax);
    }
    ASSERT_EQ(parse_list(R"(\x41)").error().code, ErrorCode::unsupported);
    ASSERT_EQ((*parse_list(R"({\x41})"))[0], R"(\x41)");
}
TEST(lists, list_limits_apply_before_unbounded_output_or_storage, "REQ-0008") {
    const std::array<std::string, 2> values{{"a", "b"}};
    ASSERT_EQ(make_list(values, {.words=1}).error().code, ErrorCode::limit);
    ASSERT_EQ(parse_list("a b", {.words=1}).error().code, ErrorCode::limit);
    ASSERT_EQ(parse_list("{{a}}", {.nesting=1}).error().code, ErrorCode::limit);
    const std::array<std::string, 1> escaping{{"   "}};
    ASSERT_EQ(make_list(escaping, {.value_bytes=5}).error().code, ErrorCode::limit);
    ASSERT_EQ(parse_list("too long", {.value_bytes=3}).error().code, ErrorCode::limit);
}
TEST(lists, basic_commands_compose_and_indices_are_strict, "REQ-0005") {
    Interpreter interpreter;
    ASSERT_EQ(*interpreter.evaluate("set x [list {one two} {} {[unknown]}];llength $x"), "3");
    ASSERT_EQ(*interpreter.evaluate("lindex $x 0"), "one two");
    ASSERT_EQ(*interpreter.evaluate("lindex $x 1"), "");
    ASSERT_EQ(*interpreter.evaluate("lindex $x 2"), "[unknown]");
    ASSERT_EQ(*interpreter.evaluate("lindex $x -1"), "");
    ASSERT_EQ(*interpreter.evaluate("lindex $x 99"), "");
    for (const auto* index : {"end", "0x1", "08", "1+1", "01", "9223372036854775808"}) {
        const auto result = interpreter.evaluate(std::string("lindex $x ") + index);
        ASSERT_FALSE(result.has_value()); ASSERT_EQ(result.error().code, ErrorCode::unsupported);
    }
}
TEST(arguments, reserved_variables_preserve_exact_values_and_replacement, "REQ-0010", "REQ-0015") {
    Interpreter interpreter;
    const std::array<std::string, 3> values{{"two words", "", "[unknown];$missing"}};
    ASSERT_TRUE(interpreter.set_arguments("file with spaces.tcl", values).has_value());
    ASSERT_EQ(*interpreter.variable("argv0"), "file with spaces.tcl");
    ASSERT_EQ(*interpreter.variable("argc"), "3");
    ASSERT_EQ(*interpreter.evaluate("lindex $argv 2"), values[2]);
    ASSERT_TRUE(interpreter.set_arguments("next.tcl", {}).has_value());
    ASSERT_EQ(*interpreter.variable("argc"), "0"); ASSERT_EQ(*interpreter.variable("argv"), "");
}
TEST(arguments, rejected_contexts_do_not_partially_replace_existing_variables, "REQ-0008", "REQ-0010") {
    Interpreter interpreter{{.value_bytes=32}};
    const std::array<std::string, 1> initial{{"value"}}, huge{{std::string(33, 'x')}};
    ASSERT_TRUE(interpreter.set_arguments("before.tcl", initial).has_value());
    ASSERT_FALSE(interpreter.set_arguments("after.tcl", huge).has_value());
    ASSERT_EQ(*interpreter.variable("argv0"), "before.tcl");
    ASSERT_EQ(*interpreter.variable("argc"), "1");
    ASSERT_EQ(*interpreter.evaluate("lindex $argv 0"), "value");
    Interpreter too_few{{.variables=2}};
    ASSERT_FALSE(too_few.set_arguments("file.tcl", {}).has_value());
    ASSERT_FALSE(too_few.variable("argv0").has_value());
}
TEST(arguments, active_callbacks_cannot_replace_the_context, "REQ-0010") {
    Interpreter interpreter;
    ASSERT_TRUE(interpreter.register_command("probe", [](Interpreter& self, auto) -> Result {
        const auto result = self.set_arguments("changed", {});
        if (result || result.error().code != ErrorCode::reentrant) { return std::unexpected(Error{ErrorCode::host,"reentry accepted",{}}); }
        return std::string{"rejected"};
    }).has_value());
    ASSERT_EQ(*interpreter.evaluate("probe"), "rejected");
}
