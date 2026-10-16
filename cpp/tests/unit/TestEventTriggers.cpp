/*
 * Copyright 2013-2022 Step Function I/O, LLC
 *
 * Licensed to Green Energy Corp (www.greenenergycorp.com) and Step Function I/O
 * LLC (https://stepfunc.io) under one or more contributor license agreements.
 * See the NOTICE file distributed with this work for additional information
 * regarding copyright ownership. Green Energy Corp and Step Function I/O LLC license
 * this file to you under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License. You may obtain
 * a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <app/MeasurementTypeSpecs.h>

#include <catch.hpp>

#include <limits>

using namespace opendnp3;

#define SUITE(name) "EventTriggersTestSuite - " name

namespace
{
bool IsAnalogEvent(double oldValue, double newValue, double deadband)
{
    AnalogConfig config;
    config.deadband = deadband;
    return AnalogSpec::IsEvent(Analog(oldValue), Analog(newValue), config);
}
} // namespace

TEST_CASE(SUITE("An analog change within the deadband is no event"))
{
    REQUIRE_FALSE(IsAnalogEvent(0, 0, 0));
    REQUIRE_FALSE(IsAnalogEvent(1.0, 1.5, 1.0));
    REQUIRE(IsAnalogEvent(1.0, 2.5, 1.0));
}

TEST_CASE(SUITE("An analog change to or from NaN is an event"))
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(IsAnalogEvent(1.0, nan, 0));
    REQUIRE(IsAnalogEvent(nan, 1.0, 0));
    REQUIRE(IsAnalogEvent(nan, 1.0, 1000));
    REQUIRE_FALSE(IsAnalogEvent(nan, nan, 0));
}

TEST_CASE(SUITE("An analog change to or from infinity is an event"))
{
    const auto inf = std::numeric_limits<double>::infinity();
    REQUIRE(IsAnalogEvent(1.0, inf, 0));
    REQUIRE(IsAnalogEvent(inf, -inf, 0));
    REQUIRE_FALSE(IsAnalogEvent(inf, inf, 0));
}
