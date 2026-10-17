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

// Round trip fuzzer: the input sets the values of an outstation's points; the outstation answers an integrity poll,
// over as many fragments as it takes; the master parses the responses; every point must come back as the
// outstation holds it. Only the variations that report a value exactly are used.
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

constexpr uint16_t NUM = 8;

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
    std::fprintf(stderr, "round trip mismatch: %s %u: %s\n", type, index, detail.c_str());
    std::abort();
}

std::string Exact(double value)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%a", value);
    return buf;
}

// ONLINE alone for an even byte, else the byte's flags
uint8_t OnlineOr(uint8_t flags)
{
    return (flags & 1) ? flags : 0x01;
}

bool SameDouble(double a, double b)
{
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

template<class T> void CheckFlags(const char* type, uint16_t i, const T& expected, const T& actual)
{
    if (expected.flags.value != actual.flags.value)
        Mismatch(type, i, "flags " + std::to_string(expected.flags.value) + " became " + std::to_string(actual.flags.value));
}

template<class Map> const typename Map::mapped_type& Received(const char* type, const Map& map, uint16_t i)
{
    const auto it = map.find(i);
    if (it == map.end())
        Mismatch(type, i, "not reported");
    return it->second;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Reader in(data, size);

    // the packed variations, which report the state alone, or those with flags
    const uint8_t packed = in.Next<uint8_t>();
    DatabaseConfig database;
    for (uint16_t i = 0; i < NUM; ++i)
    {
        database.binary_input[i].svariation
            = (packed & 1) ? StaticBinaryVariation::Group1Var1 : StaticBinaryVariation::Group1Var2;
        database.double_binary[i].svariation
            = (packed & 2) ? StaticDoubleBinaryVariation::Group3Var1 : StaticDoubleBinaryVariation::Group3Var2;
        database.counter[i].svariation = StaticCounterVariation::Group20Var1;
        database.analog_input[i].svariation = StaticAnalogVariation::Group30Var6;
        database.binary_output_status[i].svariation = (packed & 4) ? StaticBinaryOutputStatusVariation::Group10Var1
                                                                    : StaticBinaryOutputStatusVariation::Group10Var2;
        database.analog_output_status[i].svariation = StaticAnalogOutputStatusVariation::Group40Var4;
        database.octet_string[i].svariation = StaticOctetStringVariation::Group110Var0;
    }

    OutstationConfig config;
    config.params.maxTxFragSize = 249 + (in.Next<uint8_t>() % 4) * 600;
    OutstationTestObject outstation(config, database);

    std::map<uint16_t, Binary> binaries;
    std::map<uint16_t, DoubleBitBinary> doubles;
    std::map<uint16_t, Counter> counters;
    std::map<uint16_t, Analog> analogs;
    std::map<uint16_t, BinaryOutputStatus> boStatus;
    std::map<uint16_t, AnalogOutputStatus> aoStatus;
    std::map<uint16_t, std::string> octets;

    outstation.Transaction([&](IUpdateHandler& db) {
        for (uint16_t i = 0; i < NUM; ++i)
        {
            // the state bit is the value's, so only the other flag bits come from the input
            const bool b = in.Next<uint8_t>() & 1;
            binaries[i] = Binary(b, Flags((OnlineOr(in.Next<uint8_t>()) & 0x7F) | (b ? 0x80 : 0)));
            db.Update(binaries[i], i, EventMode::Suppress);

            const auto dbit = static_cast<DoubleBit>(in.Next<uint8_t>() & 3);
            doubles[i] = DoubleBitBinary(dbit, Flags(OnlineOr(in.Next<uint8_t>()) & 0x3F));
            db.Update(doubles[i], i, EventMode::Suppress);

            counters[i] = Counter(in.Next<uint32_t>(), Flags(in.Next<uint8_t>()));
            db.Update(counters[i], i, EventMode::Suppress);

            analogs[i] = Analog(in.Next<double>(), Flags(in.Next<uint8_t>()));
            db.Update(analogs[i], i, EventMode::Suppress);

            const bool o = in.Next<uint8_t>() & 1;
            boStatus[i] = BinaryOutputStatus(o, Flags((OnlineOr(in.Next<uint8_t>()) & 0x7F) | (o ? 0x80 : 0)));
            db.Update(boStatus[i], i, EventMode::Suppress);

            aoStatus[i] = AnalogOutputStatus(in.Next<double>(), Flags(in.Next<uint8_t>()));
            db.Update(aoStatus[i], i, EventMode::Suppress);

            std::string text(in.Next<uint8_t>() % 40, '\0');
            for (auto& c : text)
                c = static_cast<char>(in.Next<uint8_t>());
            octets[i] = text;
            db.Update(OctetString(opendnp3::Buffer(reinterpret_cast<const uint8_t*>(text.data()), text.size())), i,
                      EventMode::Suppress);
        }
    });
    outstation.LowerLayerUp();

    MasterTestFixture master(NoStartupTasks());
    master.context->OnLowerLayerUp();

    // the master polls for class 0; every message goes across, until neither side has anything more to send
    master.context->ScanClasses(ClassField(ClassField::CLASS_0), master.meas, TaskConfig::Default());
    master.exe->run_many();
    // YADNP3_FUZZ_TRACE prints the traffic, to debug a mismatch
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

    const auto& soe = *master.meas;
    for (uint16_t i = 0; i < NUM; ++i)
    {
        const auto& b = Received("binary", soe.binarySOE, i).meas;
        if (b.value != binaries[i].value)
            Mismatch("binary", i, "value");
        CheckFlags("binary", i, binaries[i], b);

        const auto& d = Received("double-bit", soe.doubleBinarySOE, i).meas;
        if (d.value != doubles[i].value)
            Mismatch("double-bit", i, "value");

        const auto& c = Received("counter", soe.counterSOE, i).meas;
        if (c.value != counters[i].value)
            Mismatch("counter", i, std::to_string(counters[i].value) + " became " + std::to_string(c.value));
        CheckFlags("counter", i, counters[i], c);

        const auto& a = Received("analog", soe.analogSOE, i).meas;
        if (!SameDouble(a.value, analogs[i].value))
            Mismatch("analog", i, Exact(analogs[i].value) + " became " + Exact(a.value));
        CheckFlags("analog", i, analogs[i], a);

        const auto& bo = Received("binary output status", soe.binaryOutputStatusSOE, i).meas;
        if (bo.value != boStatus[i].value)
            Mismatch("binary output status", i, "value");
        CheckFlags("binary output status", i, boStatus[i], bo);

        const auto& ao = Received("analog output status", soe.analogOutputStatusSOE, i).meas;
        if (!SameDouble(ao.value, aoStatus[i].value))
            Mismatch("analog output status", i, Exact(aoStatus[i].value) + " became " + Exact(ao.value));
        CheckFlags("analog output status", i, aoStatus[i], ao);

        const auto& o = Received("octet string", soe.octetStringSOE, i).meas;
        const auto buffer = o.ToBuffer();
        const std::string got(reinterpret_cast<const char*>(buffer.data), buffer.length);
        // an empty string is reported as one zero octet
        const std::string expected = octets[i].empty() ? std::string(1, '\0') : octets[i];
        if (got != expected)
            Mismatch("octet string", i, "content");
    }
    return 0;
}
