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
#include "secauth/master/KeyWrap.h"

#include "logging/LogMacros.h"

#include "opendnp3/logging/LogLevels.h"

#include <ser4cpp/serialization/LittleEndian.h>

#include <cstring>

namespace opendnp3
{

bool KeyWrapBuffer::Wrap(IKeyWrapAlgo& algo,
                         const ser4cpp::rseq_t& updateKey,
                         const SessionKeysView& sessionKeys,
                         const ser4cpp::rseq_t& keyStatus,
                         Logger logger)
{
    if (!sessionKeys.IsValid())
    {
        SIMPLE_LOG_BLOCK(logger, flags::ERR, "Cannot wrap invalid session keys");
        return false;
    }

    const uint32_t keySize = sessionKeys.controlKey.length();
    const uint32_t WRAPPED_DATA_SIZE
        = 2 + sessionKeys.controlKey.length() + sessionKeys.monitorKey.length() + keyStatus.length();
    const uint32_t WRAPPED_DATA_SIZE_MOD8 = WRAPPED_DATA_SIZE % 8;
    const uint32_t WRAPPED_DATA_SIZE_WITH_PADDING
        = (WRAPPED_DATA_SIZE_MOD8 == 0) ? WRAPPED_DATA_SIZE : (WRAPPED_DATA_SIZE + (8 - WRAPPED_DATA_SIZE_MOD8));

    // Build the data to wrap into a temporary buffer
    std::array<uint8_t, AuthSizes::MAX_SESSION_KEY_WRAP_BUFFER_SIZE> dataToWrap;
    // the key status comes from the outstation, which decides its length
    if (WRAPPED_DATA_SIZE_WITH_PADDING > dataToWrap.size())
    {
        SIMPLE_LOG_BLOCK(logger, flags::ERR, "Session keys and key status too large to wrap");
        return false;
    }
    ser4cpp::wseq_t dest(dataToWrap.data(), WRAPPED_DATA_SIZE_WITH_PADDING);

    // Write the key length
    ser4cpp::UInt16::write_to(dest, static_cast<uint16_t>(keySize));

    // Write control key
    std::memcpy(dest, sessionKeys.controlKey, sessionKeys.controlKey.length());
    dest.advance(sessionKeys.controlKey.length());

    // Write monitor key
    std::memcpy(dest, sessionKeys.monitorKey, sessionKeys.monitorKey.length());
    dest.advance(sessionKeys.monitorKey.length());

    // Write key status
    std::memcpy(dest, keyStatus, keyStatus.length());
    dest.advance(keyStatus.length());

    // Zero any remaining padding
    while (dest.is_not_empty())
    {
        dest[0] = 0;
        dest.advance(1);
    }

    // Build a read view of the data we just wrote
    ser4cpp::rseq_t input(dataToWrap.data(), WRAPPED_DATA_SIZE_WITH_PADDING);
    ser4cpp::wseq_t output(this->buffer.data(), this->buffer.size());

    // Wrap the keys
    std::error_code ec;
    this->data = algo.WrapKey(updateKey, input, output, ec);

    if (ec)
    {
        SIMPLE_LOG_BLOCK(logger, flags::ERR, ec.message().c_str());
        return false;
    }

    return data.is_not_empty();
}

} // namespace opendnp3
