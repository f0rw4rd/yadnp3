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
#include <outstation/Database.h>

#include <catch.hpp>

#include <deque>
#include <limits>

using namespace opendnp3;

#define SUITE(name) "DatabaseTestSuite - " name

namespace
{

struct QueueingEventReceiver final : public IEventReceiver
{
    void Update(const Event<BinarySpec>& evt) override
    {
        binaryEvents.push_back(evt);
    }
    void Update(const Event<DoubleBitBinarySpec>&) override {}
    void Update(const Event<AnalogSpec>& evt) override
    {
        analogEvents.push_back(evt);
    }
    void Update(const Event<CounterSpec>& evt) override
    {
        counterEvents.push_back(evt);
    }
    void Update(const Event<FrozenCounterSpec>&) override {}
    void Update(const Event<BinaryOutputStatusSpec>&) override {}
    void Update(const Event<AnalogOutputStatusSpec>&) override {}
    void Update(const Event<OctetStringSpec>&) override {}

    std::deque<Event<BinarySpec>> binaryEvents;
    std::deque<Event<AnalogSpec>> analogEvents;
    std::deque<Event<CounterSpec>> counterEvents;
};

struct DatabaseFixture
{
    explicit DatabaseFixture(const DatabaseConfig& config) : db(config, receiver, timeSource, StaticTypeBitField::AllTypes())
    {
    }

    QueueingEventReceiver receiver;
    IDnpTimeSource timeSource;
    Database db;
};

DatabaseConfig AnalogConfigWith(double deadband)
{
    DatabaseConfig config;
    config.analog_input[0].clazz = PointClass::Class1;
    config.analog_input[0].deadband = deadband;
    return config;
}

DatabaseConfig CounterConfigWith(uint32_t deadband)
{
    DatabaseConfig config;
    config.counter[0].clazz = PointClass::Class1;
    config.counter[0].deadband = deadband;
    return config;
}

// updates point 0 and checks whether that produced exactly the one event for the new value
template<class Meas, class Queue> void UpdateExpecting(bool isEvent, const Meas& value, DatabaseFixture& t, Queue& queue)
{
    REQUIRE(t.db.Update(value, 0, EventMode::Detect));
    if (isEvent)
    {
        REQUIRE(queue.size() == 1);
        REQUIRE(queue.front().value.value == value.value);
        REQUIRE(queue.front().value.flags.value == value.flags.value);
        REQUIRE(queue.front().index == 0);
        queue.pop_front();
    }
    else
    {
        REQUIRE(queue.empty());
    }
}

bool IsAnalogEvent(double oldValue, double newValue, double deadband)
{
    AnalogConfig config;
    config.deadband = deadband;
    return AnalogSpec::IsEvent(Analog(oldValue), Analog(newValue), config);
}

bool IsCounterEvent(uint32_t oldValue, uint32_t newValue, uint32_t deadband)
{
    CounterConfig config;
    config.deadband = deadband;
    return CounterSpec::IsEvent(Counter(oldValue), Counter(newValue), config);
}

} // namespace

TEST_CASE(SUITE("An analog change of exactly the deadband is no event"))
{
    REQUIRE_FALSE(IsAnalogEvent(0, 1, 1));
}

TEST_CASE(SUITE("Negative analog changes are measured by their size"))
{
    REQUIRE(IsAnalogEvent(-34, -36, 1));
    REQUIRE_FALSE(IsAnalogEvent(-34, -36, 2));
}

TEST_CASE(SUITE("An analog change across the whole double range is an event"))
{
    const auto max = std::numeric_limits<double>::max();
    REQUIRE(IsAnalogEvent(-max, max, 1));
}

TEST_CASE(SUITE("A counter change across the whole range is an event"))
{
    const auto max = std::numeric_limits<uint32_t>::max();
    REQUIRE(IsCounterEvent(0, max, max - 1));
    REQUIRE(IsCounterEvent(max, 0, max - 1));
    REQUIRE_FALSE(IsCounterEvent(0, max, max));
}

TEST_CASE(SUITE("An update to the value already held is no event"))
{
    DatabaseConfig config;
    config.binary_input[0].clazz = PointClass::Class1;
    config.analog_input[0].clazz = PointClass::Class1;
    config.counter[0].clazz = PointClass::Class1;
    DatabaseFixture t(config);
    UpdateExpecting(false, Binary(false, Flags(static_cast<uint8_t>(BinaryQuality::RESTART))), t, t.receiver.binaryEvents);
    UpdateExpecting(false, Analog(0, Flags(static_cast<uint8_t>(AnalogQuality::RESTART))), t, t.receiver.analogEvents);
    UpdateExpecting(false, Counter(0, Flags(static_cast<uint8_t>(CounterQuality::RESTART))), t, t.receiver.counterEvents);
}

TEST_CASE(SUITE("A change of the flags is an event"))
{
    DatabaseConfig config;
    config.binary_input[0].clazz = PointClass::Class1;
    config.analog_input[0].clazz = PointClass::Class1;
    config.counter[0].clazz = PointClass::Class1;
    DatabaseFixture t(config);
    UpdateExpecting(true, Binary(false, Flags(static_cast<uint8_t>(BinaryQuality::ONLINE))), t, t.receiver.binaryEvents);
    UpdateExpecting(true, Analog(0, Flags(static_cast<uint8_t>(AnalogQuality::ONLINE))), t, t.receiver.analogEvents);
    UpdateExpecting(true, Counter(0, Flags(static_cast<uint8_t>(CounterQuality::ONLINE))), t, t.receiver.counterEvents);
}

TEST_CASE(SUITE("An analog deadband is measured from the last reported value"))
{
    DatabaseFixture t(AnalogConfigWith(5));
    const Flags restart(static_cast<uint8_t>(AnalogQuality::RESTART));
    UpdateExpecting(false, Analog(-2, restart), t, t.receiver.analogEvents);
    UpdateExpecting(false, Analog(5, restart), t, t.receiver.analogEvents);
    // 6 from the reported 0
    UpdateExpecting(true, Analog(6, restart), t, t.receiver.analogEvents);
    UpdateExpecting(false, Analog(1, restart), t, t.receiver.analogEvents);
    // 7 from the reported 6
    UpdateExpecting(true, Analog(-1, restart), t, t.receiver.analogEvents);
}

TEST_CASE(SUITE("A counter deadband is measured from the last reported value"))
{
    DatabaseFixture t(CounterConfigWith(5));
    const Flags restart(static_cast<uint8_t>(CounterQuality::RESTART));
    UpdateExpecting(false, Counter(1, restart), t, t.receiver.counterEvents);
    UpdateExpecting(false, Counter(5, restart), t, t.receiver.counterEvents);
    // 6 from the reported 0
    UpdateExpecting(true, Counter(6, restart), t, t.receiver.counterEvents);
    UpdateExpecting(false, Counter(1, restart), t, t.receiver.counterEvents);
    // 6 from the reported 6, down
    UpdateExpecting(true, Counter(0, restart), t, t.receiver.counterEvents);
}
