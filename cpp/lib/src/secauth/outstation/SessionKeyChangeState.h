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
#ifndef OPENDNP3_SESSIONKEYCHANGESTATE_H
#define OPENDNP3_SESSIONKEYCHANGESTATE_H

#include "app/HeaderWriter.h"
#include "gen/objects/Group120.h"
#include "secauth/AuthSizes.h"

#include "opendnp3/crypto/ICryptoProvider.h"
#include "opendnp3/gen/HMACType.h"
#include "opendnp3/gen/KeyStatus.h"
#include "opendnp3/gen/KeyWrapAlgorithm.h"
#include "opendnp3/logging/Logger.h"

#include <ser4cpp/container/SequenceTypes.h>

#include <array>
#include <cstdint>

namespace opendnp3
{

/**
 * Tracks the state of session key change on the outstation side.
 * Manages the key status response, challenge data generation, and KSQ tracking.
 */
class SessionKeyChangeState
{
public:
    SessionKeyChangeState(uint16_t challengeSize, Logger logger, ICryptoProvider& provider);

    /// Format a key status response (Group120Var5) into the writer
    bool FormatKeyStatusResponse(HeaderWriter& writer,
                                 uint16_t userNum,
                                 HMACType hmacType,
                                 KeyWrapAlgorithm keyWrapAlgo,
                                 KeyStatus status,
                                 const ser4cpp::rseq_t& hmac = ser4cpp::rseq_t());

    /// Check if serialized object data matches the last status response, byte for byte
    bool EqualsLastStatusResponse(const ser4cpp::rseq_t& object);

    /// Check if user and KSQ match the last key status response
    bool CheckUserAndKSQMatches(uint16_t userNum, uint32_t keyChangeSeq);

private:
    uint16_t lastUserNum;
    uint16_t challengeSize;
    Logger logger;
    ICryptoProvider* pProvider;
    uint32_t keyChangeSeqNum;
    std::array<uint8_t, AuthSizes::MAX_CHALLENGE_DATA_SIZE> challengeData;

    static const uint32_t MAX_KEY_STATUS_BUFFER_SIZE
        = Group120Var5::MIN_SIZE + AuthSizes::MAX_CHALLENGE_DATA_SIZE + AuthSizes::MAX_HMAC_OUTPUT_SIZE;

    // the last key status as sent: the challenge data and the HMAC are copies, not views into caller buffers
    std::array<uint8_t, MAX_KEY_STATUS_BUFFER_SIZE> lastStatus;
    size_t lastStatusSize;
};

} // namespace opendnp3

#endif
