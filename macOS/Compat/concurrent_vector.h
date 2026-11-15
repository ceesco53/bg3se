// Stand-in for MSVC's PPL concurrency::concurrent_vector (the subset the extender uses).
// Like the original, growing never moves existing elements, so references stay valid.
#pragma once

#include <deque>
#include <memory>
#include <mutex>

namespace concurrency {

template <class T, class TAllocator = std::allocator<T>>
class concurrent_vector
{
public:
    using iterator = typename std::deque<T, TAllocator>::iterator;
    using const_iterator = typename std::deque<T, TAllocator>::const_iterator;

    iterator push_back(T const& value)
    {
        std::lock_guard lock(mutex_);
        items_.push_back(value);
        return std::prev(items_.end());
    }

    iterator push_back(T&& value)
    {
        std::lock_guard lock(mutex_);
        items_.push_back(std::move(value));
        return std::prev(items_.end());
    }

    iterator grow_by(std::size_t count)
    {
        std::lock_guard lock(mutex_);
        auto first = items_.size();
        items_.resize(first + count);
        return items_.begin() + first;
    }

    // Elements never move, so there is nothing to pre-allocate
    void reserve(std::size_t) {}

    void resize(std::size_t count)
    {
        std::lock_guard lock(mutex_);
        items_.resize(count);
    }

    std::size_t size() const
    {
        std::lock_guard lock(mutex_);
        return items_.size();
    }

    bool empty() const { return size() == 0; }

    T& operator [] (std::size_t index)
    {
        std::lock_guard lock(mutex_);
        return items_[index];
    }

    T const& operator [] (std::size_t index) const
    {
        std::lock_guard lock(mutex_);
        return items_[index];
    }

    void clear()
    {
        std::lock_guard lock(mutex_);
        items_.clear();
    }

    iterator begin() { return items_.begin(); }
    iterator end() { return items_.end(); }
    const_iterator begin() const { return items_.begin(); }
    const_iterator end() const { return items_.end(); }

private:
    mutable std::mutex mutex_;
    std::deque<T, TAllocator> items_;
};

}

namespace Concurrency = concurrency;
