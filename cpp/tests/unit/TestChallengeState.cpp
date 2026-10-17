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
#include "utils/BufferHelpers.h"

#include <app/APDUResponse.h>
#include <crypto/CryptoProvider.h>
#include <secauth/HMACProvider.h>
#include <secauth/outstation/ChallengeState.h>

#include <catch.hpp>

#include <array>
#include <vector>

using namespace opendnp3;

#define SUITE(name) "ChallengeStateTestSuite - " name

namespace
{

struct Challenge
{
    explicit Challenge(uint16_t size) : state(size, 2048) {}

    // challenges the critical request, and returns the HMAC a master holding the key answers with
    std::vector<uint8_t> Issue(const std::string& requestHex)
    {
        HexSequence request(requestHex);
        APDUResponse response(ser4cpp::wseq_t(buffer.data(), buffer.size()));
        REQUIRE(state.WriteChallenge(request.ToRSeq(), APDUHeader::SolicitedConfirm(3), response, HMACType::HMAC_SHA256_TRUNC_16,
                                     crypto, nullptr));
        const auto fragment = response.ToRSeq();
        std::error_code ec;
        const auto hmac = provider.Compute(Key(), {fragment, request.ToRSeq()}, ec);
        REQUIRE(!ec);
        const uint8_t* bytes = hmac;
        return std::vector<uint8_t>(bytes, bytes + hmac.length());
    }

    bool Verify(const std::vector<uint8_t>& hmac)
    {
        return state.VerifyAuthenticity(Key(), provider, ser4cpp::rseq_t(hmac.data(), hmac.size()),
                                        Logger(nullptr, ModuleId(), "test", LogLevels::everything()));
    }

    ser4cpp::rseq_t Key() const
    {
        return ser4cpp::rseq_t(key.data(), key.size());
    }

    CryptoProvider crypto;
    HMACProvider provider{crypto, HMACMode::SHA256_TRUNC_16};
    ChallengeState state;
    std::array<uint8_t, 2048> buffer{};
    std::array<uint8_t, 16> key{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
};

} // namespace

TEST_CASE(SUITE("A challenge of the largest size fits"))
{
    Challenge challenge(64);
    challenge.Issue("C3 05 0C 01 28 01 00 01 00 03 01 64 00 00 00 64 00 00 00 00");
}

TEST_CASE(SUITE("The critical request's header is its control and function, in that order"))
{
    Challenge challenge(4);
    challenge.Issue("C3 05 0C 01 28 01 00 01 00 03 01 64 00 00 00 64 00 00 00 00");
    const auto header = challenge.state.GetCriticalHeader();
    REQUIRE(header.function == FunctionCode::DIRECT_OPERATE);
    REQUIRE(header.control.SEQ == 3);
    REQUIRE(header.control.FIR);
    REQUIRE(header.control.FIN);
}

TEST_CASE(SUITE("A challenge authorizes its request once"))
{
    Challenge challenge(4);
    const auto hmac = challenge.Issue("C3 05 0C 01 28 01 00 01 00 03 01 64 00 00 00 64 00 00 00 00");
    REQUIRE(challenge.Verify(hmac));
    // the same reply again is a replay
    REQUIRE_FALSE(challenge.Verify(hmac));
}

TEST_CASE(SUITE("A wrong HMAC uses up the challenge"))
{
    Challenge challenge(4);
    auto hmac = challenge.Issue("C3 05 0C 01 28 01 00 01 00 03 01 64 00 00 00 64 00 00 00 00");
    auto wrong = hmac;
    wrong[0] ^= 0xFF;
    REQUIRE_FALSE(challenge.Verify(wrong));
    REQUIRE_FALSE(challenge.Verify(hmac));
}

TEST_CASE(SUITE("A request too large to store isn't challenged"))
{
    Challenge challenge(4);
    std::string request = "C3 05";
    for (int i = 0; i < 5000; ++i)
        request += " 00";
    HexSequence bytes(request);
    APDUResponse response(ser4cpp::wseq_t(challenge.buffer.data(), challenge.buffer.size()));
    REQUIRE_FALSE(challenge.state.WriteChallenge(bytes.ToRSeq(), APDUHeader::SolicitedConfirm(3), response,
                                                 HMACType::HMAC_SHA256_TRUNC_16, challenge.crypto, nullptr));
}
