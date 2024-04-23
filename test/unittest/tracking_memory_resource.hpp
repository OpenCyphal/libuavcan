/// @file
/// libcyphal common header.
///
/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT

#ifndef LIBCYPHAL_TRACKING_MEMORY_RESOURCE_HPP_INCLUDED
#define LIBCYPHAL_TRACKING_MEMORY_RESOURCE_HPP_INCLUDED

#include <cetl/pmr/memory.hpp>

class TrackingMemoryResource final : public cetl::pmr::memory_resource
{
public:
    std::size_t total_allocated_bytes = 0;
    std::size_t total_deallocated_bytes = 0;

private:
    // MARK: cetl::pmr::memory_resource

    void* do_allocate(std::size_t size_bytes, std::size_t alignment) override
    {
        if (alignment > alignof(std::max_align_t))
        {
#if defined(__cpp_exceptions)
            throw std::bad_alloc();
#endif
            return nullptr;
        }

        total_allocated_bytes += size_bytes;
        return std::malloc(size_bytes);
    }

    void do_deallocate(void* p, std::size_t size_bytes, std::size_t) override
    {
        total_deallocated_bytes += size_bytes;
        std::free(p);
    }

#if (__cplusplus < CETL_CPP_STANDARD_17)

    void* do_reallocate(void*       ptr,
                        std::size_t old_size_bytes,
                        std::size_t new_size_bytes,
                        std::size_t) override
    {
        total_allocated_bytes -= old_size_bytes;
        total_allocated_bytes += new_size_bytes;

        return std::realloc(ptr, new_size_bytes);
    }

#endif

    bool do_is_equal(const memory_resource& rhs) const noexcept override
    {
        return (&rhs == this);
    }

};  // TrackingMemoryResource

#endif  // LIBCYPHAL_TRACKING_MEMORY_RESOURCE_HPP_INCLUDED