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
#ifndef OPENDNP3_AUTHSIZES_H
#define OPENDNP3_AUTHSIZES_H

#include "opendnp3/util/StaticOnly.h"

#include <algorithm>
#include <cstdint>

namespace opendnp3
{

struct AuthSizes : StaticOnly
{
    static const uint32_t MAX_USER_NAME_SIZE = 32;

    static const uint32_t MIN_CHALLENGE_DATA_SIZE = 4;
    static const uint32_t MAX_CHALLENGE_DATA_SIZE = 64;
    static const uint32_t DEFAULT_CHALLENGE_SIZE = 4;

    static const uint32_t MIN_SESSION_KEY_SIZE_BYTES = 16;
    static const uint32_t MAX_SESSION_KEY_SIZE_BYTES = 32;

    static const uint32_t MIN_UPDATE_KEY_SIZE_BYTES = 16;
    static const uint32_t MAX_UPDATE_KEY_SIZE_BYTES = 32;
    static const uint32_t MAX_UPDATE_KEY_SIZE = 32;

    static const uint16_t MAX_HMAC_TRUNC_SIZE = 16;
    static const uint32_t MAX_HMAC_OUTPUT_SIZE = 32;

    /// Free-format object header: group(1) + variation(1) + qualifier(1) + count(1) + size(2) = 6
    /// plus 2 bytes for APDU header
    static const uint8_t FREE_FORMAT_HEADER_SIZE = 8;

    /// Maximum buffer sizes for AES key wrap operations (padded to 8-byte boundaries)
    static const uint32_t MAX_SESSION_KEY_WRAP_BUFFER_SIZE
        = ((2 + 2 * MAX_SESSION_KEY_SIZE_BYTES + 128 + MAX_CHALLENGE_DATA_SIZE + 7) / 8) * 8;

    static const uint32_t MAX_UPDATE_KEY_UNWRAP_BUFFER_SIZE
        = ((8 + MAX_USER_NAME_SIZE + MAX_UPDATE_KEY_SIZE_BYTES + MAX_CHALLENGE_DATA_SIZE + 7) / 8) * 8;

    static const uint32_t MAX_MASTER_CHALLENGE_REPLY_FRAG_SIZE = FREE_FORMAT_HEADER_SIZE + 32 + MAX_HMAC_TRUNC_SIZE;

    /// Maximum size of an outstation challenge response fragment
    /// 4 (response header, with the IIN) + 6 (free-format header) + Group120Var1 max size
    /// Group120Var1: seqNum(4) + user(2) + hmacAlgo(1) + reason(1) + challengeData(up to 64)
    static const uint32_t MAX_OUTSTATION_CHALLENGE_RESPONSE_FRAGMENT_SIZE
        = 4 + 6 + 4 + 2 + 1 + 1 + MAX_CHALLENGE_DATA_SIZE;

    static uint32_t GetBoundedSessionKeySize(uint32_t size)
    {
        // no std::min/max, which would odr-use the constants (see GetBoundedChallengeSize)
        if (size < MIN_SESSION_KEY_SIZE_BYTES)
        {
            return MIN_SESSION_KEY_SIZE_BYTES;
        }
        return (size > MAX_SESSION_KEY_SIZE_BYTES) ? MAX_SESSION_KEY_SIZE_BYTES : size;
    }

    static bool SessionKeySizeWithinLimits(uint32_t size)
    {
        return size >= MIN_SESSION_KEY_SIZE_BYTES && size <= MAX_SESSION_KEY_SIZE_BYTES;
    }

    static bool ChallengeDataSizeWithinLimits(uint32_t size)
    {
        return size >= MIN_CHALLENGE_DATA_SIZE && size <= MAX_CHALLENGE_DATA_SIZE;
    }

    static uint32_t GetBoundedChallengeSize(uint32_t size)
    {
        // std::min/max bind by reference, which ODR-uses the in-class constants and
        // leaves them undefined at link time in unoptimized C++14 builds
        if (size < MIN_CHALLENGE_DATA_SIZE)
        {
            return MIN_CHALLENGE_DATA_SIZE;
        }
        return (size > MAX_CHALLENGE_DATA_SIZE) ? MAX_CHALLENGE_DATA_SIZE : size;
    }
};

} // namespace opendnp3

#endif
