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

// Command round trip fuzzer: a master sends commands taken from the input, select-before-operate or direct, to an
// outstation over a loopback; the outstation's command handler must get each one as sent, and the master must report
// every point as a success.
#include "utils/MasterTestFixture.h"
#include "utils/OutstationTestObject.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace opendnp3;

namespace
{

class Reader
{
public:
    Reader(const uint8_t* data, size_t size) : data(data), size(size) {}

    template<class T> T Next()
    {
        T value{};
        uint8_t bytes[sizeof(T)] = {0};
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = pos < size ? data[pos++] : 0;
        std::memcpy(&value, bytes, sizeof(T));
        return value;
    }

private:
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
};

[[noreturn]] void Fail(const std::string& detail)
{
    std::fprintf(stderr, "command round trip: %s\n", detail.c_str());
    std::abort();
}

template<class T> bool SameValue(T a, T b)
{
    return a == b;
}
template<> bool SameValue<float>(float a, float b)
{
    return (std::isnan(a) && std::isnan(b)) || a == b;
}
template<> bool SameValue<double>(double a, double b)
{
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

// YADNP3_FUZZ_TRACE prints the traffic, to debug a failure
void Pump(MasterTestFixture& master, OutstationTestObject& outstation)
{
    const bool trace = std::getenv("YADNP3_FUZZ_TRACE") != nullptr;
    for (int round = 0; round < 50; ++round)
    {
        bool moved = false;
        while (!master.lower->HasNoData())
        {
            const auto request = master.lower->PopWriteAsHex();
            if (trace)
                std::fprintf(stderr, "master -> %s\n", request.c_str());
            outstation.SendToOutstation(request);
            master.context->OnTxReady();
            master.exe->run_many();
            moved = true;
        }
        while (!outstation.lower->HasNoData())
        {
            const auto response = outstation.lower->PopWriteAsHex();
            if (trace)
                std::fprintf(stderr, "outstation -> %s\n", response.c_str());
            master.SendToMaster(response);
            outstation.OnTxReady();
            moved = true;
        }
        if (!moved)
            return;
    }
}

template<class T> void CheckAnalog(const char* type, const std::vector<std::pair<T, uint16_t>>& sent, const T& received,
                                   uint16_t receivedIndex, size_t i)
{
    if (i >= sent.size())
        Fail(std::string(type) + ": more operates than commands");
    if (!SameValue(sent[i].first.value, received.value) || sent[i].second != receivedIndex)
        Fail(std::string(type) + ": command " + std::to_string(i) + " changed");
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Reader in(data, size);
    const uint8_t mode = in.Next<uint8_t>();
    const int kind = mode % 5;
    const bool sbo = mode & 0x08;
    const size_t count = 1 + (in.Next<uint8_t>() % 4);

    const OutstationConfig outstationConfig;
    OutstationTestObject outstation(outstationConfig, DatabaseConfig{});
    outstation.LowerLayerUp();
    MasterTestFixture master(NoStartupTasks());
    master.context->OnLowerLayerUp();

    std::vector<std::pair<ControlRelayOutputBlock, uint16_t>> crobs;
    std::vector<std::pair<AnalogOutputInt16, uint16_t>> int16s;
    std::vector<std::pair<AnalogOutputInt32, uint16_t>> int32s;
    std::vector<std::pair<AnalogOutputFloat32, uint16_t>> floats;
    std::vector<std::pair<AnalogOutputDouble64, uint16_t>> doubles;

    CommandSet commands;
    // distinct indices, so the outstation's echo can be matched point by point
    for (size_t i = 0; i < count; ++i)
    {
        const auto index = static_cast<uint16_t>(i * 1000 + in.Next<uint8_t>());
        switch (kind)
        {
        case 0: {
            // a valid control code: an operation type (1 to 4), a trip/close code (0 to 2), maybe clear; never the
            // queue bit, which the outstation rightly refuses
            const uint8_t b = in.Next<uint8_t>();
            const auto code = static_cast<uint8_t>((1 + (b & 0x03)) | (((b >> 2) % 3) << 6) | (b & 0x20));
            const auto count = static_cast<uint8_t>(1 + in.Next<uint8_t>() % 255);
            ControlRelayOutputBlock crob(code, count, in.Next<uint32_t>(), in.Next<uint32_t>());
            crobs.emplace_back(crob, index);
            break;
        }
        case 1:
            int16s.emplace_back(AnalogOutputInt16(in.Next<int16_t>()), index);
            break;
        case 2:
            int32s.emplace_back(AnalogOutputInt32(in.Next<int32_t>()), index);
            break;
        case 3:
            floats.emplace_back(AnalogOutputFloat32(in.Next<float>()), index);
            break;
        default:
            doubles.emplace_back(AnalogOutputDouble64(in.Next<double>()), index);
            break;
        }
    }
    {
        std::vector<Indexed<ControlRelayOutputBlock>> c;
        for (const auto& p : crobs)
            c.push_back(WithIndex(p.first, p.second));
        std::vector<Indexed<AnalogOutputInt16>> a16;
        for (const auto& p : int16s)
            a16.push_back(WithIndex(p.first, p.second));
        std::vector<Indexed<AnalogOutputInt32>> a32;
        for (const auto& p : int32s)
            a32.push_back(WithIndex(p.first, p.second));
        std::vector<Indexed<AnalogOutputFloat32>> af;
        for (const auto& p : floats)
            af.push_back(WithIndex(p.first, p.second));
        std::vector<Indexed<AnalogOutputDouble64>> ad;
        for (const auto& p : doubles)
            ad.push_back(WithIndex(p.first, p.second));
        if (!c.empty())
            commands.Add(c);
        if (!a16.empty())
            commands.Add(a16);
        if (!a32.empty())
            commands.Add(a32);
        if (!af.empty())
            commands.Add(af);
        if (!ad.empty())
            commands.Add(ad);
    }

    bool done = false;
    TaskCompletion summary = TaskCompletion::FAILURE_NO_COMMS;
    size_t successes = 0;
    auto callback = [&](const ICommandTaskResult& result) {
        done = true;
        summary = result.summary;
        result.ForeachItem([&](const CommandPointResult& r) {
            if (r.state == CommandPointState::SUCCESS && r.status == CommandStatus::SUCCESS)
                ++successes;
        });
    };
    if (sbo)
        master.context->SelectAndOperate(std::move(commands), callback, TaskConfig::Default());
    else
        master.context->DirectOperate(std::move(commands), callback, TaskConfig::Default());
    master.exe->run_many();
    Pump(master, outstation);
    master.context->OnLowerLayerDown();

    if (!done)
        Fail("the command task never completed");
    if (summary != TaskCompletion::SUCCESS)
        Fail("the command task failed: " + std::string(TaskCompletionSpec::to_human_string(summary)));
    if (successes != count)
        Fail(std::to_string(successes) + " of " + std::to_string(count) + " points succeeded");

    const auto& handler = *outstation.cmdHandler;
    if (handler.crobOps.size() + handler.aoInt16Ops.size() + handler.aoInt32Ops.size() + handler.aoFloat32Ops.size()
            + handler.aoDouble64Ops.size()
        != count)
        Fail("the outstation operated " + std::to_string(handler.NumOperate()) + " points, of " + std::to_string(count));
    for (size_t i = 0; i < handler.crobOps.size(); ++i)
    {
        const auto& op = handler.crobOps[i];
        if (!crobs[i].first.ValuesEqual(op.value) || crobs[i].first.rawCode != op.value.rawCode
            || crobs[i].second != op.index)
            Fail("crob " + std::to_string(i) + " changed");
    }
    for (size_t i = 0; i < handler.aoInt16Ops.size(); ++i)
        CheckAnalog("int16", int16s, handler.aoInt16Ops[i].value, handler.aoInt16Ops[i].index, i);
    for (size_t i = 0; i < handler.aoInt32Ops.size(); ++i)
        CheckAnalog("int32", int32s, handler.aoInt32Ops[i].value, handler.aoInt32Ops[i].index, i);
    for (size_t i = 0; i < handler.aoFloat32Ops.size(); ++i)
        CheckAnalog("float32", floats, handler.aoFloat32Ops[i].value, handler.aoFloat32Ops[i].index, i);
    for (size_t i = 0; i < handler.aoDouble64Ops.size(); ++i)
        CheckAnalog("double64", doubles, handler.aoDouble64Ops[i].value, handler.aoDouble64Ops[i].index, i);
    return 0;
}
