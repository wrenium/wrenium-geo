// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <array>
#include <cstddef>

#include "wrenium/geo/error.h"

namespace wrenium::geo {

/// Fixed-capacity, std::array-backed container. Zero heap, static storage
/// only; overflow is reported via Error::CapacityExceeded.
///
/// Define WRENIUM_GEO_TRACK_HIGH_WATER_MARK to have every Buffer
/// additionally track the largest size() it has ever reached, queryable
/// via highWaterMark(). This is how you find the real MaxPoints/MaxRings/
/// OutputCharCapacity a target needs instead of guessing one: build with
/// tracking on, run your actual workload (a representative sweep of
/// centers/clip radii, a replay of real device logs, or just the app
/// itself for a while), then read stageB.highWaterMark(),
/// ringSizesB.highWaterMark(), svgPath.highWaterMark(), etc. back --
/// exact, and it already accounts for anything data-dependent a formula
/// would miss (ring-clip's arc-bridging, for example). Undefined (the
/// default): zero cost, no extra member, no extra branch.
///
/// Set this as a compiler define for the whole build (e.g. a CMake
/// target_compile_definitions()), not per source file: this flag changes
/// Buffer<T, Capacity>'s actual layout, so the same specialization built
/// both ways across different translation units of one binary is an ODR
/// violation, not just a mismatched measurement.
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
#ifdef WRENIUM_GEO_TRACK_HIGH_WATER_MARK
        if (m_size > m_highWaterMark) {
            m_highWaterMark = m_size;
        }
#endif
        return Error::Ok;
    }

#ifdef WRENIUM_GEO_TRACK_HIGH_WATER_MARK
    /// The largest size() this Buffer has ever reached, surviving
    /// clear()/truncate() (unlike size() itself) -- only available when
    /// WRENIUM_GEO_TRACK_HIGH_WATER_MARK is defined; see this class's own
    /// comment.
    std::size_t highWaterMark() const
    {
        return m_highWaterMark;
    }
#endif

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
#ifdef WRENIUM_GEO_TRACK_HIGH_WATER_MARK
    std::size_t m_highWaterMark = 0;
#endif
};

} // namespace wrenium::geo
