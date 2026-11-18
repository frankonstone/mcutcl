#include "evaluator.hpp"
#include "text.hpp"
#include "numeric.hpp"
#include <mcutcl/list.hpp>

#include <algorithm>
#include <utility>

namespace mcutcl {
namespace {
std::unexpected<Error> fail(ErrorCode code, std::string message, std::string_view source = {}) {
    return std::unexpected(Error{code, std::move(message), std::string(source)});
}

bool identifier(std::string_view name) {
    constexpr std::string_view characters =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
    return !name.empty() && !(name.front() >= '0' && name.front() <= '9') &&
           name.find_first_not_of(characters) == std::string_view::npos;
}
}

// @req REQ-0008 REQ-0022
RunControl::RunControl(RunLimits limits, std::function<bool()> cancelled)
    : remaining_(limits), probe_(std::move(cancelled)) {}

bool RunControl::stop_requested() {
    if (!stopped_ && probe_) {
        stopped_ = probe_();
    }
    return stopped_;
}

std::expected<void, Error> RunControl::consume_work(std::size_t bytes) {
    if (stop_requested()) {
        return fail(ErrorCode::cancelled, "evaluation cancelled");
    }
    if (work_exhausted_ || bytes > remaining_.work_bytes) {
        work_exhausted_ = true;
        return fail(ErrorCode::limit, "run work budget exceeded");
    }
    remaining_.work_bytes -= bytes;
    return {};
}

namespace detail {
thread_local Evaluator* Evaluator::current_{};

Error Evaluator::escaped_loop(CompletionCode code) {
    const char* message = code == CompletionCode::break_loop
                              ? "invoked \"break\" outside of a loop"
                              : "invoked \"continue\" outside of a loop";
    return {ErrorCode::script, message, {}};
}

// A procedure or an outer evaluation consumes one plain return. Catch bodies
// receive the original completion, including break and continue.
Result Evaluator::finish_frame(CompletionResult result, std::string_view source) {
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    if (result->code == CompletionCode::break_loop ||
        result->code == CompletionCode::continue_loop) {
        auto error = escaped_loop(result->code);
        error.source = source;
        return std::unexpected(std::move(error));
    }
    return std::move(result->value);
}

Evaluator::Evaluator(Interpreter& interpreter, RunControl& run)
    : interpreter_(interpreter), run_(run), previous_(std::exchange(current_, this)),
      depth_limit_(interpreter.limits_.nesting) {
    if (previous_) {
        depth_limit_ = std::min(depth_limit_, previous_->depth_limit_);
    }
    interpreter_.active_ = this;
    interpreter_.commands_executed_ = 0;
    interpreter_.caught_.reset();
}

Evaluator::~Evaluator() {
    interpreter_.active_ = nullptr;
    current_ = previous_;
}

// @req REQ-0010 REQ-0017 REQ-0022 REQ-0023
Result Evaluator::start(Interpreter& interpreter, Input input, std::string_view source,
                        RunControl* requested) {
    if (interpreter.active_) {
        return fail(ErrorCode::reentrant, "cannot re-enter an active interpreter", source);
    }
    std::size_t depth = 0;
    if (current_) {
        if (!current_->native_depth_ || (requested && requested != &current_->run_)) {
            return fail(ErrorCode::reentrant, "model invocation must share the active host run",
                        source);
        }
        if (*current_->native_depth_ >=
            std::min(interpreter.limits_.nesting, current_->depth_limit_)) {
            return fail(ErrorCode::limit, "cross-instance nesting limit exceeded", source);
        }
        depth = *current_->native_depth_ + 1;
        requested = &current_->run_;
    }
    RunControl local{{.commands = interpreter.limits_.commands}};
    Evaluator evaluation{interpreter, requested ? *requested : local};
    if (const auto* text = std::get_if<std::string_view>(&input)) {
        return finish_frame(evaluation.script(*text, source, depth, Boundary::frame), source);
    }
    return finish_frame(evaluation.invoke(std::get<Words>(input), source, depth), source);
}

CompletionResult Evaluator::nested(std::string_view text, std::string_view source) {
    if (current_ != this || !native_depth_) {
        return fail(ErrorCode::reentrant, "nested evaluation requires the current native command",
                    source);
    }
    if (*native_depth_ >= depth_limit_) {
        return fail(ErrorCode::limit, "nesting limit exceeded", source);
    }
    const auto caller = std::exchange(native_depth_, std::nullopt);
    auto result = script(text, source, *caller + 1);
    native_depth_ = caller;
    return result;
}

std::expected<void, Error> Evaluator::check_dispatch() const {
    if (auto ready = run_.consume_work(0); !ready) {
        return ready;
    }
    if (run_.remaining_.commands == 0 ||
        interpreter_.commands_executed_ >= interpreter_.limits_.commands) {
        return fail(ErrorCode::limit, "command budget exceeded");
    }
    return {};
}

// @req REQ-0008 REQ-0009 REQ-0023
CompletionResult Evaluator::invoke(Words words, std::string_view source, std::size_t depth) {
    if (run_.stop_requested()) {
        return fail(ErrorCode::cancelled, "evaluation cancelled", source);
    }
    if (words.empty()) {
        return fail(ErrorCode::wrong_arguments, "invoke requires a command name", source);
    }
    if (auto ready = check_dispatch(); !ready) {
        ready.error().source = source;
        return std::unexpected(std::move(ready.error()));
    }
    const auto& limits = interpreter_.limits_;
    if (words.size() > limits.words) {
        return fail(ErrorCode::limit, "word limit exceeded", source);
    }
    auto available = limits.argument_bytes;
    for (const auto& word : words) {
        if (word.size() > limits.value_bytes || !take_bytes(available, word.size())) {
            return fail(ErrorCode::limit, "argument size limit exceeded", source);
        }
        if (auto paid = run_.consume_work(word.size()); !paid) {
            paid.error().source = source;
            return std::unexpected(std::move(paid.error()));
        }
        if (!utf8(word)) {
            return fail(ErrorCode::unsupported, "argument must be valid UTF-8", source);
        }
    }
    auto result = dispatch(words, source, depth);
    if (!result && result.error().source.empty()) {
        result.error().source = source;
    }
    return result;
}

std::expected<void, Error> Evaluator::charge_command() {
    if (auto ready = check_dispatch(); !ready) {
        return ready;
    }
    --run_.remaining_.commands;
    ++interpreter_.commands_executed_;
    return {};
}

CompletionResult Evaluator::dispatch(Words words, std::string_view source, std::size_t depth) {
    if (auto ready = charge_command(); !ready) {
        return std::unexpected(ready.error());
    }
    std::string_view name = words.front();
    if (name.starts_with("::")) {
        name.remove_prefix(2);
    }
    const auto found = interpreter_.commands_.find(name);
    if (found == interpreter_.commands_.end()) {
        return fail(ErrorCode::unknown_command,
                    "unknown or unimplemented command: " + words.front());
    }
    auto& entry = found->second;
    CompletionResult result;
    if (auto* native = std::get_if<Command>(&entry)) {
        const auto saved = std::exchange(native_depth_, depth);
        result = as_completion((*native)(interpreter_, words));
        native_depth_ = saved;
    } else if (auto* proc = std::get_if<Interpreter::Procedure>(&entry)) {
        if (depth >= depth_limit_) {
            return fail(ErrorCode::limit, "procedure nesting limit exceeded");
        }
        result = call_procedure(*proc, words, depth + 1);
    } else {
        result = builtin(std::get<Interpreter::Builtin>(entry), words, source, depth);
    }
    if (!result) {
        return result;
    }
    if (result->value.size() > interpreter_.limits_.value_bytes) {
        return fail(ErrorCode::limit, "command result exceeds value limit");
    }
    if (auto paid = run_.consume_work(result->value.size()); !paid) {
        return std::unexpected(paid.error());
    }
    if (!utf8(result->value)) {
        return fail(ErrorCode::unsupported, "command result must be valid UTF-8");
    }
    return result;
}

// @req REQ-0007 REQ-0011 REQ-0021 REQ-0022
CompletionResult Evaluator::builtin(Interpreter::Builtin command, Words words,
                                    std::string_view source, std::size_t depth) {
    using Builtin = Interpreter::Builtin;
    switch (command) {
    case Builtin::expression:
        if (words.size() != 2) {
            return fail(ErrorCode::unsupported, "expected expr with one expression");
        }
        return expression(words[1], source, depth + 1);
    case Builtin::conditional:
        return conditional(words, source, depth + 1);
    case Builtin::foreach_loop:
    case Builtin::while_loop:
        return loop(command, words, source, depth + 1);
    case Builtin::list_append:
    case Builtin::list_set:
    case Builtin::dictionary:
        return as_completion(collection(command, words, depth));
    case Builtin::procedure:
        return define_procedure(words, source);
    case Builtin::catch_script:
        return catch_script(words, source, depth);
    case Builtin::return_value:
        if (words.size() > 2) {
            return fail(ErrorCode::unsupported, "supported form: return ?value?");
        }
        return Completion{CompletionCode::returned, words.size() == 2 ? words[1] : std::string{}};
    case Builtin::break_loop:
    case Builtin::continue_loop:
        if (words.size() != 1) {
            return fail(ErrorCode::wrong_arguments, words.front() + " takes no arguments");
        }
        return Completion{command == Builtin::break_loop ? CompletionCode::break_loop
                                                         : CompletionCode::continue_loop,
                          {}};
    }
    return fail(ErrorCode::script, "invalid builtin");
}

CompletionResult Evaluator::catch_script(Words words, std::string_view source, std::size_t depth) {
    if (words.size() < 2 || words.size() > 3) {
        return fail(ErrorCode::unsupported, "supported form: catch script ?resultVarName?");
    }
    if (depth >= depth_limit_) {
        return fail(ErrorCode::limit, "catch nesting limit exceeded");
    }
    auto result = script(words[1], std::string(source) + "::<catch>", depth + 1);
    if (!result) {
        const auto code = result.error().code;
        if (code == ErrorCode::limit || code == ErrorCode::cancelled ||
            code == ErrorCode::reentrant) {
            return result;
        }
        if (!interpreter_.caught_) {
            interpreter_.caught_ = result.error();
        }
    }
    if (run_.stop_requested()) {
        return fail(ErrorCode::cancelled, "evaluation cancelled");
    }
    if (words.size() == 3) {
        const auto& value = result ? result->value : result.error().message;
        auto assigned = interpreter_.set_variable(words[2], value);
        if (!assigned) {
            return std::unexpected(std::move(assigned.error()));
        }
    }
    const int code = result ? static_cast<int>(result->code) : 1;
    return Completion{CompletionCode::ok, std::to_string(code)};
}

// @req REQ-0004 REQ-0008 REQ-0011 REQ-0021
CompletionResult Evaluator::define_procedure(Words words, std::string_view source) {
    if (words.size() != 4) {
        return fail(ErrorCode::wrong_arguments, "expected proc name args body");
    }
    if (interpreter_.locals_) {
        return fail(ErrorCode::unsupported, "define procedures in the global frame");
    }
    std::string_view name = words[1];
    if (name.starts_with("::")) {
        name.remove_prefix(2);
    }
    if (!identifier(name)) {
        return fail(ErrorCode::unsupported, "expected a simple procedure name");
    }
    auto found = interpreter_.commands_.find(name);
    const bool fresh = found == interpreter_.commands_.end();
    auto available = interpreter_.limits_.state_bytes - interpreter_.state_bytes_;
    if (fresh) {
        if (interpreter_.procedure_count_ == interpreter_.limits_.procedures) {
            return fail(ErrorCode::limit, "procedure count limit exceeded");
        }
    } else {
        const auto* old = std::get_if<Interpreter::Procedure>(&found->second);
        if (!old) {
            return fail(ErrorCode::unsupported, "cannot replace a builtin or host command");
        }
        available += old->storage_bytes;
    }
    const auto before = available;
    // Retain the command name, its body, and a diagnostic label "source::name".
    if (!take_bytes(available, name.size()) || !take_bytes(available, words[3].size()) ||
        !take_bytes(available, source.size()) || !take_bytes(available, 2) ||
        !take_bytes(available, name.size())) {
        return fail(ErrorCode::limit, "procedure exceeds state limit");
    }
    auto parameters = parse_list(words[2], interpreter_.limits_, &run_);
    if (!parameters) {
        return std::unexpected(std::move(parameters.error()));
    }
    for (const auto& formal : *parameters) {
        if (!identifier(formal)) {
            return fail(ErrorCode::unsupported, "expected fixed scalar procedure parameters");
        }
        if (!take_bytes(available, formal.size())) {
            return fail(ErrorCode::limit, "procedure exceeds state limit");
        }
    }
    if (!parameters->empty() && parameters->back() == "args") {
        return fail(ErrorCode::unsupported, "variadic procedures are not supported");
    }
    Interpreter::Procedure proc{std::move(*parameters), words[3],
                                std::string(source) + "::" + std::string(name), before - available};
    interpreter_.commands_.insert_or_assign(std::string(name), std::move(proc));
    interpreter_.state_bytes_ = interpreter_.limits_.state_bytes - available;
    if (fresh) {
        ++interpreter_.procedure_count_;
    }
    return Completion{};
}

CompletionResult Evaluator::call_procedure(const Interpreter::Procedure& proc, Words words,
                                           std::size_t depth) {
    if (words.size() - 1 != proc.parameters.size()) {
        return fail(ErrorCode::wrong_arguments, "wrong number of arguments to " + words.front());
    }
    Interpreter::Variables locals;
    auto* caller = std::exchange(interpreter_.locals_, &locals);
    CompletionResult result;
    for (std::size_t i = 0; i < proc.parameters.size(); ++i) {
        // Tcl resolves a repeated formal name to its first argument slot.
        if (locals.contains(proc.parameters[i])) {
            continue;
        }
        auto bound = interpreter_.set_variable(proc.parameters[i], words[i + 1]);
        if (!bound) {
            result = std::unexpected(std::move(bound.error()));
            break;
        }
    }
    if (result) {
        result = script(proc.body, proc.source, depth, Boundary::frame);
    }
    interpreter_.locals_ = caller;
    for (const auto& [key, value] : locals) {
        interpreter_.state_bytes_ -= key.size() + value.size();
    }
    interpreter_.variable_count_ -= locals.size();
    return as_completion(finish_frame(std::move(result), proc.source));
}
} // namespace detail

// @req REQ-0010 REQ-0017 REQ-0022 REQ-0023
Result Interpreter::evaluate(std::string_view script, std::string_view source, RunControl* run) {
    return detail::Evaluator::start(*this, script, source, run);
}

Result Interpreter::invoke(std::span<const std::string> words, std::string_view source,
                           RunControl* run) {
    return detail::Evaluator::start(*this, words, source, run);
}

CompletionResult Interpreter::evaluate_nested(std::string_view script, std::string_view source) {
    if (!active_) {
        return fail(ErrorCode::reentrant, "nested evaluation requires an active native command",
                    source);
    }
    return active_->nested(script, source);
}

bool Interpreter::stop_requested() const { return active_ && active_->stop_requested(); }
} // namespace mcutcl
