/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Unit tests of the libuavcan CAN bus headers.
 */

#include "libuavcan/libuavcan.hpp"
#include "libuavcan/bus/can.hpp"
#include "gtest/gtest.h"

/**
 * Verify the immutable properties of CAN FD
 */
TEST(CanBusTest, TypeFD) {
    ASSERT_EQ(64U, libuavcan::bus::CAN::TypeFD::MaxFrameSizeBytes);
}

/**
 * Verify the immutable properties of CAN 2.0
 */
TEST(CanBusTest, Type2_0) {
    ASSERT_EQ(8U, libuavcan::bus::CAN::Type2_0::MaxFrameSizeBytes);
}
