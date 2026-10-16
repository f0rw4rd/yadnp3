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
#include "utils/MasterTestFixture.h"

#include <catch.hpp>

#include <vector>

using namespace opendnp3;

#define SUITE(name) "MasterFileTransferTestSuite - " name

namespace
{

// g70v4: handle 1, file size as given, max block size 2048, request id 0, status SUCCESS
std::string OpenResponse(const std::string& sizeHex)
{
    return "C0 81 00 00 46 04 5B 01 0D 00 01 00 00 00 " + sizeHex + " 00 08 00 00 00";
}

struct ReadFileTest
{
    explicit ReadFileTest(const std::string& sizeHex = "00 00 00 00") : t(NoStartupTasks())
    {
        t.context->OnLowerLayerUp();
        t.context->ReadFile("x", [this](const FileReadResult& r) { results.push_back(r); });
        t.exe->run_many();

        REQUIRE(t.lower->PopWriteAsHex().substr(0, 5) == "C0 19"); // OPEN
        t.context->OnTxReady();
        t.SendToMaster(OpenResponse(sizeHex));
        t.exe->run_many();
    }

    // answers the pending READ of the block with a g70v5 of handle, block number and 4 octets of data
    void Block(const std::string& seq, const std::string& handleHex, const std::string& blockHex)
    {
        REQUIRE(t.lower->PopWriteAsHex().substr(0, 5) == seq + " 01"); // READ
        t.context->OnTxReady();
        t.SendToMaster(seq + " 81 00 00 46 05 5B 01 0C 00 " + handleHex + " " + blockHex + " AA BB CC DD");
        t.exe->run_many();
    }

    void Close(const std::string& seq)
    {
        REQUIRE(t.lower->PopWriteAsHex().substr(0, 5) == seq + " 1A"); // CLOSE
        t.context->OnTxReady();
        t.SendToMaster(seq + " 81 00 00 46 04 5B 01 0D 00 01 00 00 00 00 00 00 00 00 00 00 00 00");
        t.exe->run_many();
    }

    MasterTestFixture t;
    std::vector<FileReadResult> results;
};

} // namespace

TEST_CASE(SUITE("ReadFile puts the blocks together"))
{
    ReadFileTest test;
    test.Block("C1", "01 00 00 00", "00 00 00 00");
    test.Block("C2", "01 00 00 00", "01 00 00 80"); // last
    test.Close("C3");

    REQUIRE(test.results.size() == 1);
    REQUIRE(test.results[0].summary == TaskCompletion::SUCCESS);
    REQUIRE(test.results[0].statusCode == FileStatus::SUCCESS);
    REQUIRE(test.results[0].data == std::vector<uint8_t>{0xAA, 0xBB, 0xCC, 0xDD, 0xAA, 0xBB, 0xCC, 0xDD});
}

TEST_CASE(SUITE("ReadFile fails on a block of another handle"))
{
    ReadFileTest test;
    test.Block("C1", "EF BE AD DE", "00 00 00 00");
    test.Close("C2");

    REQUIRE(test.results.size() == 1);
    REQUIRE(test.results[0].statusCode != FileStatus::SUCCESS);
    REQUIRE(test.results[0].data.empty());
}

TEST_CASE(SUITE("ReadFile fails on a block out of sequence"))
{
    ReadFileTest test;
    test.Block("C1", "01 00 00 00", "00 00 00 00");
    test.Block("C2", "01 00 00 00", "00 00 00 00"); // block 0 again
    test.Close("C3");

    REQUIRE(test.results.size() == 1);
    REQUIRE(test.results[0].statusCode == FileStatus::BLOCK_SEQ);
    REQUIRE(test.results[0].data.empty());
}

TEST_CASE(SUITE("ReadFile fails on more data than the file size the outstation gave"))
{
    ReadFileTest test("06 00 00 00"); // 6 octets
    test.Block("C1", "01 00 00 00", "00 00 00 00");
    test.Block("C2", "01 00 00 00", "01 00 00 00");
    test.Close("C3");

    REQUIRE(test.results.size() == 1);
    REQUIRE(test.results[0].statusCode == FileStatus::BUFFER_OVERFLOW);
    REQUIRE(test.results[0].data.empty());
}

TEST_CASE(SUITE("GetFileInfo asks with a file descriptor (g70v7)"))
{
    MasterTestFixture t(NoStartupTasks());
    t.context->OnLowerLayerUp();
    t.context->GetFileInfo("x", [](const FileInfoResult&) {});
    t.exe->run_many();

    // length 21: name offset 20, name length 1, the other fields zero, then the name
    REQUIRE(t.lower->PopWriteAsHex()
            == "C0 1C 46 07 5B 01 15 00 14 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 78");
}

TEST_CASE(SUITE("AuthenticateFile takes the key from the g70v2 response"))
{
    MasterTestFixture t(NoStartupTasks());
    t.context->OnLowerLayerUp();
    std::vector<FileAuthResult_t> results;
    t.context->AuthenticateFile("u", "p", [&](const FileAuthResult_t& r) { results.push_back(r); });
    t.exe->run_many();

    REQUIRE(t.lower->PopWriteAsHex().substr(0, 11) == "C0 1D 46 02");
    t.context->OnTxReady();
    t.SendToMaster("C0 81 00 00 46 02 5B 01 0C 00 0C 00 00 00 0C 00 00 00 78 56 34 12");
    t.exe->run_many();

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].statusCode == FileStatus::SUCCESS);
    REQUIRE(results[0].authKey == 0x12345678);
}
