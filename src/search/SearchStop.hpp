#ifndef TREEMCP_SEARCHSTOP_HPP
#define TREEMCP_SEARCHSTOP_HPP

#include <atomic>
#include <chrono>

namespace treemcp {

struct SearchStop {
    const std::atomic_bool& cancelled;
    std::chrono::steady_clock::time_point deadline;
    const std::atomic_bool* abandoned = nullptr;

    bool requested() const
    {
        return cancelled.load(std::memory_order_relaxed) || (abandoned && abandoned->load(std::memory_order_relaxed)) ||
            std::chrono::steady_clock::now() >= deadline;
    }
};

} // namespace treemcp
#endif // TREEMCP_SEARCHSTOP_HPP
