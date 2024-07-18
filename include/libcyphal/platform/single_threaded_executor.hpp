/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT

#ifndef LIBCYPHAL_PLATFORM_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED
#define LIBCYPHAL_PLATFORM_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED

#include "libcyphal/common/cavl/cavl.hpp"
#include "libcyphal/executor.hpp"
#include "libcyphal/types.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <tuple>
#include <utility>

namespace libcyphal
{
namespace platform
{

class SingleThreadedExecutor : public IExecutor
{
public:
    explicit SingleThreadedExecutor(cetl::pmr::memory_resource& memory_resource)
        : nodes_allocator_{&memory_resource}
    {
    }

    ~SingleThreadedExecutor() override
    {
        // Just in case release whatever callback nodes left, but properly used `Callback::Handle`-s
        // (aka "handle must not outlive executor") should have removed them all.
        //
        CETL_DEBUG_ASSERT(scheduled_nodes_.empty(), "");
        CETL_DEBUG_ASSERT(registered_nodes_.empty(), "");
        //
        // Note, we release only `registered_nodes_` tree here b/c "helper" `scheduled_nodes_` tree
        // is based on a subset of the same nodes allocated for the "master" `registered_nodes_` tree.
        releaseCallbackNodes(registered_nodes_);
    }

    SingleThreadedExecutor(const SingleThreadedExecutor&)                = delete;
    SingleThreadedExecutor(SingleThreadedExecutor&&) noexcept            = delete;
    SingleThreadedExecutor& operator=(const SingleThreadedExecutor&)     = delete;
    SingleThreadedExecutor& operator=(SingleThreadedExecutor&&) noexcept = delete;

    struct SpinResult
    {
        /// The deadline of the next scheduled callback to run,
        /// or `cetl::nullopt` if there are no scheduled callbacks.
        /// This can be used to let the application sleep/poll when there are no callbacks pending.
        cetl::optional<TimePoint> next_deadline;

        /// An approximation of the maximum lateness observed during the spin call
        /// (the real slack may be worse than the approximation).
        /// This is always non-negative.
        Duration worst_lateness;
    };

    CETL_NODISCARD SpinResult spinOnce()
    {
        SpinResult spin_result{};

        auto approx_now = TimePoint::min();

        while (auto* const scheduled_node = scheduled_nodes_.min())
        {
            // No linting b/c we know for sure the type of the node.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            auto& callback_node = static_cast<CallbackNode&>(*scheduled_node);
            CETL_DEBUG_ASSERT(callback_node.isScheduled(), "");

            const auto exec_time = callback_node.executionTime();
            if (approx_now < exec_time)
            {
                approx_now = now();
                if (approx_now < exec_time)
                {
                    spin_result.next_deadline = exec_time;
                    break;
                }
            }

            spin_result.worst_lateness = std::max(approx_now - exec_time, spin_result.worst_lateness);

            callback_node.isScheduled() = false;
            scheduled_nodes_.remove(&callback_node);

            if (callback_node.isAutoRemove())
            {
                registered_nodes_.remove(&callback_node);
                didRemoveCallback(callback_node.id());
            }

            callback_node(approx_now);

            if (callback_node.isAutoRemove())
            {
                destroyCallbackNode(&callback_node);
            }

        }  // while there is pending callback to execute

        return spin_result;
    }

    // MARK: - IExecutor

    TimePoint now() const noexcept override
    {
        const auto duration = std::chrono::steady_clock::now().time_since_epoch();
        return TimePoint{} + std::chrono::duration_cast<Duration>(duration);
    }

    using IExecutor::registerCallback;

protected:
    CETL_NODISCARD cetl::optional<Callback::Id> appendCallback(const bool           is_auto_remove,
                                                               Callback::Function&& function) override
    {
        CETL_DEBUG_ASSERT(function, "");

        auto* const new_callback_node = makeCallbackNode(std::move(function), is_auto_remove);
        if (nullptr == new_callback_node)
        {
            return cetl::nullopt;
        }

        ++last_callback_id_;
        const auto new_callback_id = last_callback_id_;
        new_callback_node->id()    = new_callback_id;

        const std::tuple<CallbackNode*, bool> reg_node_existing = registered_nodes_.search(  //
            [new_callback_id](const CallbackNode& node) {                                    // predicate
                return node.compareById(new_callback_id);
            },
            [new_callback_node]() { return new_callback_node; });  // factory

        (void) reg_node_existing;
        CETL_DEBUG_ASSERT(!std::get<1>(reg_node_existing), "Callback id collision detected.");
        CETL_DEBUG_ASSERT(new_callback_node == std::get<0>(reg_node_existing), "Unexpected not the new node.");

        return cetl::optional<Callback::Id>{new_callback_id};
    }

    bool scheduleCallbackByIdAt(const Callback::Id callback_id, const TimePoint time_point) override
    {
        auto* const callback_node = registered_nodes_.search(  //
            [callback_id](const CallbackNode& node) {          // predicate
                return node.compareById(callback_id);
            });
        if (nullptr == callback_node)
        {
            return false;
        }

        // Remove previously scheduled node (if any),
        // and then re/insert the node with updated/given execution time.
        //
        if (callback_node->isScheduled())
        {
            scheduled_nodes_.remove(callback_node);
        }
        callback_node->isScheduled()   = true;
        callback_node->executionTime() = time_point;
        //
        const std::tuple<ScheduledNode*, bool> sched_node_existing = scheduled_nodes_.search(  //
            [time_point](const ScheduledNode& scheduled_node) {                                // predicate
                return scheduled_node.compareByExecutionTime(time_point);
            },
            [callback_node]() { return callback_node; });  // factory

        (void) sched_node_existing;
        CETL_DEBUG_ASSERT(!std::get<1>(sched_node_existing), "Unexpected existing scheduled node.");
        CETL_DEBUG_ASSERT(callback_node == std::get<0>(sched_node_existing), "Unexpected callback node.");

        return true;
    }

