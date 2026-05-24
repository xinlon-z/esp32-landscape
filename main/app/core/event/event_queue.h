#pragma once

#include <stddef.h>
#include <stdint.h>

template <typename T, size_t Capacity>
class EventQueue {
public:
    bool publish(const T& event)
    {
        if (Capacity == 0 || count_ == Capacity) {
            ++overflow_count_;
            return false;
        }
        items_[tail_] = event;
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    bool poll(T* event)
    {
        if (!event || count_ == 0) {
            return false;
        }
        *event = items_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

    size_t size() const { return count_; }
    uint32_t overflowCount() const { return overflow_count_; }

private:
    T items_[Capacity == 0 ? 1 : Capacity]{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    uint32_t overflow_count_ = 0;
};
