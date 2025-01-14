/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT

#ifndef LIBCYPHAL_TRANSPORT_UDP_SESSION_TREE_HPP_INCLUDED
#define LIBCYPHAL_TRANSPORT_UDP_SESSION_TREE_HPP_INCLUDED

#include "delegate.hpp"
#include "tx_rx_sockets.hpp"

#include "libcyphal/common/cavl/cavl.hpp"
#include "libcyphal/executor.hpp"
#include "libcyphal/transport/errors.hpp"
#include "libcyphal/transport/types.hpp"
#include "libcyphal/types.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <udpard.h>

#include <array>
#include <cstdint>
#include <functional>

namespace libcyphal
{
namespace transport
{
namespace udp
{

/// Internal implementation details of the UDP transport.
/// Not supposed to be used directly by the users of the library.
///
namespace detail
{

template <typename SocketInterface>
struct SocketState
{
    UniquePtr<SocketInterface> interface;
    IExecutor::Callback::Any   callback;

};  // SocketState

/// @brief Defines a tree of sessions for the UDP transport.
///
template <typename Node>
class SessionTree final
{
public:
    using NodeRef = typename Node::ReferenceWrapper;

    explicit SessionTree(cetl::pmr::memory_resource& mr)
        : allocator_{&mr}
    {
    }

    SessionTree(const SessionTree&)                = delete;
    SessionTree(SessionTree&&) noexcept            = delete;
    SessionTree& operator=(const SessionTree&)     = delete;
    SessionTree& operator=(SessionTree&&) noexcept = delete;

    ~SessionTree()
    {
        nodes_.traversePostOrder([this](auto& node) { destroyNode(node); });
    }

    CETL_NODISCARD bool isEmpty() const noexcept
    {
        return nodes_.empty();
    }

    CETL_NODISCARD Expected<NodeRef, AnyFailure> ensureNewNodeFor(const PortId port_id)
    {
        const auto node_existing = nodes_.search(
            [port_id](const Node& node) {  // predicate
                //
                return node.compareByPortId(port_id);
            },
            [this, port_id]() {  // factory
                //
                return constructNewNode(port_id);
            });

        auto* const node = std::get<0>(node_existing);
        if (nullptr == node)
        {
            return MemoryError{};
        }
        if (std::get<1>(node_existing))
        {
            return AlreadyExistsError{};
        }

        return *node;
    }

    void removeNodeFor(const PortId port_id)
    {
        removeAndDestroyNode(nodes_.search([port_id](const Node& node) {  // predicate
            //
            return node.compareByPortId(port_id);
        }));
    }

    template <typename Action>
    CETL_NODISCARD cetl::optional<AnyFailure> forEachNode(Action&& action)
    {
        return nodes_.traverse(std::forward<Action>(action));
    }

private:
    CETL_NODISCARD Node* constructNewNode(const PortId port_id)
    {
        Node* const node = allocator_.allocate(1);
        if (nullptr != node)
        {
            allocator_.construct(node, port_id);
        }
        return node;
    }

    void removeAndDestroyNode(Node* node)
    {
        if (nullptr != node)
        {
            nodes_.remove(node);
            destroyNode(*node);
        }
    }

    void destroyNode(Node& node)
    {
        // No Sonar cpp:M23_329 b/c we do our own low-level PMR management here.
        node.~Node();  // NOSONAR cpp:M23_329
        allocator_.deallocate(&node, 1);
    }

    // MARK: Data members:

    common::cavl::Tree<Node>              nodes_;
    libcyphal::detail::PmrAllocator<Node> allocator_;

};  // SessionTree

// MARK: -

struct RxSessionTreeNode
{
    template <typename Derived>
    class Base : public common::cavl::Node<Derived>
    {
    public:
        using common::cavl::Node<Derived>::getChildNode;
        using ReferenceWrapper = std::reference_wrapper<Derived>;

        explicit Base(const PortId port_id)
            : port_id_{port_id}
        {
        }

        CETL_NODISCARD std::int32_t compareByPortId(const PortId port_id) const
        {
            return static_cast<std::int32_t>(port_id_) - static_cast<std::int32_t>(port_id);
        }

    private:
        // MARK: Data members:

        PortId port_id_;

    };  // Base

    /// @brief Represents a message RX session node.
    ///
    class Message final : public Base<Message>
    {
    public:
        using Base::Base;

        CETL_NODISCARD IMsgRxSessionDelegate*& delegate() noexcept
        {
            return delegate_;
        }

        CETL_NODISCARD SocketState<IRxSocket>& socketState(const std::uint8_t media_index) noexcept
        {
            CETL_DEBUG_ASSERT(media_index < socket_states_.size(), "");

            // No lint b/c at transport constructor we made sure that number of media interfaces is bound.
            return socket_states_[media_index];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        }

    private:
        // MARK: Data members:

        IMsgRxSessionDelegate*                                                 delegate_{nullptr};
        std::array<SocketState<IRxSocket>, UDPARD_NETWORK_INTERFACE_COUNT_MAX> socket_states_;

    };  // Message

    /// @brief Represents a service request RX session node.
    ///
    class Request final : public Base<Request>
    {
    public:
        using Base::Base;
    };

    /// @brief Represents a service response RX session node.
    ///
    class Response final : public Base<Response>
    {
    public:
        using Base::Base;
    };

};  // RxSessionTreeNode

}  // namespace detail
}  // namespace udp
}  // namespace transport
}  // namespace libcyphal

#endif  // LIBCYPHAL_TRANSPORT_UDP_SESSION_TREE_HPP_INCLUDED
