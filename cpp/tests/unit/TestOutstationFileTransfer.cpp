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
#include "utils/OutstationTestObject.h"

#include <catch.hpp>

#include <string>
#include <vector>

using namespace opendnp3;

#define SUITE(name) "OutstationFileTransferTestSuite - " name

namespace
{

class RecordingFileHandler final : public IFileHandler
{
public:
    std::vector<std::string> opened;
    std::vector<std::pair<std::string, std::string>> authenticated;
    std::vector<std::string> described;
    size_t blockSize = 4;

    FileCommandResult GetFileInfo(const std::string& filename) override
    {
        described.push_back(filename);
        return FileCommandResult();
    }

    FileOpenResult OpenFile(const std::string& filename,
                            uint32_t authKey,
                            FilePermissions permissions,
                            FileMode mode,
                            uint16_t maxBlockSize,
                            uint16_t requestId) override
    {
        opened.push_back(filename);
        FileOpenResult result;
        result.status = FileStatus::SUCCESS;
        result.fileHandle = 7;
        return result;
    }

    FileBlockResult ReadBlock(uint32_t fileHandle, uint32_t blockNum) override
    {
        FileBlockResult result;
        result.data = std::vector<uint8_t>(blockSize, 0xAA);
        result.lastBlock = true;
        return result;
    }

    FileStatus WriteBlock(uint32_t, uint32_t, bool, const uint8_t*, size_t) override
    {
        return FileStatus::SUCCESS;
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

    FileAuthResult AuthenticateFile(const std::string& username, const std::string& password) override
    {
        authenticated.emplace_back(username, password);
        FileAuthResult result;
        result.status = FileStatus::SUCCESS;
        result.authKey = 0x12345678;
        return result;
    }
};

// g70v3 fixed fields after the name offset and length: time of creation, permissions, auth key, file size, mode,
// max block size and request id
const std::string openFixed = "00 00 00 00 00 00 FF 01 00 00 00 00 00 00 00 00 01 00 00 08 00 00";

} // namespace

TEST_CASE(SUITE("OPEN takes the file name from inside the object"))
{
    auto files = std::make_shared<RecordingFileHandler>();
    OutstationTestObject t(OutstationConfig(), DatabaseConfig(), files);
    t.LowerLayerUp();

    // length 29: name at offset 26, 3 octets
    t.SendToOutstation("C0 19 46 03 5B 01 1D 00 1A 00 03 00 " + openFixed + " 61 62 63");
    REQUIRE(t.lower->PopWriteAsHex().substr(0, 23) == "C0 81 80 00 46 04 5B 01");
    REQUIRE(files->opened == std::vector<std::string>{"abc"});
}

TEST_CASE(SUITE("OPEN rejects a file name outside the object"))
{
    auto files = std::make_shared<RecordingFileHandler>();
    OutstationTestObject t(OutstationConfig(), DatabaseConfig(), files);
    t.LowerLayerUp();

    // length 26, so no room for the 5-octet name that follows the object
    t.SendToOutstation("C0 19 46 03 5B 01 1A 00 1A 00 05 00 " + openFixed + " 48 45 4C 4C 4F");
    REQUIRE(t.lower->PopWriteAsHex() == "C0 81 80 04");
    REQUIRE(files->opened.empty());
}

TEST_CASE(SUITE("AUTHENTICATE takes the user name and password at their offsets"))
{
    auto files = std::make_shared<RecordingFileHandler>();
    OutstationTestObject t(OutstationConfig(), DatabaseConfig(), files);
    t.LowerLayerUp();

    // length 16: user name "u1" at offset 14, password "p1" at offset 12
    t.SendToOutstation("C0 1D 46 02 5B 01 10 00 0E 00 02 00 0C 00 02 00 00 00 00 00 70 31 75 31");
    t.lower->PopWriteAsHex();
    REQUIRE(files->authenticated.size() == 1);
    REQUIRE(files->authenticated[0].first == "u1");
    REQUIRE(files->authenticated[0].second == "p1");
}

TEST_CASE(SUITE("READ of a block that doesn't fit answers with a status"))
{
    auto files = std::make_shared<RecordingFileHandler>();
    files->blockSize = 4000;
    OutstationTestObject t(OutstationConfig(), DatabaseConfig(), files);
    t.LowerLayerUp();

    t.SendToOutstation("C0 01 46 05 5B 01 08 00 07 00 00 00 00 00 00 00");
    // g70v6, handle 7, block 0, BUFFER_OVERFLOW
    REQUIRE(t.lower->PopWriteAsHex() == "C0 81 80 00 46 06 5B 01 09 00 07 00 00 00 00 00 00 00 11");
}

TEST_CASE(SUITE("READ of a block answers with its data"))
{
    auto files = std::make_shared<RecordingFileHandler>();
    OutstationTestObject t(OutstationConfig(), DatabaseConfig(), files);
    t.LowerLayerUp();

    t.SendToOutstation("C0 01 46 05 5B 01 08 00 07 00 00 00 00 00 00 00");
    REQUIRE(t.lower->PopWriteAsHex() == "C0 81 80 00 46 05 5B 01 0C 00 07 00 00 00 00 00 00 80 AA AA AA AA");
}

TEST_CASE(SUITE("AUTHENTICATE answers with the key in a g70v2"))
{
    auto files = std::make_shared<RecordingFileHandler>();
    OutstationTestObject t(OutstationConfig(), DatabaseConfig(), files);
    t.LowerLayerUp();

    t.SendToOutstation("C0 1D 46 02 5B 01 10 00 0C 00 02 00 0E 00 02 00 00 00 00 00 75 31 70 31");
    REQUIRE(t.lower->PopWriteAsHex() == "C0 81 80 00 46 02 5B 01 0C 00 0C 00 00 00 0C 00 00 00 78 56 34 12");
}

TEST_CASE(SUITE("GET_FILE_INFO takes the file name from a g70v7"))
{
    auto files = std::make_shared<RecordingFileHandler>();
    OutstationTestObject t(OutstationConfig(), DatabaseConfig(), files);
    t.LowerLayerUp();

    t.SendToOutstation("C0 1C 46 07 5B 01 15 00 14 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 78");
    REQUIRE(t.lower->PopWriteAsHex().substr(0, 23) == "C0 81 80 00 46 04 5B 01"); // NOT_EXIST
    REQUIRE(files->described == std::vector<std::string>{"x"});
}
