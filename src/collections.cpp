#include "collections.hpp"
#include "evaluator.hpp"
#include <mcutcl/list.hpp>
#include <algorithm>
#include <charconv>

namespace mcutcl::detail {
namespace {
std::unexpected<Error> problem(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message), {}});
}
using Words = std::span<const std::string>;
using Items = std::vector<std::string>;
using ItemsResult = std::expected<Items, Error>;

// Keep the first key's position and the last value. Views used by the index
// refer to the immutable input until this conversion returns.
ItemsResult unique_pairs(Words pairs, RunControl& run) {
    if (pairs.size() % 2) {
        return problem(ErrorCode::script, "missing dictionary value");
    }
    std::map<std::string_view, std::size_t, std::less<>> index;
    Items output;
    for (std::size_t i = 0; i < pairs.size(); i += 2) {
        for (const auto& item : pairs.subspan(i, 2)) {
            if (auto paid = run.consume_work(item.size() + 1); !paid) {
                return std::unexpected(paid.error());
            }
        }
        auto [where, inserted] = index.try_emplace(pairs[i], output.size());
        if (inserted) {
            output.push_back(pairs[i]);
            output.push_back(pairs[i + 1]);
        } else {
            output[where->second + 1] = pairs[i + 1];
        }
    }
    return output;
}
class DictionaryPath final {
public:
    DictionaryPath(const Limits& limits, RunControl& run)
        : limits_(limits), run_(run), available_(limits.argument_bytes) {}
    ItemsResult unpack(std::string_view raw) {
        if (!take_bytes(available_, raw.size())) {
            return problem(ErrorCode::limit, "dictionary path byte limit exceeded");
        }
        auto list = parse_list(raw, limits_, &run_);
        if (!list) {
            return std::unexpected(list.error());
        }
        return unique_pairs(*list, run_);
    }
    // The index points to a key; size() means that the key is absent.
    std::expected<std::size_t, Error> find(Words pairs, std::string_view key) {
        for (std::size_t i = 0; i < pairs.size(); i += 2) {
            if (auto paid = run_.consume_work(1 + std::min(key.size(), pairs[i].size())); !paid) {
                return std::unexpected(paid.error());
            }
            if (pairs[i] == key) {
                return i;
            }
        }
        return pairs.size();
    }
    Result get(std::string_view raw, Words keys, bool exists) {
        Items current;
        for (const auto& key : keys) {
            auto pairs = unpack(raw);
            if (!pairs) {
                if (exists && (pairs.error().code == ErrorCode::syntax ||
                               pairs.error().code == ErrorCode::script)) {
                    return std::string{"0"};
                }
                return std::unexpected(pairs.error());
            }
            current = std::move(*pairs);
            auto index = find(current, key);
            if (!index) {
                return std::unexpected(index.error());
            }
            if (*index == current.size()) {
                if (exists) {
                    return std::string{"0"};
                }
                return problem(ErrorCode::script, "unknown dictionary key: " + key);
            }
            raw = current[*index + 1];
        }
        if (exists) {
            return std::string{"1"};
        }
        if (!keys.empty()) {
            return std::string(raw);
        }
        auto pairs = unpack(raw);
        if (!pairs) {
            return std::unexpected(pairs.error());
        }
        return make_list(*pairs, limits_, &run_);
    }
    Result update(std::string_view raw, Words keys, const std::string* value) {
        auto pairs = unpack(raw);
        if (!pairs) {
            return std::unexpected(pairs.error());
        }
        auto index = find(*pairs, keys.front());
        if (!index) {
            return std::unexpected(index.error());
        }
        const bool missing = *index == pairs->size();
        if (keys.size() == 1 && !value) {
            if (!missing) {
                pairs->erase(pairs->begin() + *index, pairs->begin() + *index + 2);
            }
        } else {
            if (missing) {
                if (!value) {
                    return problem(ErrorCode::script, "missing intermediate dictionary key");
                }
                if (limits_.words - pairs->size() < 2) {
                    return problem(ErrorCode::limit, "dictionary element limit exceeded");
                }
                pairs->push_back(keys.front());
                pairs->emplace_back();
            }
            auto& slot = (*pairs)[*index + 1];
            if (keys.size() == 1) {
                slot = *value;
            } else {
                auto child = update(slot, keys.subspan(1), value);
                if (!child) {
                    return child;
                }
                slot = std::move(*child);
            }
        }
        return make_list(*pairs, limits_, &run_);
    }

private:
    const Limits& limits_;
    RunControl& run_;
    std::size_t available_;
};
Result replace_index(std::string_view raw, Words indices, const std::string& value,
                     const Limits& limits, RunControl& run, std::size_t& available) {
    if (indices.empty()) {
        return value;
    }
    if (!take_bytes(available, raw.size())) {
        return problem(ErrorCode::limit, "list path byte limit exceeded");
    }
    auto index = list_index(indices.front());
    if (!index) {
        return std::unexpected(index.error());
    }
    auto list = parse_list(raw, limits, &run);
    if (!list) {
        return std::unexpected(list.error());
    }
    if (*index < 0 || static_cast<std::uint64_t>(*index) > list->size()) {
        return problem(ErrorCode::script, "list index out of range");
    }
    const auto slot = static_cast<std::size_t>(*index);
    if (slot == list->size()) {
        if (slot == limits.words) {
            return problem(ErrorCode::limit, "list element limit exceeded");
        }
        list->emplace_back();
    }
    auto child = replace_index((*list)[slot], indices.subspan(1), value, limits, run, available);
    if (!child) {
        return child;
    }
    (*list)[slot] = std::move(*child);
    return make_list(*list, limits, &run);
}
}

