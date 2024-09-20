/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT

#include "gtest_helpers.hpp"  // NOLINT(misc-include-cleaner)
#include "tracking_memory_resource.hpp"
#include "transport/svc_sessions_mock.hpp"
#include "transport/transport_gtest_helpers.hpp"
#include "transport/transport_mock.hpp"
#include "verification_utilities.hpp"
#include "virtual_time_scheduler.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/node/get_info.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/transport/svc_sessions.hpp>
#include <libcyphal/transport/types.hpp>
#include <libcyphal/types.hpp>

#include <uavcan/node/GetInfo_1_0.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>
#include <utility>

namespace
{

using libcyphal::TimePoint;
using namespace libcyphal::application;   // NOLINT This our main concern here in the unit tests.
using namespace libcyphal::presentation;  // NOLINT This our main concern here in the unit tests.
using namespace libcyphal::transport;     // NOLINT This our main concern here in the unit tests.

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::IsEmpty;
using testing::StrictMock;
using testing::ElementsAre;
using testing::VariantWith;

// https://github.com/llvm/llvm-project/issues/53444
// NOLINTBEGIN(misc-unused-using-decls, misc-include-cleaner)
using std::literals::chrono_literals::operator""s;
using std::literals::chrono_literals::operator""ms;
// NOLINTEND(misc-unused-using-decls, misc-include-cleaner)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

class TestNodeGetInfo : public testing::Test
{
protected:
    using UniquePtrReqRxSpec = RequestRxSessionMock::RefWrapper::Spec;
    using UniquePtrResTxSpec = ResponseTxSessionMock::RefWrapper::Spec;

    void SetUp() override
    {
        EXPECT_CALL(transport_mock_, getProtocolParams())
            .WillRepeatedly(Return(ProtocolParams{std::numeric_limits<TransferId>::max(), 0, 0}));
    }

    void TearDown() override
    {
        EXPECT_THAT(mr_.allocations, IsEmpty());
        EXPECT_THAT(mr_.total_allocated_bytes, mr_.total_deallocated_bytes);
    }

    TimePoint now() const
    {
        return scheduler_.now();
    }

    // MARK: Data members:

    // NOLINTBEGIN
    libcyphal::VirtualTimeScheduler scheduler_{};
    TrackingMemoryResource          mr_;
    StrictMock<TransportMock>       transport_mock_;
    // NOLINTEND
};

// MARK: - Tests:

TEST_F(TestNodeGetInfo, make)
{
    using Service = uavcan::node::GetInfo_1_0;

    Presentation presentation{mr_, scheduler_, transport_mock_};

    IRequestRxSession::OnReceiveCallback::Function req_rx_cb_fn;
    StrictMock<RequestRxSessionMock>               req_rx_session_mock;
    EXPECT_CALL(req_rx_session_mock, setOnReceiveCallback(_))  //
        .WillRepeatedly(Invoke([&](auto&& cb_fn) {             //
            req_rx_cb_fn = std::forward<IRequestRxSession::OnReceiveCallback::Function>(cb_fn);
        }));

    StrictMock<ResponseTxSessionMock> res_tx_session_mock;

    constexpr RequestRxParams rx_params{Service::Request::_traits_::ExtentBytes,
                                        Service::Request::_traits_::FixedPortId};
    EXPECT_CALL(transport_mock_, makeRequestRxSession(RequestRxParamsEq(rx_params)))  //
        .WillOnce(Invoke([&](const auto&) {                                           //
            return libcyphal::detail::makeUniquePtr<UniquePtrReqRxSpec>(mr_, req_rx_session_mock);
        }));
    constexpr ResponseTxParams tx_params{Service::Request::_traits_::FixedPortId};
    EXPECT_CALL(transport_mock_, makeResponseTxSession(ResponseTxParamsEq(tx_params)))  //
        .WillOnce(Invoke([&](const auto&) {                                             //
            return libcyphal::detail::makeUniquePtr<UniquePtrResTxSpec>(mr_, res_tx_session_mock);
        }));

    cetl::optional<node::GetInfo> get_info;

    ServiceRxTransfer request{{{{123, Priority::Fast}, {}}, NodeId{0x31}}, {}};

    scheduler_.scheduleAt(1s, [&](const auto&) {
        //
        auto maybe_get_info = node::GetInfo::make(presentation);
        ASSERT_THAT(maybe_get_info, VariantWith<node::GetInfo>(_));
        get_info.emplace(cetl::get<node::GetInfo>(std::move(maybe_get_info)));
    });
    scheduler_.scheduleAt(2s, [&](const auto&) {
        //
        EXPECT_CALL(res_tx_session_mock,
                    send(ServiceTxMetadataEq({{{123, Priority::Fast}, now() + 1s}, NodeId{0x31}}), _))  //
            .WillOnce(Invoke([this](const auto&, const auto fragments) {
                //
                Service::Response response{Service::Response::allocator_type{&mr_}};
                EXPECT_TRUE(libcyphal::verification_utilities::tryDeserialize(response, fragments));
                EXPECT_THAT(response.protocol_version.major, 1);
                return cetl::nullopt;
            }));

        request.metadata.rx_meta.timestamp = now();
        req_rx_cb_fn({request});
    });
    scheduler_.scheduleAt(3s, [&](const auto&) {
        //
        auto& info                  = get_info->response();
        info.software_version.major = 7;
        std::copy_n("test", 4, std::back_inserter(info.name));

        get_info->setResponseTimeout(100ms);

        EXPECT_CALL(res_tx_session_mock,
                    send(ServiceTxMetadataEq({{{124, Priority::Nominal}, now() + 100ms}, NodeId{0x31}}), _))  //
            .WillOnce(Invoke([this](const auto&, const auto fragments) {
                //
                Service::Response response{Service::Response::allocator_type{&mr_}};
                EXPECT_TRUE(libcyphal::verification_utilities::tryDeserialize(response, fragments));
                EXPECT_THAT(response.protocol_version.major, 1);
                EXPECT_THAT(response.software_version.major, 7);
                EXPECT_THAT(response.name, ElementsAre('t', 'e', 's', 't'));
                return cetl::nullopt;
            }));

        request.metadata.rx_meta.base.transfer_id = 124;
        request.metadata.rx_meta.base.priority    = Priority::Nominal;
        request.metadata.rx_meta.timestamp        = now();
        req_rx_cb_fn({request});
    });
    scheduler_.scheduleAt(9s, [&](const auto&) {
        //
        EXPECT_CALL(req_rx_session_mock, deinit()).Times(1);
        EXPECT_CALL(res_tx_session_mock, deinit()).Times(1);

        get_info.reset();
    });
    scheduler_.spinFor(10s);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

}  // namespace