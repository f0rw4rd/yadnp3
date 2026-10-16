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
#include <opendnp3/util/Timestamp.h>

#include <catch.hpp>

using namespace opendnp3;

#define SUITE(name) "TimestampTestSuite - " name

TEST_CASE(SUITE("Adding to a timestamp before zero doesn't saturate"))
{
    const auto start = Timestamp::Min();
    const auto later = start + TimeDuration::Milliseconds(5);
    REQUIRE(!later.IsMax());
    REQUIRE(later - start == TimeDuration::Milliseconds(5));
}

TEST_CASE(SUITE("Subtracting from a timestamp after zero doesn't saturate"))
{
    const auto end = Timestamp::Max();
    const auto earlier = end - TimeDuration::Milliseconds(5);
    REQUIRE(!earlier.IsMin());
    REQUIRE(end - earlier == TimeDuration::Milliseconds(5));
}

TEST_CASE(SUITE("Timestamp arithmetic saturates at the limits"))
{
    REQUIRE((Timestamp::Max() - TimeDuration::Milliseconds(5) + TimeDuration::Milliseconds(10)).IsMax());
    REQUIRE((Timestamp::Min() + TimeDuration::Milliseconds(5) - TimeDuration::Milliseconds(10)).IsMin());
    REQUIRE((Timestamp::Max() + TimeDuration::Max()).IsMax());
    REQUIRE((Timestamp::Min() - TimeDuration::Max()).IsMin());
}

TEST_CASE(SUITE("Adding a negative duration subtracts it"))
{
    const Timestamp t = Timestamp::Min() + TimeDuration::Seconds(100);
    REQUIRE(t + TimeDuration::Milliseconds(-5) == t - TimeDuration::Milliseconds(5));
    REQUIRE((Timestamp::Min() + TimeDuration::Min()).IsMin());
}
