#pragma once

#include <chrono>
#include <cstddef>
#include <set>
#include <vector>

namespace cryptoprice
{
// Connection-local: a reconnect starts with fresh seen/warned sets.
class StreamSnapshots
{
public:
    void Observe(std::size_t index) { seen_.insert(index); }
    std::vector<std::size_t> Missing(const std::vector<std::size_t>& indexes, std::chrono::milliseconds elapsed)
    {
        std::vector<std::size_t> missing;
        if (elapsed <= std::chrono::seconds(30)) return missing;
        for (auto index : indexes)
            if (!seen_.count(index) && warned_.insert(index).second)
                missing.push_back(index);
        return missing;
    }
private:
    std::set<std::size_t> seen_;
    std::set<std::size_t> warned_;
};
} // namespace cryptoprice