std::expected<std::int64_t, Error> list_index(std::string_view text) {
    auto digits = text;
    if (digits.starts_with('-')) {
        digits.remove_prefix(1);
    }
    if (digits.empty() || (digits.size() > 1 && digits.front() == '0') || text == "-0") {
        return problem(ErrorCode::unsupported, "expected a canonical decimal list index");
    }
    std::int64_t value{};
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return problem(ErrorCode::unsupported, "unsupported list index");
    }
    return value;
}

// @req REQ-0004 REQ-0005 REQ-0008 REQ-0021 REQ-0022
Result Evaluator::collection(Interpreter::Builtin kind, Words words, std::size_t depth) {
    using Builtin = Interpreter::Builtin;
    const auto save = [&](const std::string& name, Result result) -> Result {
        if (!result) {
            return result;
        }
        if (auto paid = run_.consume_work(result->size()); !paid) {
            return std::unexpected(paid.error());
        }
        auto stored = interpreter_.set_variable(name, *result);
        if (!stored) {
            return std::unexpected(stored.error());
        }
        return result;
    };
    const auto contents = [&](const std::string& name,
                              bool may_create) -> std::expected<std::string_view, Error> {
        auto old = interpreter_.lookup(name);
        if (!old && may_create && old.error().code == ErrorCode::undefined_variable) {
            return std::string_view{};
        }
        return old;
    };
    if (kind == Builtin::list_append) {
        if (words.size() < 2) {
            return problem(ErrorCode::wrong_arguments, "expected lappend varName ?value ...?");
        }
        auto old = contents(words[1], true);
        if (!old) {
            return std::unexpected(old.error());
        }
        auto list = parse_list(*old, limits(), &run_);
        if (!list) {
            return std::unexpected(list.error());
        }
        if (words.size() == 2) {
            return save(words[1], std::string(*old));
        }
        auto extra = words.subspan(2);
        if (extra.size() > limits().words - list->size()) {
            return problem(ErrorCode::limit, "list element limit exceeded");
        }
        list->insert(list->end(), extra.begin(), extra.end());
        return save(words[1], make_list(*list, limits(), &run_));
    }
    if (kind == Builtin::list_set) {
        if (words.size() < 3) {
            return problem(ErrorCode::wrong_arguments, "expected lset varName ?index ...? value");
        }
        auto old = contents(words[1], false);
        if (!old) {
            return std::unexpected(old.error());
        }
        auto indices = words.subspan(2, words.size() - 3);
        Items grouped;
        if (indices.size() == 1) {
            auto list = parse_list(indices[0], limits(), &run_);
            if (!list) {
                return std::unexpected(list.error());
            }
            grouped = std::move(*list);
            indices = grouped;
        }
        if (indices.size() > depth_limit_ - depth) {
            return problem(ErrorCode::limit, "list path nesting limit exceeded");
        }
        auto available = limits().argument_bytes;
        return save(words[1],
                    replace_index(*old, indices, words.back(), limits(), run_, available));
    }
    if (words.size() < 2) {
        return problem(ErrorCode::wrong_arguments, "expected a dictionary operation");
    }
    const auto& op = words[1];
    if (op == "create") {
        auto pairs = unique_pairs(words.subspan(2), run_);
        if (!pairs) {
            return std::unexpected(pairs.error());
        }
        return make_list(*pairs, limits(), &run_);
    }
    if (op != "get" && op != "exists" && op != "set" && op != "unset") {
        return problem(ErrorCode::unsupported, "unsupported dictionary operation");
    }
    const bool write = op == "set" || op == "unset";
    const std::size_t minimum = op == "get" ? 3 : (op == "set" ? 5 : 4);
    if (words.size() < minimum) {
        return problem(ErrorCode::wrong_arguments, "missing dictionary arguments");
    }
    auto keys = words.subspan(3, words.size() - 3 - (op == "set"));
    if (keys.size() > depth_limit_ - depth) {
        return problem(ErrorCode::limit, "dictionary key nesting limit exceeded");
    }
    DictionaryPath path{limits(), run_};
    if (!write) {
        return path.get(words[2], keys, op == "exists");
    }
    auto old = contents(words[2], true);
    if (!old) {
        return std::unexpected(old.error());
    }
    return save(words[2], path.update(*old, keys, op == "set" ? &words.back() : nullptr));
}
} // namespace mcutcl::detail