    void removeCallbackById(const Callback::Id callback_id) override
    {
        auto* const callback_node = registered_nodes_.search(  //
            [callback_id](const CallbackNode& node) {          // predicate
                return node.compareById(callback_id);
            });
        if (nullptr == callback_node)
        {
            return;
        }

        if (callback_node->isScheduled())
        {
            scheduled_nodes_.remove(callback_node);
        }

        registered_nodes_.remove(callback_node);
        didRemoveCallback(callback_id);

        destroyCallbackNode(callback_node);
    }

    virtual void didRemoveCallback(const Callback::Id) {}

private:
    class ScheduledNode : public cavl::Node<ScheduledNode>
    {
    public:
        ScheduledNode(const ScheduledNode&)                = delete;
        ScheduledNode(ScheduledNode&&) noexcept            = delete;
        ScheduledNode& operator=(const ScheduledNode&)     = delete;
        ScheduledNode& operator=(ScheduledNode&&) noexcept = delete;

        bool isAutoRemove() const noexcept
        {
            return is_auto_remove_;
        }

        bool& isScheduled() noexcept
        {
            return is_scheduled_;
        }

        TimePoint& executionTime() noexcept
        {
            return execution_time_;
        }

        void operator()(const TimePoint time_point) const
        {
            function_(time_point);
        }

        CETL_NODISCARD std::int8_t compareByExecutionTime(const TimePoint execution_time) const noexcept
        {
            // No two execution times compare equal, which allows us to have multiple nodes
            // with the same execution time in the tree. With two nodes sharing the same execution time,
            // the one added later is considered to be later.
            return (execution_time >= execution_time_) ? +1 : -1;
        }

    protected:
        ScheduledNode(Callback::Function&& function, const bool is_auto_remove)
            : function_{std::move(function)}
            , is_auto_remove_{is_auto_remove}
            , is_scheduled_{false}
        {
        }
        ~ScheduledNode() = default;

    private:
        // MARK: Data members:

        const Callback::Function function_;
        const bool               is_auto_remove_;
        bool                     is_scheduled_;
        TimePoint                execution_time_;

    };  // ScheduledNode

    class CallbackNode final : public cavl::Node<CallbackNode>, public ScheduledNode
    {
    public:
        using cavl::Node<CallbackNode>::getChildNode;

        CallbackNode(Callback::Function&& function, const bool is_auto_remove)
            : ScheduledNode{std::move(function), is_auto_remove}
            , id_{0}
        {
        }
        ~CallbackNode() = default;

        CallbackNode(const CallbackNode&)                = delete;
        CallbackNode(CallbackNode&&) noexcept            = delete;
        CallbackNode& operator=(const CallbackNode&)     = delete;
        CallbackNode& operator=(CallbackNode&&) noexcept = delete;

        Callback::Id& id() noexcept
        {
            return id_;
        }

        CETL_NODISCARD std::int8_t compareById(const Callback::Id id) const noexcept
        {
            if (id == id_)
            {
                return 0;
            }
            return (id > id_) ? +1 : -1;
        }

    private:
        // MARK: Data members:

        Callback::Id id_;

    };  // CallbackNode

    CETL_NODISCARD CallbackNode* makeCallbackNode(Callback::Function&& function, const bool is_auto_remove)
    {
        CallbackNode* const node = nodes_allocator_.allocate(1);
        if (nullptr != node)
        {
            nodes_allocator_.construct(node, std::move(function), is_auto_remove);
        }
        return node;
    }

    void destroyCallbackNode(CallbackNode* const callback_node)
    {
        CETL_DEBUG_ASSERT(nullptr != callback_node, "");

        // No Sonar cpp:M23_329 b/c we do our own low-level PMR management here.
        callback_node->~CallbackNode();  // NOSONAR cpp:M23_329
        nodes_allocator_.deallocate(callback_node, 1);
    }

    /// @brief Recursively releases all callback nodes.
    ///
    /// AVL tree is balanced, hence the `NOLINT(misc-no-recursion)` and `NOSONAR cpp:S925` exceptions.
    /// TODO: Add "post-order" traversal support to the AVL tree.
    ///
    void releaseCallbackNodes(CallbackNode* node)  // NOLINT(misc-no-recursion)
    {
        if (nullptr != node)
        {
            releaseCallbackNodes(node->getChildNode(false));  // NOSONAR cpp:S925
            releaseCallbackNodes(node->getChildNode(true));   // NOSONAR cpp:S925

            destroyCallbackNode(node);
        }
    }

    // MARK: - Data members:

    libcyphal::detail::PmrAllocator<CallbackNode> nodes_allocator_;
    cavl::Tree<ScheduledNode>                     scheduled_nodes_;
    cavl::Tree<CallbackNode>                      registered_nodes_;
    Callback::Id                                  last_callback_id_{0};

};  // SingleThreadedExecutor

}  // namespace platform
}  // namespace libcyphal

#endif  // LIBCYPHAL_PLATFORM_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED
