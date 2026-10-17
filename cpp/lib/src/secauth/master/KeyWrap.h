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
#ifndef OPENDNP3_KEYWRAP_H
#define OPENDNP3_KEYWRAP_H

#include "secauth/AuthSizes.h"
#include "secauth/SessionKeysView.h"

#include "opendnp3/crypto/IKeyWrapAlgo.h"
#include "opendnp3/logging/Logger.h"

#include <ser4cpp/container/SequenceTypes.h>

#include <array>

namespace opendnp3
{

/**
 * Buffer for wrapping session keys for transmission to an outstation.
 * The wrapped data format is: keyLength(2) + controlKey(N) + monitorKey(N) + keyStatusMessage(M) + padding
 * The data is padded to an 8-byte boundary as required by AES key wrap.
 */
class KeyWrapBuffer
{
public:
    bool Wrap(IKeyWrapAlgo& algo,
              const ser4cpp::rseq_t& updateKey,
              const SessionKeysView& sessionKeys,
              const ser4cpp::rseq_t& keyStatus,
              Logger logger);

    ser4cpp::rseq_t GetWrappedData() const
    {
        return data;
    }

private:
    ser4cpp::rseq_t data;
    // RFC 3394 key wrap adds 8 bytes to the data it wraps
    std::array<uint8_t, AuthSizes::MAX_SESSION_KEY_WRAP_BUFFER_SIZE + 8> buffer;
};

} // namespace opendnp3

#endif
