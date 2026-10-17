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
#include <crypto/AESKeyWrap.h>
#include <secauth/outstation/EncryptedUpdateKey.h>

#include <catch.hpp>

#include <string>
#include <vector>

using namespace opendnp3;

#define SUITE(name) "EncryptedUpdateKeyTestSuite - " name

namespace
{

// what the authority sends: the user name, the update key and the outstation's challenge, padded and wrapped
std::vector<uint8_t> Wrap(const std::vector<uint8_t>& kek,
                          const std::string& user,
                          const std::vector<uint8_t>& key,
                          const std::vector<uint8_t>& challenge)
{
    std::vector<uint8_t> plain(user.begin(), user.end());
    plain.insert(plain.end(), key.begin(), key.end());
    plain.insert(plain.end(), challenge.begin(), challenge.end());
    while (plain.size() % 8)
        plain.push_back(0);
    std::vector<uint8_t> out(plain.size() + 8);
    ser4cpp::wseq_t dest(out.data(), out.size());
    std::error_code ec;
    AESKeyWrap aes;
    const auto wrapped = aes.WrapKey(ser4cpp::rseq_t(kek.data(), kek.size()), ser4cpp::rseq_t(plain.data(), plain.size()),
                                     dest, ec);
    REQUIRE(!ec);
    return std::vector<uint8_t>(static_cast<const uint8_t*>(wrapped), static_cast<const uint8_t*>(wrapped) + wrapped.length());
}

bool Decrypt(const std::vector<uint8_t>& kek,
             const std::vector<uint8_t>& wrapped,
             const std::vector<uint8_t>& challenge,
             size_t keySize,
             UpdateKey& key)
{
    AESKeyWrap aes;
    std::error_code ec;
    return EncryptedUpdateKey::DecryptAndVerify(aes, ser4cpp::rseq_t(kek.data(), kek.size()),
                                                ser4cpp::rseq_t(wrapped.data(), wrapped.size()), "Common",
                                                ser4cpp::rseq_t(challenge.data(), challenge.size()), keySize, key, ec);
}

} // namespace

TEST_CASE(SUITE("An AES-128 update key decrypts"))
{
    const std::vector<uint8_t> kek(16, 0x11), key(16, 0x22), challenge(4, 0x33);
    UpdateKey result;
    REQUIRE(Decrypt(kek, Wrap(kek, "Common", key, challenge), challenge, 16, result));
    REQUIRE(result.IsValid());
    REQUIRE(result.GetView().algorithm == KeyWrapAlgorithm::AES_128);
}

TEST_CASE(SUITE("An AES-256 update key decrypts"))
{
    const std::vector<uint8_t> kek(32, 0x11), key(32, 0x22), challenge(4, 0x33);
    UpdateKey result;
    REQUIRE(Decrypt(kek, Wrap(kek, "Common", key, challenge), challenge, 32, result));
    REQUIRE(result.GetView().algorithm == KeyWrapAlgorithm::AES_256);
}

TEST_CASE(SUITE("An update key of the wrong size, or with the wrong challenge, is refused"))
{
    const std::vector<uint8_t> kek(16, 0x11), key(16, 0x22), challenge(4, 0x33), other(4, 0x44);
    UpdateKey result;
    const auto wrapped = Wrap(kek, "Common", key, challenge);
    REQUIRE_FALSE(Decrypt(kek, wrapped, challenge, 24, result));
    REQUIRE_FALSE(Decrypt(kek, wrapped, other, 16, result));
}
