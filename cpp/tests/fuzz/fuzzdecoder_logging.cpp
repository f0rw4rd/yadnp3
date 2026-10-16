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

// Decoder fuzzer with every log level on, so the object pretty-printing runs too
#include <opendnp3/decoder/Decoder.h>
#include <opendnp3/logging/ILogHandler.h>
#include <opendnp3/logging/LogLevels.h>

#include <cstring>
#include <memory>

using namespace opendnp3;

namespace
{
class TouchingLogHandler final : public ILogHandler
{
public:
    void log(ModuleId, const char* id, LogLevel, char const* location, char const* message) override
    {
        total += std::strlen(id) + std::strlen(location) + std::strlen(message);
    }
    size_t total = 0;
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;
    auto handler = std::make_shared<TouchingLogHandler>();
    Logger logger(handler, ModuleId(), "decoder", LogLevels::everything());
    IDecoderCallbacks callback;
    Decoder decoder(callback, logger);
    const Buffer buffer(data + 1, size - 1);
    switch (data[0] % 3)
    {
    case 0:
        decoder.DecodeLPDU(buffer);
        break;
    case 1:
        decoder.DecodeTPDU(buffer);
        break;
    default:
        decoder.DecodeAPDU(buffer);
        break;
    }
    return 0;
}
