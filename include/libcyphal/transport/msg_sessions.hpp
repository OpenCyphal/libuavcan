/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT

#ifndef LIBCYPHAL_TRANSPORT_MSG_SESSIONS_HPP_INCLUDED
#define LIBCYPHAL_TRANSPORT_MSG_SESSIONS_HPP_INCLUDED

#include "session.hpp"

namespace libcyphal
{
namespace transport
{
namespace session
{

struct MessageRxParams final
{
    std::size_t extent_bytes;
    PortId      subject_id;
};

struct MessageTxParams final
{
    PortId subject_id;
};

class IMessageRxSession : public IRxSession
{
public:
    CETL_NODISCARD virtual MessageRxParams getParams() const noexcept = 0;

    /// @brief Receives a message from the transport layer.
    ///
    /// Method is not blocking, and will return immediately if no message is available.
    ///
    /// @return A message transfer if available; otherwise an empty optional.
    ///
    CETL_NODISCARD virtual cetl::optional<MessageRxTransfer> receive() = 0;
};

class IMessageTxSession : public IRunnable
{
public:
    CETL_NODISCARD virtual MessageTxParams getParams() const noexcept = 0;

    /// @brief Sends a message to the transport layer.
    ///
    /// @param metadata Additional metadata associated with the message.
    /// @param payload_fragments Segments of the message payload.
    /// @return `nullopt` in case of success; otherwise a transport error.
    ///
    CETL_NODISCARD virtual cetl::optional<AnyError> send(const TransferMetadata& metadata,
                                                         const PayloadFragments  payload_fragments) = 0;
};

}  // namespace session
}  // namespace transport
}  // namespace libcyphal

#endif  // LIBCYPHAL_TRANSPORT_MSG_SESSIONS_HPP_INCLUDED