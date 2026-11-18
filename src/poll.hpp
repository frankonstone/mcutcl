#pragma once
#include <mcutcl/interpreter.hpp>

namespace mcutcl::detail {
// Scanners can skip bytes in an escape. Compare blocks instead of exact offsets.
class BlockPoll final {
public:
    explicit BlockPoll(RunControl* run) : run_(run) {}
    bool cancelled(std::size_t position) {
        const auto block = position / 256;
        if (!run_ || block == block_) {
            return false;
        }
        block_ = block;
        return run_->stop_requested();
    }

private:
    RunControl* run_;
    std::size_t block_{};
};
}
