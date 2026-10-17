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
#ifndef OPENDNP3_ENCRYPTEDUPDATEKEY_H
#define OPENDNP3_ENCRYPTEDUPDATEKEY_H

#include "opendnp3/crypto/IKeyWrapAlgo.h"
#include "opendnp3/secauth/UpdateKey.h"

#include <ser4cpp/container/SequenceTypes.h>

#include <string>
#include <system_error>

namespace opendnp3
{

/**
 * Decrypts and verifies an encrypted update key during a key change operation.
 * The encrypted blob contains: username + update_key + challenge_data (+ padding).
 */
struct EncryptedUpdateKey
{
    /**
     * Decrypt the wrapped update key data and verify its contents.
     *
     * @param algorithm  The AES key wrap algorithm to use for decryption
     * @param kek        The key-encrypting-key (authority symmetric key)
     * @param encryptedData The encrypted blob from the authority
     * @param username   The expected username embedded in the blob
     * @param outstationChallengeData The challenge data sent by the outstation
     * @param keySize    The size of the update key: 16 for AES-128, 32 for AES-256
     * @param updateKey  [out] The decrypted update key on success
     * @param ec         [out] Error code on failure
     * @return true if decryption and verification succeeded
     */
    static bool DecryptAndVerify(IKeyWrapAlgo& algorithm,
                                 const ser4cpp::rseq_t& kek,
                                 const ser4cpp::rseq_t& encryptedData,
                                 const std::string& username,
                                 const ser4cpp::rseq_t& outstationChallengeData,
                                 size_t keySize,
                                 UpdateKey& updateKey,
                                 std::error_code& ec);
};

} // namespace opendnp3

#endif
