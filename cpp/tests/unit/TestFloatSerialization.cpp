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
#include <ser4cpp/serialization/LittleEndian.h>

#include <catch.hpp>

#include <cmath>
#include <cstring>
#include <limits>

using namespace ser4cpp;

#define SUITE(name) "FloatSerializationTestSuite - " name

namespace
{

template<class T, class U> U Bits(T value)
{
    U bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void CheckDouble(double value)
{
    // the encoding is the IEEE 754 binary64 bit pattern, and decodes to the same value
    REQUIRE(DoubleFloat::to_uint64(value) == Bits<double, uint64_t>(value));
    REQUIRE(Bits<double, uint64_t>(DoubleFloat::to_double(DoubleFloat::to_uint64(value))) == Bits<double, uint64_t>(value));
}

void CheckFloat(float value)
{
    REQUIRE(SingleFloat::to_uint32(value) == Bits<float, uint32_t>(value));
    REQUIRE(Bits<float, uint32_t>(SingleFloat::to_float32(SingleFloat::to_uint32(value))) == Bits<float, uint32_t>(value));
}

} // namespace

TEST_CASE(SUITE("Doubles keep their bit pattern"))
{
    CheckDouble(0.0);
    CheckDouble(-0.0);
    CheckDouble(1.0);
    CheckDouble(-123.456);
    CheckDouble(std::numeric_limits<double>::max());
    CheckDouble(std::numeric_limits<double>::lowest());
    CheckDouble(std::numeric_limits<double>::min());
    CheckDouble(std::numeric_limits<double>::infinity());
    CheckDouble(-std::numeric_limits<double>::infinity());
}

TEST_CASE(SUITE("Subnormal doubles keep their value"))
{
    CheckDouble(std::numeric_limits<double>::denorm_min());
    CheckDouble(-std::numeric_limits<double>::denorm_min());
    CheckDouble(std::numeric_limits<double>::min() / 3);
}

TEST_CASE(SUITE("Floats keep their bit pattern"))
{
    CheckFloat(0.0f);
    CheckFloat(-0.0f);
    CheckFloat(1.5f);
    CheckFloat(-20.0f);
    CheckFloat(std::numeric_limits<float>::max());
    CheckFloat(std::numeric_limits<float>::min());
    CheckFloat(std::numeric_limits<float>::infinity());
    CheckFloat(-std::numeric_limits<float>::infinity());
}

TEST_CASE(SUITE("Subnormal floats keep their value"))
{
    CheckFloat(std::numeric_limits<float>::denorm_min());
    CheckFloat(-std::numeric_limits<float>::denorm_min());
    CheckFloat(std::numeric_limits<float>::min() / 3);
}

TEST_CASE(SUITE("NaN stays NaN"))
{
    REQUIRE(std::isnan(DoubleFloat::to_double(DoubleFloat::to_uint64(std::numeric_limits<double>::quiet_NaN()))));
    REQUIRE(std::isnan(SingleFloat::to_float32(SingleFloat::to_uint32(std::numeric_limits<float>::quiet_NaN()))));
}
