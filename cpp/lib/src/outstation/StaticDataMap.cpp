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

#include "StaticDataMap.h"

namespace opendnp3
{

bool convert_to_event_class(PointClass pc, EventClass& ec)
{
    switch (pc)
    {
    case (PointClass::Class1):
        ec = EventClass::EC1;
        return true;
    case (PointClass::Class2):
        ec = EventClass::EC2;
        return true;
    case (PointClass::Class3):
        ec = EventClass::EC3;
        return true;
    default:
        return false;
    }
}

template<>
bool StaticDataMap<TimeAndIntervalSpec>::update(const TimeAndInterval& value,
                                                uint16_t index,
                                                EventMode /*mode*/,
                                                IEventReceiver& /*receiver*/)
{
    const auto iter = this->map.find(index);
    if (iter == this->map.end())
    {
        return false;
    }

    iter->second.value = value;

    return true;
}

template<> StaticBinaryVariation check_for_promotion<BinarySpec>(const Binary& value, StaticBinaryVariation variation)
{
    if (variation == StaticBinaryVariation::Group1Var1)
    {
        return BinarySpec::IsQualityOnlineOnly(value) ? variation : StaticBinaryVariation::Group1Var2;
    }

    return variation;
}

// A variation without flags can only report a point whose flags are ONLINE alone: any other point is reported in the
// variation of the same width with flags, as IEEE 1815 requires
static bool is_online_only(const Flags& flags)
{
    return flags.value == 0x01;
}

template<>
StaticCounterVariation check_for_promotion<CounterSpec>(const Counter& value, StaticCounterVariation variation)
{
    if (is_online_only(value.flags))
    {
        return variation;
    }

    switch (variation)
    {
    case (StaticCounterVariation::Group20Var5):
        return StaticCounterVariation::Group20Var1;
    case (StaticCounterVariation::Group20Var6):
        return StaticCounterVariation::Group20Var2;
    default:
        return variation;
    }
}

template<>
StaticFrozenCounterVariation check_for_promotion<FrozenCounterSpec>(const FrozenCounter& value,
                                                                    StaticFrozenCounterVariation variation)
{
    if (is_online_only(value.flags))
    {
        return variation;
    }

    switch (variation)
    {
    case (StaticFrozenCounterVariation::Group21Var9):
        return StaticFrozenCounterVariation::Group21Var1;
    case (StaticFrozenCounterVariation::Group21Var10):
        return StaticFrozenCounterVariation::Group21Var2;
    default:
        return variation;
    }
}

template<> StaticAnalogVariation check_for_promotion<AnalogSpec>(const Analog& value, StaticAnalogVariation variation)
{
    if (is_online_only(value.flags))
    {
        return variation;
    }

    switch (variation)
    {
    case (StaticAnalogVariation::Group30Var3):
        return StaticAnalogVariation::Group30Var1;
    case (StaticAnalogVariation::Group30Var4):
        return StaticAnalogVariation::Group30Var2;
    default:
        return variation;
    }
}

} // namespace opendnp3
