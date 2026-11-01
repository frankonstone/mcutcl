#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcutcl {

enum class ErrorCode { syntax, unsupported, unknown_command, wrong_arguments,
    undefined_variable, limit, host, reentrant };
struct Error final {
    ErrorCode code{};
    std::string message;
    std::string source;
    std::size_t offset{};
    std::size_t line{1};
    std::size_t column{1};
};
using Result = std::expected<std::string, Error>;
struct Limits final {
    std::size_t commands{10000};
    std::size_t nesting{64};
    std::size_t source_bytes{1024 * 1024};
    std::size_t value_bytes{1024 * 1024};
    std::size_t words{4096};
    std::size_t variables{1024};
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
    [[nodiscard]] Result evaluate(std::string_view script, std::string_view source = "<eval>");
    [[nodiscard]] std::expected<void, Error> register_command(std::string name, Command command);
    [[nodiscard]] std::expected<void, Error> set_arguments(std::string source, std::span<const std::string> arguments);
    [[nodiscard]] Result variable(std::string_view name) const;
    [[nodiscard]] std::expected<void, Error> set_variable(std::string name, std::string value);
    [[nodiscard]] std::size_t commands_executed() const noexcept { return commands_executed_; }
private:
    Limits limits_;
    std::map<std::string, Command, std::less<>> commands_;
    std::map<std::string, std::string, std::less<>> variables_;
    bool active_{};
    std::size_t commands_executed_{};
};
[[nodiscard]] std::string_view error_name(ErrorCode code) noexcept;

} // namespace mcutcl
