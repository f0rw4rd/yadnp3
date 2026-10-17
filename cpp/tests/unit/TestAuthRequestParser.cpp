/*
 * Copyright 2024-2026 f0rw4rd (experimental fork)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include "utils/BufferHelpers.h"

#include "dnp3mocks/MockAPDUHeaderHandler.h"

#include <app/parsing/APDUParser.h>
#include <catch.hpp>

using namespace opendnp3;

#define SUITE(name) "AuthRequestParser - " name

TEST_CASE(SUITE("rejects insufficient data for header"))
{
    HexSequence buffer("78 01");
    MockApduHeaderHandler handler;
    auto result = APDUParser::Parse(buffer.ToRSeq(), handler, nullptr);
    REQUIRE(result == ParseResult::NOT_ENOUGH_DATA_FOR_HEADER);
}

TEST_CASE(SUITE("rejects unknown qualifier"))
{
    HexSequence buffer("78 01 FF FF");
    MockApduHeaderHandler handler;
    auto result = APDUParser::Parse(buffer.ToRSeq(), handler, nullptr);
    REQUIRE(result == ParseResult::UNKNOWN_QUALIFIER);
}

TEST_CASE(SUITE("rejects insufficient free-format data"))
{
    HexSequence buffer("78 01 5B 01 08 00 FF FF FF FF FF FF FF");
    MockApduHeaderHandler handler;
    auto result = APDUParser::Parse(buffer.ToRSeq(), handler, nullptr);
    REQUIRE(result == ParseResult::NOT_ENOUGH_DATA_FOR_OBJECTS);
}

TEST_CASE(SUITE("rejects trailing data after free-format object"))
{
    HexSequence buffer("78 01 5B 01 08 00 FF FF FF FF FF FF FF FF FF");
    MockApduHeaderHandler handler;
    auto result = APDUParser::Parse(buffer.ToRSeq(), handler, nullptr);
    REQUIRE(result == ParseResult::NOT_ENOUGH_DATA_FOR_HEADER);
}

TEST_CASE(SUITE("accepts matching free-format g120v1 data"))
{
    HexSequence buffer("78 01 5B 01 08 00 11 22 33 44 FF FF FF FF");
    MockApduHeaderHandler handler;
    auto result = APDUParser::Parse(buffer.ToRSeq(), handler, nullptr);
    REQUIRE(result == ParseResult::OK);
    REQUIRE(handler.records.size() == 1);
}

// g120v4 (Session Key Status Request) uses UINT8_CNT qualifier (0x07)
TEST_CASE(SUITE("g120v4 key status request via count qualifier"))
{
    HexSequence buffer("78 04 07 01 00 00");
    MockApduHeaderHandler handler;
    auto result = APDUParser::Parse(buffer.ToRSeq(), handler, nullptr);
    REQUIRE(result == ParseResult::OK);
    REQUIRE(handler.keyStatusRequests == std::vector<uint16_t>{0});
}

TEST_CASE(SUITE("g120v4 key status request with userNum=9 via count qualifier"))
{
    HexSequence buffer("78 04 07 01 09 00");
    MockApduHeaderHandler handler;
    auto result = APDUParser::Parse(buffer.ToRSeq(), handler, nullptr);
    REQUIRE(result == ParseResult::OK);
    REQUIRE(handler.keyStatusRequests == std::vector<uint16_t>{9});
}

// g120v5 (Session Key Status) via free-format qualifier (0x5B) parses successfully
TEST_CASE(SUITE("g120v5 key status via free-format qualifier"))
{
    // group=120, var=5, qualifier=0x5B, count=1, size=11 (0x000B), then 11 bytes of g120v5 data
    // g120v5 min: seqNum(4) + userNum(2) + keyWrapAlgo(1) + keyStatus(1) + hmacAlgo(1) + challengeLen(2) = 11
    HexSequence buffer("78 05 5B 01 0B 00 01 00 00 00 07 00 02 01 04 00 00");
    MockApduHeaderHandler handler;
    auto result = APDUParser::Parse(buffer.ToRSeq(), handler, nullptr);
    REQUIRE(result == ParseResult::OK);
    REQUIRE(handler.records.size() == 1);
}
