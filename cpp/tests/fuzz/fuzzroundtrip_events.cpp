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

// Event round trip fuzzer: the input generates one event, with a time, for each point; a master reads the events of
// classes 1, 2 and 3 over a loopback, confirming each fragment; every event must come back with its value, flags and
// time. The event variations, chosen by the input, include relative times, which need common time of occurrence
// objects whenever the events are more than 65535 ms apart.
#include "utils/MasterTestFixture.h"
#include "utils/OutstationTestObject.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

using namespace opendnp3;

namespace
{

constexpr uint16_t NUM = 6;

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

[[noreturn]] void Mismatch(const char* type, uint16_t index, const std::string& detail)
{
    std::fprintf(stderr, "event round trip mismatch: %s %u: %s\n", type, index, detail.c_str());
    std::abort();
}

bool SameDouble(double a, double b)
{
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

template<class Map> const typename Map::mapped_type& Received(const char* type, const Map& map, uint16_t i)
{
    const auto it = map.find(i);
    if (it == map.end())
        Mismatch(type, i, "no event");
    return it->second;
}

template<class T> void CheckCommon(const char* type, uint16_t i, const T& expected, const T& actual, bool hasTime)
{
    if (expected.flags.value != actual.flags.value)
        Mismatch(type, i, "flags " + std::to_string(expected.flags.value) + " became " + std::to_string(actual.flags.value));
    if (hasTime && expected.time.value != actual.time.value)
        Mismatch(type, i, "time " + std::to_string(expected.time.value) + " became " + std::to_string(actual.time.value));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Reader in(data, size);

    const uint8_t choice = in.Next<uint8_t>();
    const auto binaryVariation = static_cast<EventBinaryVariation>(1 + (choice & 1)); // g2v2 or g2v3
    const auto doubleVariation = static_cast<EventDoubleBinaryVariation>(1 + ((choice >> 1) & 1)); // g4v2 or g4v3
    const bool binaryTime = true;

    DatabaseConfig database;
    for (uint16_t i = 0; i < NUM; ++i)
    {
        database.binary_input[i].clazz = PointClass::Class1;
        database.binary_input[i].evariation = binaryVariation;
        database.double_binary[i].clazz = PointClass::Class1;
        database.double_binary[i].evariation = doubleVariation;
        database.counter[i].clazz = PointClass::Class2;
        database.counter[i].evariation = EventCounterVariation::Group22Var5;
        database.analog_input[i].clazz = PointClass::Class3;
        database.analog_input[i].evariation = EventAnalogVariation::Group32Var8;
        database.binary_output_status[i].clazz = PointClass::Class1;
        database.binary_output_status[i].evariation = EventBinaryOutputStatusVariation::Group11Var2;
        database.analog_output_status[i].clazz = PointClass::Class2;
        database.analog_output_status[i].evariation = EventAnalogOutputStatusVariation::Group42Var8;
    }

    OutstationConfig config;
    config.params.maxTxFragSize = 249 + ((choice >> 2) % 4) * 600;
    config.eventBufferConfig = EventBufferConfig::AllTypes(NUM);
    OutstationTestObject outstation(config, database);

    std::map<uint16_t, Binary> binaries;
    std::map<uint16_t, DoubleBitBinary> doubles;
    std::map<uint16_t, Counter> counters;
    std::map<uint16_t, Analog> analogs;
    std::map<uint16_t, BinaryOutputStatus> boStatus;
    std::map<uint16_t, AnalogOutputStatus> aoStatus;

    // DNP3 times are 48 bits of milliseconds
    auto time = [&]() { return DNPTime(in.Next<uint64_t>() & 0xFFFFFFFFFFFFULL); };

    outstation.LowerLayerUp();
    outstation.Transaction([&](IUpdateHandler& db) {
        for (uint16_t i = 0; i < NUM; ++i)
        {
            const bool b = in.Next<uint8_t>() & 1;
            binaries[i] = Binary(b, Flags((in.Next<uint8_t>() & 0x7F) | (b ? 0x80 : 0)), time());
            db.Update(binaries[i], i, EventMode::Force);

            const auto dbit = static_cast<DoubleBit>(in.Next<uint8_t>() & 3);
            doubles[i] = DoubleBitBinary(dbit, Flags(in.Next<uint8_t>() & 0x3F), time());
            db.Update(doubles[i], i, EventMode::Force);

            counters[i] = Counter(in.Next<uint32_t>(), Flags(in.Next<uint8_t>()), time());
            db.Update(counters[i], i, EventMode::Force);

            analogs[i] = Analog(in.Next<double>(), Flags(in.Next<uint8_t>()), time());
            db.Update(analogs[i], i, EventMode::Force);

            const bool o = in.Next<uint8_t>() & 1;
            boStatus[i] = BinaryOutputStatus(o, Flags((in.Next<uint8_t>() & 0x7F) | (o ? 0x80 : 0)), time());
            db.Update(boStatus[i], i, EventMode::Force);

            aoStatus[i] = AnalogOutputStatus(in.Next<double>(), Flags(in.Next<uint8_t>()), time());
            db.Update(aoStatus[i], i, EventMode::Force);
        }
    });

    MasterTestFixture master(NoStartupTasks());
    master.context->OnLowerLayerUp();
    master.context->ScanClasses(ClassField::AllEventClasses(), master.meas, TaskConfig::Default());
    master.exe->run_many();

    const bool trace = std::getenv("YADNP3_FUZZ_TRACE") != nullptr;
    for (int round = 0; round < 200; ++round)
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
            break;
    }
    master.context->OnLowerLayerDown();

    const auto& soe = *master.meas;
    for (uint16_t i = 0; i < NUM; ++i)
    {
        const auto& b = Received("binary", soe.binarySOE, i).meas;
        if (b.value != binaries[i].value)
            Mismatch("binary", i, "value");
        CheckCommon("binary", i, binaries[i], b, binaryTime);

        const auto& d = Received("double-bit", soe.doubleBinarySOE, i).meas;
        if (d.value != doubles[i].value)
            Mismatch("double-bit", i, "value");
        CheckCommon("double-bit", i, doubles[i], d, true);

        const auto& c = Received("counter", soe.counterSOE, i).meas;
        if (c.value != counters[i].value)
            Mismatch("counter", i, std::to_string(counters[i].value) + " became " + std::to_string(c.value));
        CheckCommon("counter", i, counters[i], c, true);

        const auto& a = Received("analog", soe.analogSOE, i).meas;
        if (!SameDouble(a.value, analogs[i].value))
            Mismatch("analog", i, "value");
        CheckCommon("analog", i, analogs[i], a, true);

        const auto& bo = Received("binary output status", soe.binaryOutputStatusSOE, i).meas;
        if (bo.value != boStatus[i].value)
            Mismatch("binary output status", i, "value");
        CheckCommon("binary output status", i, boStatus[i], bo, true);

        const auto& ao = Received("analog output status", soe.analogOutputStatusSOE, i).meas;
        if (!SameDouble(ao.value, aoStatus[i].value))
            Mismatch("analog output status", i, "value");
        CheckCommon("analog output status", i, aoStatus[i], ao, true);
    }
    return 0;
}
