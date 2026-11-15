// Stand-in for MSVC's PPL concurrency::concurrent_queue (the subset the extender uses).
#pragma once

#include <list>
#include <memory>
#include <mutex>

namespace concurrency {

template <class T, class TAllocator = std::allocator<T>>
class concurrent_queue
{
public:
    void push(T const& value)
    {
        std::lock_guard lock(mutex_);
        items_.push_back(value);
    }

    void push(T&& value)
    {
        std::lock_guard lock(mutex_);
        items_.push_back(std::move(value));
    }

    bool try_pop(T& value)
    {
        std::lock_guard lock(mutex_);
        if (items_.empty()) return false;
        value = std::move(items_.front());
        items_.pop_front();
        return true;
    }

    bool empty() const
    {
        std::lock_guard lock(mutex_);
        return items_.empty();
    }

    std::size_t unsafe_size() const
    {
        std::lock_guard lock(mutex_);
        return items_.size();
    }

    void clear()
    {
        std::lock_guard lock(mutex_);
        items_.clear();
    }

private:
    mutable std::mutex mutex_;
    // std::list (unlike std::deque) may be instantiated with an incomplete element type
    std::list<T, TAllocator> items_;
};

}

namespace Concurrency = concurrency;
