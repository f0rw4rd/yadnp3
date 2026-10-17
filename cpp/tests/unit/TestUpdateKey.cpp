/*
 * Copyright 2024-2026 f0rw4rd (experimental fork)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include <opendnp3/secauth/UpdateKey.h>

#include <ser4cpp/container/SequenceTypes.h>

#include <catch.hpp>

using namespace opendnp3;

#define SUITE(name) "UpdateKey - " name

TEST_CASE(SUITE("default constructed is not valid"))
{
    UpdateKey key;
    REQUIRE_FALSE(key.IsValid());
}

TEST_CASE(SUITE("default constructed GetView has UNDEFINED algorithm"))
{
    UpdateKey key;
    auto view = key.GetView();
    REQUIRE(view.algorithm == KeyWrapAlgorithm::UNDEFINED);
    REQUIRE(view.data.length == 0);
}

TEST_CASE(SUITE("Initialize with 16 bytes gives valid AES-128"))
{
    UpdateKey key;
    uint8_t data[16];
    for (int i = 0; i < 16; ++i)
        data[i] = static_cast<uint8_t>(i);
    Buffer seq(data, 16);
    REQUIRE(key.Initialize(seq));
    REQUIRE(key.IsValid());
    auto view = key.GetView();
    REQUIRE(view.algorithm == KeyWrapAlgorithm::AES_128);
    REQUIRE(view.data.length == 16);
}

TEST_CASE(SUITE("Initialize with 32 bytes gives valid AES-256"))
{
    UpdateKey key;
    uint8_t data[32];
    for (int i = 0; i < 32; ++i)
        data[i] = static_cast<uint8_t>(i);
    Buffer seq(data, 32);
    REQUIRE(key.Initialize(seq));
    REQUIRE(key.IsValid());
    auto view = key.GetView();
    REQUIRE(view.algorithm == KeyWrapAlgorithm::AES_256);
    REQUIRE(view.data.length == 32);
}

TEST_CASE(SUITE("Initialize with invalid size fails"))
{
    UpdateKey key;
    uint8_t data[8];
    Buffer seq(data, 8);
    REQUIRE_FALSE(key.Initialize(seq));
    REQUIRE_FALSE(key.IsValid());
}

TEST_CASE(SUITE("GetView returns correct data"))
{
    uint8_t data[16];
    for (int i = 0; i < 16; ++i)
        data[i] = static_cast<uint8_t>(0xAA + i);
    Buffer seq(data, 16);
    UpdateKey key(seq);
    REQUIRE(key.IsValid());
    auto view = key.GetView();
    REQUIRE(view.data.length == 16);
    // verify data matches
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(view.data.data[i] == static_cast<uint8_t>(0xAA + i));
    }
}

TEST_CASE(SUITE("test constructor with repeat byte AES-128"))
{
    UpdateKey key(0xBB, KeyWrapAlgorithm::AES_128);
    REQUIRE(key.IsValid());
    auto view = key.GetView();
    REQUIRE(view.algorithm == KeyWrapAlgorithm::AES_128);
    REQUIRE(view.data.length == 16);
    REQUIRE(view.data.data[0] == 0xBB);
    REQUIRE(view.data.data[15] == 0xBB);
}

TEST_CASE(SUITE("test constructor with repeat byte AES-256"))
{
    UpdateKey key(0xCC, KeyWrapAlgorithm::AES_256);
    REQUIRE(key.IsValid());
    auto view = key.GetView();
    REQUIRE(view.algorithm == KeyWrapAlgorithm::AES_256);
    REQUIRE(view.data.length == 32);
    REQUIRE(view.data.data[0] == 0xCC);
}

TEST_CASE(SUITE("test constructor with UNDEFINED algorithm is not valid"))
{
    UpdateKey key(0xDD, KeyWrapAlgorithm::UNDEFINED);
    REQUIRE_FALSE(key.IsValid());
}

TEST_CASE(SUITE("Initialize with zero-length data fails"))
{
    UpdateKey key;
    Buffer empty;
    REQUIRE_FALSE(key.Initialize(empty));
    REQUIRE_FALSE(key.IsValid());
}
