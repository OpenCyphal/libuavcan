/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT

#ifndef LIBCYPHAL_TRANSPORT_UDP_SESSION_TREE_HPP
#define LIBCYPHAL_TRANSPORT_UDP_SESSION_TREE_HPP

#include "libcyphal/common/cavl/cavl.hpp"
#include "libcyphal/transport/errors.hpp"
#include "libcyphal/transport/types.hpp"
#include "libcyphal/types.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

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

/// @brief Defines a tree of sessions for the UDP transport.
///
/// No Sonar cpp:S4963 b/c we do directly handle resources here.
///
template <typename Node>
class SessionTree final  // NOSONAR cpp:S4963
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
        releaseNodes(nodes_);
    }

    CETL_NODISCARD Expected<NodeRef, AnyError> ensureNewNodeFor(const PortId port_id)
    {
        auto const node_existing = nodes_.search([port_id](const Node& node) { return node.compareWith(port_id); },
                                                 [port_id, this]() { return constructNewNode(port_id); });

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
        removeAndDestroyNode(nodes_.search([port_id](const Node& node) { return node.compareWith(port_id); }));
    }

private:
    Node* constructNewNode(const PortId port_id)
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
            destroyNode(node);
        }
    }

    /// @brief Recursively releases each remaining node of the AVL tree.
    ///
    /// Recursion goes first to the left child, then to the right child, and finally to the current node.
    /// In such order it is guaranteed that the current node have no children anymore, and so can be deallocated.
    ///
    /// B/c AVL tree is balanced, the total complexity is `O(n)` and call stack depth should not be deeper than
    /// ~O(log(N)) (`ceil(1.44 * log2(N + 2) - 0.328)` to be precise, or 19 in case of 8192 ports),
    /// where `N` is the total number of tree nodes. Hence, the `NOLINT(misc-no-recursion)`
    /// and `NOSONAR cpp:S925` exceptions.
    ///
    void releaseNodes(Node* node)  // NOLINT(misc-no-recursion)
    {
        if (nullptr != node)
        {
            releaseNodes(node->getChildNode(false));  // NOSONAR cpp:S925
            releaseNodes(node->getChildNode(true));   // NOSONAR cpp:S925

            destroyNode(node);
        }
    }

    void destroyNode(Node* node)
    {
        CETL_DEBUG_ASSERT(nullptr != node, "");

        // No Sonar cpp:M23_329 b/c we do our own low-level PMR management here.
        node->~Node();  // NOSONAR cpp:M23_329
        allocator_.deallocate(node, 1);
    }

    // MARK: Data members:

    cavl::Tree<Node>                      nodes_;
    libcyphal::detail::PmrAllocator<Node> allocator_;

};  // SessionTree

// MARK: -

struct RxSessionTreeNode
{
    template <typename Derived>
    class Base : public cavl::Node<Derived>
    {
    public:
        using cavl::Node<Derived>::getChildNode;
        using ReferenceWrapper = std::reference_wrapper<Derived>;

        explicit Base(const PortId port_id)
            : port_id_{port_id}
        {
        }

        std::int32_t compareWith(const PortId port_id) const
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
    };

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

#endif  // LIBCYPHAL_TRANSPORT_UDP_SESSION_TREE_HPP
