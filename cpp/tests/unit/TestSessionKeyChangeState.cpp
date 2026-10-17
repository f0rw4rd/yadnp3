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
#include <app/APDUResponse.h>
#include <crypto/CryptoProvider.h>
#include <secauth/outstation/SessionKeyChangeState.h>

#include <catch.hpp>

#include <algorithm>
#include <array>
#include <vector>

using namespace opendnp3;

#define SUITE(name) "SessionKeyChangeStateTestSuite - " name

namespace
{

struct KeyStatusFixture
{
    KeyStatusFixture() : state(4, Logger(nullptr, ModuleId(), "test", LogLevels::everything()), crypto) {}

    // formats a key status response and returns its object, which the master echoes back in its key change
    std::vector<uint8_t> Format(const std::vector<uint8_t>& hmac)
    {
        APDUResponse response(ser4cpp::wseq_t(buffer.data(), buffer.size()));
        auto writer = response.GetWriter();
        REQUIRE(state.FormatKeyStatusResponse(writer, 1, HMACType::HMAC_SHA256_TRUNC_16, KeyWrapAlgorithm::AES_128,
                                              KeyStatus::OK, ser4cpp::rseq_t(hmac.data(), hmac.size())));
        // the response header, then the free-format object header
        const auto object = response.ToRSeq().skip(4 + 6);
        const uint8_t* bytes = object;
        return std::vector<uint8_t>(bytes, bytes + object.length());
    }

    bool Equals(const std::vector<uint8_t>& object)
    {
        return state.EqualsLastStatusResponse(ser4cpp::rseq_t(object.data(), object.size()));
    }

    CryptoProvider crypto;
    SessionKeyChangeState state;
    std::array<uint8_t, 2048> buffer{};
};

} // namespace

TEST_CASE(SUITE("The last key status matches itself"))
{
    KeyStatusFixture status;
    const auto object = status.Format({});
    REQUIRE(status.Equals(object));
}

TEST_CASE(SUITE("A key status with other challenge data doesn't match"))
{
    KeyStatusFixture status;
    auto object = status.Format({});
    // KSQ(4) user(2) wrap(1) status(1) mac(1) length(2), then the challenge data
    object[11] ^= 0xFF;
    REQUIRE_FALSE(status.Equals(object));
}

TEST_CASE(SUITE("An older key status doesn't match"))
{
    KeyStatusFixture status;
    const auto first = status.Format({});
    status.Format({});
    REQUIRE_FALSE(status.Equals(first));
}

TEST_CASE(SUITE("Nothing matches before a key status was sent"))
{
    KeyStatusFixture status;
    REQUIRE_FALSE(status.Equals(std::vector<uint8_t>(11, 0)));
}

TEST_CASE(SUITE("The key status keeps its own copy of the HMAC"))
{
    KeyStatusFixture status;
    std::vector<uint8_t> hmac(16, 0xAA);
    const auto object = status.Format(hmac);
    std::fill(hmac.begin(), hmac.end(), 0x55);
    REQUIRE(status.Equals(object));
    auto changed = object;
    changed.back() ^= 0xFF;
    REQUIRE_FALSE(status.Equals(changed));
}
