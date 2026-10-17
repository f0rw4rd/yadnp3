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

// Unsolicited fuzzer: binary input changes reach the master in unsolicited responses over a loopback, while the input
// loses some of the master's confirms and lets time pass, so the outstation retries. The outstation retries without
// regenerating, so the master must get every event exactly once, in order for each point, unless the outstation
// reported an overflow.
#include "utils/OutstationTestObject.h"

#include <dnp3mocks/MockLowerLayer.h>
#include <dnp3mocks/MockMasterApplication.h>

#include <master/MasterContext.h>
#include <master/MasterSchedulerBackend.h>

#include <exe4cpp/MockExecutor.h>

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
    bool events = false;

    void BeginFragment(const ResponseInfo& info) override
    {
        events = info.unsolicited;
    }
    void EndFragment(const ResponseInfo&) override {}
    void Process(const HeaderInfo& header, const ICollection<Indexed<Binary>>& values) override
    {
        // only events: the static values of an integrity poll aren't changes
        if (!events && !header.isEventVariation)
            return;
        values.ForeachItem([this](const Indexed<Binary>& v) {
            received.push_back(BinaryChange{v.index, v.value.value, v.value.time.value});
        });
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
    std::fprintf(stderr, "unsolicited round trip: %s\n", detail.c_str());
    std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;
    const bool trace = std::getenv("YADNP3_FUZZ_TRACE") != nullptr;

    DatabaseConfig database;
    for (uint16_t i = 0; i < NUM; ++i)
    {
        database.binary_input[i].clazz = PointClass::Class1;
        database.binary_input[i].evariation = EventBinaryVariation::Group2Var2;
    }
    OutstationConfig config;
    config.eventBufferConfig = EventBufferConfig::AllTypes(1 + data[0] % 6);
    config.params.allowUnsolicited = true;
    config.params.unsolClassMask = ClassField::AllEventClasses();
    config.params.unsolConfirmTimeout = TimeDuration::Seconds(1);
    config.params.maxTxFragSize = 249;
    OutstationTestObject outstation(config, database);

    MasterParams params;
    params.startupIntegrityClassMask = ClassField::None();
    params.disableUnsolOnStartup = false;
    params.unsolClassMask = ClassField::AllEventClasses();
    auto exe = std::make_shared<exe4cpp::MockExecutor>();
    auto lower = std::make_shared<MockLowerLayer>();
    auto soe = std::make_shared<OrderedSOE>();
    auto scheduler = std::make_shared<MasterSchedulerBackend>(exe);
    auto master = MContext::Create(Addresses(1, 1024), Logger(nullptr, ModuleId(), "master", LogLevels()), exe, lower,
                                   soe, std::make_shared<MockMasterApplication>(), scheduler, params);
    lower->SetUpperLayer(*master);

    bool overflowSeen = false;
    auto pump = [&](bool loseUnsolicitedConfirms) {
        for (int round = 0; round < 100; ++round)
        {
            bool moved = false;
            while (lower->NumWrites() > 0)
            {
                const auto request = lower->PopWriteAsHex();
                master->OnTxReady();
                exe->run_many();
                moved = true;
                // an unsolicited confirm has the UNS bit (0x10) set and function code 0
                const auto control = std::stoul(request.substr(0, 2), nullptr, 16);
                const bool unsolConfirm = (control & 0x10) && request.substr(3, 2) == "00";
                const bool lose = unsolConfirm && loseUnsolicitedConfirms;
                if (trace)
                    std::fprintf(stderr, "master -> %s%s\n", request.c_str(), lose ? " (lost)" : "");
                if (!lose)
                    outstation.SendToOutstation(request);
            }
            while (!outstation.lower->HasNoData())
            {
                const auto response = outstation.lower->PopWriteAsHex();
                if (trace)
                    std::fprintf(stderr, "outstation -> %s\n", response.c_str());
                if (response.size() >= 11 && (std::stoul(response.substr(9, 2), nullptr, 16) & 0x08))
                    overflowSeen = true;
                ser4cpp::rseq_t empty;
                std::vector<uint8_t> bytes;
                for (size_t i = 0; i + 1 < response.size(); i += 3)
                    bytes.push_back(static_cast<uint8_t>(std::stoul(response.substr(i, 2), nullptr, 16)));
                master->OnReceive(Message(Addresses(1024, 1), ser4cpp::rseq_t(bytes.data(), bytes.size())));
                exe->run_many();
                outstation.OnTxReady();
                moved = true;
            }
            if (!moved)
                return;
        }
    };

    outstation.LowerLayerUp();
    master->OnLowerLayerUp();
    exe->run_many();
    pump(false);

    std::vector<BinaryChange> generated;
    std::map<uint16_t, bool> state;
    uint64_t now = 1000;
    for (size_t pos = 1; pos < size && pos < 200; ++pos)
    {
        const uint8_t op = data[pos];
        switch (op & 0x03)
        {
        case 0:
        case 1: {
            const auto index = static_cast<uint16_t>((op >> 2) % NUM);
            state[index] = !state[index];
            now += 1 + (op >> 4);
            const BinaryChange e{index, state[index], now};
            generated.push_back(e);
            outstation.Transaction([&](IUpdateHandler& db) {
                db.Update(Binary(e.value, Flags(0x01), DNPTime(e.time)), e.index, EventMode::Detect);
            });
            // the unsolicited response waits in the outstation's lower layer for the next exchange
            break;
        }
        case 2:
            pump((op & 0x04) != 0);
            break;
        default:
            // the outstation's unsolicited confirm timeout passes, and it retries
            outstation.AdvanceTime(TimeDuration::Milliseconds(1500));
            pump((op & 0x04) != 0);
            break;
        }
    }

    // time passes with every confirm delivered, until the outstation has sent everything
    for (int i = 0; i < 20; ++i)
    {
        outstation.AdvanceTime(TimeDuration::Milliseconds(1500));
        pump(false);
    }
    master->OnLowerLayerDown();
    scheduler->Shutdown();

    std::map<uint16_t, uint64_t> lastTime;
    for (const auto& r : soe->received)
    {
        bool found = false;
        for (const auto& g : generated)
            found |= (g == r);
        if (!found)
            Fail("received an event that was never generated, point " + std::to_string(r.index));
        if (lastTime.count(r.index) && r.time <= lastTime[r.index])
            Fail("point " + std::to_string(r.index) + "'s events out of order, or one repeated");
        lastTime[r.index] = r.time;
    }
    if (!overflowSeen)
    {
        for (const auto& g : generated)
        {
            bool got = false;
            for (const auto& r : soe->received)
                got |= (g == r);
            if (!got)
                Fail("event of point " + std::to_string(g.index) + " at " + std::to_string(g.time)
                     + " lost without an overflow");
        }
    }
    return 0;
}
