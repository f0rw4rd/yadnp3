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
#include "secauth/outstation/EncryptedUpdateKey.h"

#include "secauth/AuthSizes.h"
#include "secauth/StringConversions.h"
#include "secauth/outstation/OutstationErrorCodes.h"

#include "opendnp3/crypto/SecureCompare.h"

#include <array>
#include <cstring>

namespace opendnp3
{

bool EncryptedUpdateKey::DecryptAndVerify(IKeyWrapAlgo& algorithm,
                                          const ser4cpp::rseq_t& kek,
                                          const ser4cpp::rseq_t& encryptedData,
                                          const std::string& username,
                                          const ser4cpp::rseq_t& outstationChallengeData,
                                          size_t keySize,
                                          UpdateKey& updateKey,
                                          std::error_code& ec)
{
    // an AES-128 or AES-256 key: the size was taken to be 32, so AES-128 keys never verified
    if (keySize != AuthSizes::MIN_UPDATE_KEY_SIZE_BYTES && keySize != AuthSizes::MAX_UPDATE_KEY_SIZE_BYTES)
    {
        ec = make_error_code(OutstationError::BAD_UNWRAPPPED_UPDATE_KEY_DATA_SIZE);
        return false;
    }

    // Buffer for the decrypted data
    std::array<uint8_t, AuthSizes::MAX_UPDATE_KEY_UNWRAP_BUFFER_SIZE> buffer;
    ser4cpp::wseq_t dest(buffer.data(), buffer.size());

    auto unwrapped = algorithm.UnwrapKey(kek, encryptedData, dest, ec);

    if (ec)
    {
        return false;
    }

    const auto MIN_UNWRAPPED_SIZE = username.size() + keySize + outstationChallengeData.length();

    if (unwrapped.length() < MIN_UNWRAPPED_SIZE)
    {
        ec = make_error_code(OutstationError::BAD_UNWRAPPPED_UPDATE_KEY_DATA_SIZE);
        return false;
    }

    // The name should be the same as what was sent previously
    auto name = ToString(unwrapped.take(username.size()));

    // This comparison does not need to be secure since it is not a secret
    if (name != username)
    {
        ec = make_error_code(OutstationError::DECRYPTED_USERNAME_MISMATCH);
        return false;
    }

    unwrapped.advance(username.size()); // now points to beginning of the key

    // Next locate the challenge data, which is whatever is after the key
    // Remove any padding by truncating to the size of the expected challenge
    auto challengeData = unwrapped.skip(keySize).take(outstationChallengeData.length());

    if (!SecureEquals(challengeData, outstationChallengeData))
    {
        ec = make_error_code(OutstationError::CHALLENGE_DATA_MISMATCH);
        return false;
    }

    // We're fully authenticated so this data is the key
    const auto keyData = unwrapped.take(keySize);
    updateKey = UpdateKey(Buffer(keyData, keyData.length()));

    return true;
}

} // namespace opendnp3
