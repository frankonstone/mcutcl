#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mcutcl {

enum class ErrorCode { syntax, unsupported, unknown_command, wrong_arguments,
    undefined_variable, limit, host, reentrant, cancelled, script };
struct Error final {
    ErrorCode code{};
    std::string message;
    std::string source;
    std::size_t offset{};
    std::size_t line{1};
    std::size_t column{1};
};
using Result = std::expected<std::string, Error>;
// Tcl completion codes. An unexpected Error represents a script error or host stop.
enum class CompletionCode { ok = 0, returned = 2, break_loop = 3, continue_loop = 4 };
struct Completion final {
    CompletionCode code{CompletionCode::ok};
    std::string value;
};
using CompletionResult = std::expected<Completion, Error>;

namespace detail { class Evaluator; }

struct RunLimits final {
    std::size_t commands{10000};
    std::size_t work_bytes{64 * 1024 * 1024};
};
// Borrowed for synchronous evaluation on one owner thread. The optional probe
// must be bounded, non-throwing and free of interpreter calls. A true result latches.
class RunControl final {
public:
    explicit RunControl(RunLimits limits = {}, std::function<bool()> cancelled = {});
    RunControl(const RunControl&) = delete;
    RunControl& operator=(const RunControl&) = delete;
    [[nodiscard]] bool stop_requested();
    [[nodiscard]] RunLimits remaining() const noexcept { return remaining_; }
    [[nodiscard]] std::expected<void, Error> consume_work(std::size_t bytes);
private:
    friend class detail::Evaluator;
    RunLimits remaining_;
    std::function<bool()> probe_;
    bool stopped_{};
    bool work_exhausted_{};
};

struct Limits final {
    std::size_t commands{10000};
    std::size_t nesting{64};
    std::size_t source_bytes{1024 * 1024};
    std::size_t value_bytes{1024 * 1024};
    std::size_t words{4096};
    std::size_t variables{1024};
    std::size_t procedures{256};
    std::size_t state_bytes{8 * 1024 * 1024};
    std::size_t argument_bytes{4 * 1024 * 1024};
};
enum class InputState { complete, incomplete, invalid };
struct InputInspection final { InputState state{InputState::complete}; std::optional<Error> error; };
// Parse only within the supported profile: no evaluation, substitutions, variable
// lookup, callbacks or interpreter-budget changes. Incomplete input retains its
// missing-delimiter diagnostic for EOF; invalid/unsupported input is rejected.
[[nodiscard]] InputInspection inspect_input(std::string_view script,
    std::string_view source = "<input>", Limits limits = {});

class Interpreter;
// Arguments include the command name at index zero. Callbacks must be bounded,
// non-throwing, and owned by this interpreter's execution thread.
using Command = std::function<Result(Interpreter&, std::span<const std::string>)>;

class Interpreter final {
public:
    explicit Interpreter(Limits limits = {});
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    [[nodiscard]] Result evaluate(std::string_view script, std::string_view source = "<eval>",
        RunControl* run = nullptr);
    // Words include the command name. Each UTF-8 string is one argument.
    [[nodiscard]] Result invoke(std::span<const std::string> words,
        std::string_view source = "<invoke>", RunControl* run = nullptr);
    // For a trusted native command evaluating an intentional script argument.
    // Shares the caller's frame, depth, run control and dispatch count.
    [[nodiscard]] CompletionResult evaluate_nested(std::string_view script, std::string_view source);
    [[nodiscard]] bool stop_requested() const;
    [[nodiscard]] const std::optional<Error>& first_caught_error() const noexcept { return caught_; }
    [[nodiscard]] std::expected<void, Error> register_command(std::string name, Command command);
    [[nodiscard]] std::expected<void, Error> set_arguments(std::string source, std::span<const std::string> arguments);
    [[nodiscard]] Result variable(std::string_view name) const;
    [[nodiscard]] std::expected<void, Error> set_variable(std::string name, std::string value);
    [[nodiscard]] std::size_t commands_executed() const noexcept { return commands_executed_; }
private:
    friend class detail::Evaluator;
    using Variables = std::map<std::string, std::string, std::less<>>;
    [[nodiscard]] std::expected<std::string_view, Error> lookup(std::string_view name) const;
    struct Procedure {
        std::vector<std::string> parameters;
        std::string body;
        std::string source;
        std::size_t storage_bytes{};
    };
    enum class Builtin { procedure, catch_script, return_value, break_loop, continue_loop,
        expression, conditional, foreach_loop, while_loop, list_append, list_set, dictionary };
    Limits limits_;
    std::map<std::string, std::variant<Command, Builtin, Procedure>, std::less<>> commands_;
    Variables variables_;
    Variables* locals_{};
    detail::Evaluator* active_{};
    std::size_t state_bytes_{};
    std::size_t variable_count_{};
    std::size_t procedure_count_{};
    std::size_t commands_executed_{};
    std::optional<Error> caught_;
};
[[nodiscard]] std::string_view error_name(ErrorCode code) noexcept;

} // namespace mcutcl
