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

// BinaryChange buffer fuzzer: the input interleaves binary input changes with class 1 polls by a master over a loopback,
// some of whose confirms are lost, against a small event buffer. Every event the master gets must be one that was
// generated, each point's events must arrive in order, and unless the outstation reported an overflow, the master must
// get every event; unless a confirm was lost, it must get each only once.
#include "utils/MasterTestFixture.h"
#include "utils/OutstationTestObject.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace opendnp3;

namespace
{

constexpr uint16_t NUM = 3;

struct BinaryChange
{
    uint16_t index;
    bool value;
    uint64_t time;

    bool operator==(const BinaryChange& other) const
    {
        return index == other.index && value == other.value && time == other.time;
    }
};

class OrderedSOE final : public ISOEHandler
{
public:
    std::vector<BinaryChange> received;

    void BeginFragment(const ResponseInfo&) override {}
    void EndFragment(const ResponseInfo&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<Binary>>& values) override
    {
        values.ForeachItem(
            [this](const Indexed<Binary>& v) { received.push_back(BinaryChange{v.index, v.value.value, v.value.time.value}); });
    }
    void Process(const HeaderInfo&, const ICollection<Indexed<DoubleBitBinary>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<Analog>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<Counter>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<FrozenCounter>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<BinaryOutputStatus>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<AnalogOutputStatus>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<OctetString>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<TimeAndInterval>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<BinaryCommandEvent>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<AnalogCommandEvent>>&) override {}
    void Process(const HeaderInfo&, const ICollection<Indexed<AnalogInputDeadband>>&) override {}
    void Process(const HeaderInfo&, const ICollection<DNPTime>&) override {}
};

[[noreturn]] void Fail(const std::string& detail)
{
    std::fprintf(stderr, "event buffer round trip: %s\n", detail.c_str());
    std::abort();
}

struct Session
{
    OutstationTestObject& outstation;
    MasterTestFixture& master;
    bool overflowSeen = false;
    bool confirmLost = false;
    bool trace = std::getenv("YADNP3_FUZZ_TRACE") != nullptr;

    // moves messages both ways until neither side sends; drops the master's confirms if asked
    void Pump(bool dropConfirms)
    {
        for (int round = 0; round < 100; ++round)
        {
            bool moved = false;
            while (!master.lower->HasNoData())
            {
                const auto request = master.lower->PopWriteAsHex();
                master.context->OnTxReady();
                master.exe->run_many();
                moved = true;
                const bool isConfirm = request.size() >= 5 && request.substr(3, 2) == "00";
                if (trace)
                    std::fprintf(stderr, "master -> %s%s\n", request.c_str(),
                                 (isConfirm && dropConfirms) ? " (lost)" : "");
                if (isConfirm && dropConfirms)
                {
                    confirmLost = true;
                    continue;
                }
                outstation.SendToOutstation(request);
            }
            while (!outstation.lower->HasNoData())
            {
                const auto response = outstation.lower->PopWriteAsHex();
                if (trace)
                    std::fprintf(stderr, "outstation -> %s\n", response.c_str());
                // IIN2.3, event buffer overflow, is bit 0x08 of the second IIN octet
                if (response.size() >= 11)
                {
                    const auto iin2 = std::stoul(response.substr(9, 2), nullptr, 16);
                    if (iin2 & 0x08)
                        overflowSeen = true;
                }
                master.SendToMaster(response);
                outstation.OnTxReady();
                moved = true;
            }
            if (!moved)
                return;
        }
    }
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;

    DatabaseConfig database;
    for (uint16_t i = 0; i < NUM; ++i)
    {
        database.binary_input[i].clazz = PointClass::Class1;
        database.binary_input[i].evariation = EventBinaryVariation::Group2Var2;
    }
    OutstationConfig config;
    config.eventBufferConfig = EventBufferConfig::AllTypes(1 + data[0] % 6);
    config.params.maxTxFragSize = 249;
    config.params.solConfirmTimeout = TimeDuration::Seconds(1);
    OutstationTestObject outstation(config, database);
    outstation.LowerLayerUp();

    MasterTestFixture master(NoStartupTasks());
    master.context->OnLowerLayerUp();
    auto soe = std::make_shared<OrderedSOE>();
    Session session{outstation, master};

    std::vector<BinaryChange> generated;
    std::map<uint16_t, bool> state;
    uint64_t now = 1000;

    for (size_t pos = 1; pos < size && pos < 200; ++pos)
    {
        const uint8_t op = data[pos];
        if ((op & 0x03) != 0)
        {
            // a change of a binary input
            const auto index = static_cast<uint16_t>((op >> 2) % NUM);
            state[index] = !state[index];
            now += 1 + (op >> 4);
            const BinaryChange e{index, state[index], now};
            generated.push_back(e);
            outstation.Transaction([&](IUpdateHandler& db) {
                db.Update(Binary(e.value, Flags(0x01), DNPTime(e.time)), e.index, EventMode::Detect);
            });
        }
        else
        {
            // a class 1 poll, whose confirms may be lost; the outstation's confirm timeout passes afterwards
            master.context->ScanClasses(ClassField(ClassField::CLASS_1), soe, TaskConfig::Default());
            master.exe->run_many();
            session.Pump((op & 0x04) != 0);
            outstation.AdvanceTime(TimeDuration::Seconds(2));
        }
    }

    // polls with every confirm delivered, until no events are left
    for (int i = 0; i < 10; ++i)
    {
        master.context->ScanClasses(ClassField(ClassField::CLASS_1), soe, TaskConfig::Default());
        master.exe->run_many();
        session.Pump(false);
    }
    master.context->OnLowerLayerDown();

    // every event received was generated, and each point's events come in the order they were generated
    std::map<uint16_t, uint64_t> lastTime;
    for (const auto& r : soe->received)
    {
        bool found = false;
        for (const auto& g : generated)
            found |= (g == r);
        if (!found)
            Fail("received an event that was never generated, point " + std::to_string(r.index));
        if (lastTime.count(r.index) && r.time < lastTime[r.index] && !session.confirmLost)
            Fail("point " + std::to_string(r.index) + "'s events out of order");
        lastTime[r.index] = r.time;
    }

    if (!session.overflowSeen)
    {
        for (const auto& g : generated)
        {
            size_t times = 0;
            for (const auto& r : soe->received)
                times += (g == r) ? 1 : 0;
            if (times == 0)
                Fail("event of point " + std::to_string(g.index) + " at " + std::to_string(g.time)
                     + " lost without an overflow");
            if (times > 1 && !session.confirmLost)
                Fail("event of point " + std::to_string(g.index) + " received twice without a lost confirm");
        }
    }
    return 0;
}
