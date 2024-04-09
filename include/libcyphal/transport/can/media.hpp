/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT

#ifndef LIBCYPHAL_TRANSPORT_CAN_MEDIA_HPP_INCLUDED
#define LIBCYPHAL_TRANSPORT_CAN_MEDIA_HPP_INCLUDED

#include "libcyphal/types.hpp"
#include "libcyphal/transport/errors.hpp"
#include "libcyphal/transport/defines.hpp"

namespace libcyphal
{
namespace transport
{
namespace can
{

using CanId = std::uint32_t;

struct Filter final
{
    CanId id;
    CanId mask;
};
using Filters = cetl::span<const Filter>;

struct RxFragment final
{
    TimePoint   timestamp;
    CanId       can_id;
    std::size_t payload_size;
};

class IMedia
{
public:
    virtual ~IMedia() = default;

    /// @brief Get the maximum transmission unit (MTU) of the CAN bus.
    ///
    /// This value may change arbitrarily at runtime. The transport implementation will query it before every
    /// transmission on the port. This value has no effect on the reception pipeline as it can accept arbitrary MTU.
    ///
    CETL_NODISCARD virtual std::size_t getMtu() const noexcept = 0;

    /// @brief Set the filters for the CAN bus.
    ///
    /// If there are fewer hardware filters available than requested, the configuration will be coalesced as described
    /// in the Cyphal/CAN Specification. If zero filters are requested, all incoming traffic will be rejected.
    /// While reconfiguration is in progress, incoming frames may be lost and/or unwanted frames may be received.
    /// The lifetime of the filter array may end upon return (no references retained).
    /// @return Returns `true` on success, `false` in case of a low-level error (e.g., IO error).
    ///
    CETL_NODISCARD virtual bool setFilters(const Filters filters) noexcept = 0;

    /// @brief Schedules the frame for transmission asynchronously and return immediately.
    ///
    /// @return Returns `true` if accepted or already timed out; `false` to try again later.
    ///
    CETL_NODISCARD virtual Expected<bool, cetl::variant<ArgumentError>> push(
        const TimePoint       deadline,
        const CanId           can_id,
        const PayloadFragment payload) noexcept = 0;

    /// @brief Takes the next fragment (aka CAN frame) from the reception queue unless it's empty.
    ///
    /// @param payload_buffer The payload of the frame will be written into the buffer (aka span).
    /// @return Description of a received fragment if available; otherwise an empty optional is returned immediately.
    ///
    CETL_NODISCARD virtual cetl::optional<RxFragment> pop(const PayloadFragment payload_buffer) noexcept = 0;

protected:
    IMedia()                         = default;
    IMedia(IMedia&&)                 = default;
    IMedia(const IMedia&)            = default;
    IMedia& operator=(IMedia&&)      = default;
    IMedia& operator=(const IMedia&) = default;

};  // IMedia

}  // namespace can
}  // namespace transport
}  // namespace libcyphal

#endif  // LIBCYPHAL_TRANSPORT_CAN_MEDIA_HPP_INCLUDED
