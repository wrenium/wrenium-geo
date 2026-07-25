// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <array>
#include <cstddef>

#include "wrenium/geo/error.h"

/// Fixed-capacity, std::array-backed container. Zero heap, static storage
/// only; overflow is reported via Error::CapacityExceeded.

namespace wrenium::geo {

/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements, fixed at compile time.
template <typename T, std::size_t Capacity>
class Buffer
{
public:
    /// Appends @p value if there's room.
    /// @return Error::Ok on success, Error::CapacityExceeded if the buffer
    /// is already at capacity() (the buffer is left unchanged).
    Error pushBack(const T &value)
    {
        if (m_size >= Capacity) {
            return Error::CapacityExceeded;
        }
        m_data[m_size] = value;
        ++m_size;
        return Error::Ok;
    }

    std::size_t size() const
    {
        return m_size;
    }

    constexpr std::size_t capacity() const
    {
        return Capacity;
    }

    T *data()
    {
        return m_data.data();
    }

    const T *data() const
    {
        return m_data.data();
    }

    T &operator[](std::size_t index)
    {
        return m_data[index];
    }

    const T &operator[](std::size_t index) const
    {
        return m_data[index];
    }

    void clear()
    {
        m_size = 0;
    }

    /// Shrinks the logical size to @p newSize; a no-op if @p newSize isn't
    /// smaller than the current size. Used by the clip stage to discard a
    /// ring that turned out degenerate, without a second buffer to copy out of.
    void truncate(std::size_t newSize)
    {
        if (newSize < m_size) {
            m_size = newSize;
        }
    }

    T *begin()
    {
        return m_data.data();
    }

    T *end()
    {
        return m_data.data() + m_size;
    }

    const T *begin() const
    {
        return m_data.data();
    }

    const T *end() const
    {
        return m_data.data() + m_size;
    }

private:
    std::array<T, Capacity> m_data{};
    std::size_t m_size = 0;
};

} // namespace wrenium::geo
