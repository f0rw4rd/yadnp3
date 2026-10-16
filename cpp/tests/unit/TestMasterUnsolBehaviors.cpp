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
#include "utils/APDUHexBuilders.h"
#include "utils/CallbackQueue.h"
#include "utils/MasterTestFixture.h"
#include "utils/MeasurementComparisons.h"

#include <app/APDUBuilders.h>
#include <app/APDUResponse.h>
#include <catch.hpp>

using namespace opendnp3;

#define SUITE(name) "MasterUnsolTestSuite - " name

TEST_CASE(SUITE("ReceiveUnsolBeforeTransmit"))
{
    MasterParams params;
    params.disableUnsolOnStartup = false;
    MasterTestFixture t(params);

    t.context->OnLowerLayerUp();

    t.SendToMaster(hex::NullUnsolicited(0, IINField::Empty()));

    REQUIRE(t.exe->run_many());

    REQUIRE(t.lower->PopWriteAsHex() == hex::UnsolConfirm(0));
    t.context->OnTxReady();

    REQUIRE(t.lower->PopWriteAsHex() == hex::IntegrityPoll(0));
}

TEST_CASE(SUITE("A repeated unsolicited fragment is confirmed again but not processed again"))
{
    MasterTestFixture t(NoStartupTasks());
    t.context->OnLowerLayerUp();
    t.exe->run_many();

    const std::string unsol = "F2 82 00 00 01 02 00 02 02 81"; // seq 2, g1v2 index 2
    t.SendToMaster(unsol);
    REQUIRE(t.lower->PopWriteAsHex() == hex::UnsolConfirm(2));
    t.context->OnTxReady();
    REQUIRE(t.meas->TotalReceived() == 1);

    // the confirm was lost, and the outstation sends the fragment again
    t.SendToMaster(unsol);
    REQUIRE(t.lower->PopWriteAsHex() == hex::UnsolConfirm(2));
    t.context->OnTxReady();
    REQUIRE(t.meas->TotalReceived() == 1);

    // a new fragment with the next sequence number is processed
    t.SendToMaster("F3 82 00 00 01 02 00 02 02 01");
    REQUIRE(t.lower->PopWriteAsHex() == hex::UnsolConfirm(3));
    t.context->OnTxReady();
    REQUIRE(t.meas->TotalReceived() == 2);
}

TEST_CASE(SUITE("An unsolicited fragment with the same sequence number but other content is processed"))
{
    MasterTestFixture t(NoStartupTasks());
    t.context->OnLowerLayerUp();
    t.exe->run_many();

    t.SendToMaster("F2 82 00 00 01 02 00 02 02 81");
    t.lower->PopWriteAsHex();
    t.context->OnTxReady();

    t.SendToMaster("F2 82 00 00 01 02 00 02 02 01");
    REQUIRE(t.lower->PopWriteAsHex() == hex::UnsolConfirm(2));
    t.context->OnTxReady();
    REQUIRE(t.meas->TotalReceived() == 2);
}
