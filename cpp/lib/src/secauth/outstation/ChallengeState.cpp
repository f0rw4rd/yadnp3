/*
 * Copyright 2013-2022 Step Function I/O, LLC
 * Copyright 2024-2026 f0rw4rd (experimental fork)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "secauth/outstation/ChallengeState.h"

#include "app/AppControlField.h"
#include "gen/objects/Group120.h"
#include "logging/LogMacros.h"

#include "opendnp3/crypto/SecureCompare.h"
#include "opendnp3/logging/LogLevels.h"

#include <cstring>

namespace opendnp3
{

ChallengeState::ChallengeState(uint16_t challengeSize, uint32_t maxRxASDUSize)
    : CHALLENGE_SIZE(AuthSizes::GetBoundedChallengeSize(challengeSize)), criticalASDU(), seqNumber(0)
{
    challengeFragmentBuffer.fill(0);
    challengeDataBuffer.fill(0);
}

bool ChallengeState::WriteChallenge(const ser4cpp::rseq_t& fragment,
                                    const APDUHeader& header,
                                    APDUResponse& response,
                                    HMACType hmacType,
                                    ICryptoProvider& crypto,
                                    Logger* pLogger)
{
    // Store the critical ASDU: a request too large to store can't be authorized later, so it isn't challenged, and
    // any earlier challenge is void
    this->challengeFragment = ser4cpp::rseq_t::empty();
    if (!criticalASDU.Set(fragment))
    {
        criticalASDU.Clear();
        SIMPLE_LOGGER_BLOCK(pLogger, flags::ERR, "Critical request too large to challenge");
        return false;
    }

    // Configure the response
    response.SetFunction(FunctionCode::AUTH_RESPONSE);
    response.SetControl(header.control);

    // Generate random challenge data
    ser4cpp::wseq_t challengeDest(challengeDataBuffer.data(), CHALLENGE_SIZE);
    std::error_code ec;
    auto random = crypto.GetSecureRandom(challengeDest, ec);
    if (ec)
    {
        SIMPLE_LOGGER_BLOCK(pLogger, flags::ERR, ec.message().c_str());
        return false;
    }

    this->challengeData = random;
    ++seqNumber;

    Group120Var1 challengeObj(seqNumber, 0 /* UNKNOWN_ID */, hmacType, ChallengeReason::CRITICAL, challengeData);

    if (!response.GetWriter().WriteFreeFormat(challengeObj))
    {
        SIMPLE_LOGGER_BLOCK(pLogger, flags::ERR, "Insufficient space to write challenge to buffer");
        return false;
    }

    // Copy the response fragment so we can calculate the HMAC later
    auto asdu = response.ToRSeq();
    if (asdu.length() > challengeFragmentBuffer.size())
    {
        SIMPLE_LOGGER_BLOCK(pLogger, flags::ERR, "Insufficient space to persist challenge message");
        return false;
    }

    std::memcpy(challengeFragmentBuffer.data(), asdu, asdu.length());
    this->challengeFragment = ser4cpp::rseq_t(challengeFragmentBuffer.data(), asdu.length());

    return true;
}

bool ChallengeState::VerifyAuthenticity(const ser4cpp::rseq_t& key,
                                        HMACProvider& provider,
                                        const ser4cpp::rseq_t& hmac,
                                        Logger logger)
{
    // a challenge authorizes one reply, right or wrong: a reply seen again is a replay
    const auto fragment = this->challengeFragment;
    this->challengeFragment = ser4cpp::rseq_t::empty();
    if (fragment.is_empty() || !criticalASDU.IsSet())
    {
        SIMPLE_LOG_BLOCK(logger, flags::WARN, "No challenge outstanding");
        return false;
    }

    if (provider.OutputSize() != hmac.length())
    {
        FORMAT_LOG_BLOCK(logger, flags::WARN, "Received HMAC length of %zu did not match expected HMAC length of %u",
                         hmac.length(), provider.OutputSize());
        return false;
    }

    std::error_code ec;
    auto hmacCalc = provider.Compute(key, {fragment, criticalASDU.Get()}, ec);

    if (ec)
    {
        SIMPLE_LOG_BLOCK(logger, flags::ERR, ec.message().c_str());
        return false;
    }

    if (!SecureEquals(hmac, hmacCalc))
    {
        SIMPLE_LOG_BLOCK(logger, flags::WARN, "HMAC comparison failed");
        return false;
    }

    return true;
}

ser4cpp::rseq_t ChallengeState::GetCriticalASDU() const
{
    return criticalASDU.Get();
}

APDUHeader ChallengeState::GetCriticalHeader() const
{
    // Parse the header from the stored critical ASDU
    APDUHeader header;
    auto data = criticalASDU.Get();
    if (data.length() >= 2)
    {
        // a request starts with its application control octet, then its function code
        header.control = AppControlField(data[0]);
        header.function = static_cast<FunctionCode>(data[1]);
    }
    return header;
}

} // namespace opendnp3
