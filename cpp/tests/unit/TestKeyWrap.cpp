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
#include "secauth/master/KeyWrap.h"

#include <catch.hpp>

#include <vector>

using namespace opendnp3;

#define SUITE(name) "KeyWrapTestSuite - " name

namespace
{

// passes the data through, so a test sees what would be wrapped
struct PassThroughAlgo final : IKeyWrapAlgo
{
    ser4cpp::rseq_t WrapKey(const ser4cpp::rseq_t&,
                            const ser4cpp::rseq_t& input,
                            ser4cpp::wseq_t&,
                            std::error_code&) const override
    {
        return input;
    }

    ser4cpp::rseq_t UnwrapKey(const ser4cpp::rseq_t&,
                              const ser4cpp::rseq_t& input,
                              ser4cpp::wseq_t&,
                              std::error_code&) const override
    {
        return input;
    }
};

} // namespace

TEST_CASE(SUITE("Wrap refuses key status data that doesn't fit"))
{
    PassThroughAlgo algo;
    KeyWrapBuffer buffer;
    const std::vector<uint8_t> key(32, 0xAA);
    const std::vector<uint8_t> updateKey(16, 0x01);
    // the key status comes from the outstation's challenge data, which can be up to 64K
    const std::vector<uint8_t> keyStatus(400, 0x41);
    const SessionKeysView keys(ser4cpp::rseq_t(key.data(), key.size()), ser4cpp::rseq_t(key.data(), key.size()));
    const Logger logger(nullptr, ModuleId(), "test", LogLevels::everything());

    REQUIRE_FALSE(buffer.Wrap(algo, ser4cpp::rseq_t(updateKey.data(), updateKey.size()), keys,
                              ser4cpp::rseq_t(keyStatus.data(), keyStatus.size()), logger));
}
