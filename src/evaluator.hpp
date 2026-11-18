#pragma once

#include <mcutcl/interpreter.hpp>
#include <utility>

namespace mcutcl::detail {

struct Node;
struct Part;
class ExpressionParser;

inline CompletionResult as_completion(Result result) {
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    return Completion{CompletionCode::ok, std::move(*result)};
}

// Leave the allowance unchanged if a value does not fit.
inline bool take_bytes(std::size_t& available, std::size_t bytes) {
    if (bytes > available) {
        return false;
    }
    available -= bytes;
    return true;
}

class Evaluator final {
public:
    using Words = std::span<const std::string>;
    using Input = std::variant<std::string_view, Words>;
    static Result start(Interpreter& interpreter, Input input, std::string_view source,
                        RunControl* requested);
    CompletionResult nested(std::string_view script, std::string_view source);
    bool stop_requested() { return run_.stop_requested(); }
    RunControl& control() { return run_; }
    bool is_current() const noexcept { return current_ == this; }

private:
    friend class ExpressionParser;
    const Limits& limits() const noexcept { return interpreter_.limits_; }
    Evaluator(Interpreter& interpreter, RunControl& run);
    ~Evaluator();
    enum class Boundary { body, frame };
    CompletionResult script(std::string_view text, std::string_view source, std::size_t depth,
                            Boundary boundary = Boundary::body);
    CompletionResult execute(const Node&, std::string_view text, std::string_view source,
                             std::size_t depth, Boundary);
    CompletionResult expand(std::span<const Part>, std::string_view text, std::string_view source,
                            std::size_t depth, Boundary, std::size_t& available);
    CompletionResult expression_operand(std::string_view input, std::size_t& consumed,
                                        std::string_view source, std::size_t depth, bool evaluate);
    CompletionResult expression(std::string_view text, std::string_view source, std::size_t depth);
    CompletionResult conditional(Words, std::string_view source, std::size_t depth);
    CompletionResult loop(Interpreter::Builtin, Words, std::string_view source, std::size_t depth);
    Result collection(Interpreter::Builtin, Words, std::size_t depth);
    static Error escaped_loop(CompletionCode code);
    static Result finish_frame(CompletionResult result, std::string_view source);
    CompletionResult invoke(Words words, std::string_view source, std::size_t depth);
    CompletionResult dispatch(Words words, std::string_view source, std::size_t depth);
    CompletionResult builtin(Interpreter::Builtin command, Words words, std::string_view source,
                             std::size_t depth);
    CompletionResult define_procedure(Words words, std::string_view source);
    CompletionResult call_procedure(const Interpreter::Procedure& proc, Words words,
                                    std::size_t depth);
    CompletionResult catch_script(Words words, std::string_view source, std::size_t depth);
    std::expected<void, Error> check_dispatch() const;
    std::expected<void, Error> charge_command();

    Interpreter& interpreter_;
    RunControl& run_;
    // The currently executing owner on this thread prevents A -> B -> A reentry.
    static thread_local Evaluator* current_;
    Evaluator* previous_;
    std::size_t depth_limit_;
    std::optional<std::size_t> native_depth_;
};

} // namespace mcutcl::detail
