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

// Outstation session fuzzer: a sequence of requests, each [flags][len][len bytes], to an outstation with a file
// handler, unsolicited responses, assign class and points of every type. Every response the outstation sends must
// parse: a malformed response aborts.
#include "utils/BufferHelpers.h"
#include "utils/OutstationTestObject.h"

#include <app/parsing/APDUHeaderParser.h>
#include <app/parsing/APDUParser.h>

#include <dnp3mocks/DatabaseHelpers.h>

#include <ser4cpp/util/HexConversions.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace opendnp3;

namespace
{

void CheckResponse(const std::string& hex)
{
    if (hex.empty())
        return;
    HexSequence bytes(hex);
    const auto header = APDUHeaderParser::ParseResponse(bytes.ToRSeq());
    if (!header.success || APDUParser::ParseAndLogAll(header.objects, nullptr) != ParseResult::OK)
    {
        std::fprintf(stderr, "outstation sent a malformed response: %s\n", hex.c_str());
        std::abort();
    }
}

class FuzzFileHandler final : public IFileHandler
{
public:
    explicit FuzzFileHandler(uint8_t seed) : seed(seed) {}

    FileCommandResult GetFileInfo(const std::string& filename) override
    {
        FileCommandResult r;
        r.status = (seed & 1) ? FileStatus::SUCCESS : FileStatus::NOT_EXIST;
        r.info.fileName = filename + filename;
        r.info.size = static_cast<uint32_t>(filename.size());
        return r;
    }
    FileOpenResult OpenFile(const std::string& filename, uint32_t, FilePermissions, FileMode, uint16_t maxBlockSize,
                            uint16_t) override
    {
        FileOpenResult r;
        r.status = FileStatus::SUCCESS;
        r.fileHandle = 7;
        r.fileSize = 100000;
        r.maxBlockSize = maxBlockSize;
        return r;
    }
    FileBlockResult ReadBlock(uint32_t, uint32_t blockNum) override
    {
        FileBlockResult r;
        r.data.assign(static_cast<size_t>((blockNum * 37 + seed * 101) % 5000), 0xAA);
        r.lastBlock = (blockNum % 3) == 2;
        return r;
    }
    FileStatus WriteBlock(uint32_t, uint32_t, bool, const uint8_t* data, size_t size) override
    {
        uint32_t sum = 0;
        for (size_t i = 0; i < size; ++i)
            sum += data[i]; // touch every byte
        return sum % 2 ? FileStatus::SUCCESS : FileStatus::BLOCK_SEQ;
    }
    FileStatus CloseFile(uint32_t, uint16_t) override
    {
        return FileStatus::SUCCESS;
    }
    FileStatus DeleteFile(const std::string&) override
    {
        return FileStatus::SUCCESS;
    }
    void AbortFile(uint32_t) override {}
    FileAuthResult AuthenticateFile(const std::string& u, const std::string& p) override
    {
        FileAuthResult r;
        r.status = FileStatus::SUCCESS;
        r.authKey = static_cast<uint32_t>(u.size() + p.size());
        return r;
    }

private:
    uint8_t seed;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;

    const uint8_t setup = data[0];
    OutstationConfig config;
    config.params.allowUnsolicited = (setup & 0x01) != 0;
    config.params.unsolClassMask = ClassField::AllClasses();
    config.params.maxTxFragSize = (setup & 0x02) ? 249 : 2048;
    config.params.maxControlsPerRequest = (setup & 0x04) ? 2 : 255;
    config.eventBufferConfig = EventBufferConfig::AllTypes((setup & 0x08) ? 3 : 50);

    auto database = configure::database_by_sizes(5, 5, 5, 5, 5, 5, 5, 3, 3);
    OutstationTestObject t(config, database, std::make_shared<FuzzFileHandler>(setup));
    t.application->supportsAssignClass = true;
    t.application->coldRestartSupport = RestartMode::SUPPORTED_DELAY_FINE;
    t.application->warmRestartSupport = RestartMode::SUPPORTED_DELAY_COARSE;

    t.Transaction([&](IUpdateHandler& db) {
        db.Update(Binary(true), 1);
        db.Update(Analog(42.5), 2);
        db.Update(Counter(7), 3);
        const uint8_t bytes[] = {1, 2, 3};
        db.Update(OctetString(Buffer(bytes, (setup >> 4) % 4)), 0);
    });

    t.LowerLayerUp();

    size_t pos = 1;
    while (pos + 2 <= size)
    {
        const uint8_t flags = data[pos];
        const size_t len = data[pos + 1];
        pos += 2;
        const size_t n = std::min(len, size - pos);
        const ser4cpp::rseq_t apdu(data + pos, n);
        pos += n;

        if (flags & 0x01)
            t.BroadcastToOutstation(LinkBroadcastAddress::DontConfirm, ser4cpp::HexConversions::to_hex(apdu));
        else
            t.context.OnReceive(Message(Addresses(), apdu));
        t.OnTxReady();
        if (flags & 0x02)
            // a bounded step: a timer at the end of time would take the mock clock where its arithmetic overflows
            t.AdvanceTime(TimeDuration::Seconds(1 + (flags >> 5)));
        if (flags & 0x04)
            t.Transaction([&](IUpdateHandler& db) { db.Update(Binary((flags & 0x08) != 0), flags % 5); });
        if (flags & 0x10)
        {
            t.LowerLayerDown();
            t.LowerLayerUp();
        }
        while (!t.lower->HasNoData())
            CheckResponse(t.lower->PopWriteAsHex());
    }
    return 0;
}
