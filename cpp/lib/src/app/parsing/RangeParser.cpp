/*
 * Copyright 2013-2022 Step Function I/O, LLC
 * Modified 2024-2026 f0rw4rd (experimental fork)
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
#include "RangeParser.h"

#include "app/parsing/BufferedCollection.h"
#include "app/parsing/FreeFormatParser.h"
#include "gen/objects/Group1.h"
#include "gen/objects/Group10.h"
#include "gen/objects/Group102.h"
#include "gen/objects/Group121.h"
#include "gen/objects/Group20.h"
#include "gen/objects/Group21.h"
#include "gen/objects/Group3.h"
#include "gen/objects/Group30.h"
#include "gen/objects/Group31.h"
#include "gen/objects/Group34.h"
#include "gen/objects/Group40.h"
#include "gen/objects/Group50.h"
#include "logging/LogMacros.h"

#include "opendnp3/app/DeviceAttributes.h"

#include <ser4cpp/serialization/LittleEndian.h>

#include <cstring>

namespace opendnp3
{

RangeParser::RangeParser(const Range& range, size_t requiredSize, HandleFun handler)
    : range(range), requiredSize(requiredSize), handler(handler)
{
}

ParseResult RangeParser::ParseHeader(ser4cpp::rseq_t& buffer,
                                     const NumParser& numparser,
                                     const ParserSettings& settings,
                                     const HeaderRecord& record,
                                     Logger* pLogger,
                                     IAPDUHandler* pHandler)
{
    Range range;
    auto res = numparser.ParseRange(buffer, range, pLogger);
    if (res != ParseResult::OK)
    {
        return res;
    }

    FORMAT_LOGGER_BLOCK(pLogger, settings.LoggingLevel(), "%03u,%03u %s, %s [%u, %u]", record.group, record.variation,
                        GroupVariationSpec::to_human_string(record.enumeration),
                        QualifierCodeSpec::to_human_string(record.GetQualifierCode()), range.start, range.stop);

    if (settings.ExpectsContents())
    {
        return ParseRangeOfObjects(buffer, record, range, pLogger, pHandler);
    }

    if (pHandler)
    {
        pHandler->OnHeader(RangeHeader(record, range));
    }
    return ParseResult::OK;
}

ParseResult RangeParser::Process(const HeaderRecord& record,
                                 ser4cpp::rseq_t& buffer,
                                 IAPDUHandler* pHandler,
                                 Logger* pLogger) const
{
    if (buffer.length() < requiredSize)
    {
        SIMPLE_LOGGER_BLOCK(pLogger, flags::WARN, "Not enough data for specified objects");
        return ParseResult::NOT_ENOUGH_DATA_FOR_OBJECTS;
    }

    if (pHandler)
    {
        handler(record, range, buffer, *pHandler);
    }
    buffer.advance(requiredSize);
    return ParseResult::OK;
}

#define MACRO_PARSE_OBJECTS_WITH_RANGE(descriptor)                                                                     \
    case (GroupVariation::descriptor):                                                                                 \
        return RangeParser::FromFixedSize<descriptor>(range).Process(record, buffer, pHandler, pLogger);

ParseResult RangeParser::ParseRangeOfObjects(
    ser4cpp::rseq_t& buffer, const HeaderRecord& record, const Range& range, Logger* pLogger, IAPDUHandler* pHandler)
{
    switch (record.enumeration)
    {
    case (GroupVariation::Group1Var1):
        return RangeParser::FromBitfieldType<Binary>(range).Process(record, buffer, pHandler, pLogger);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group1Var2);

    case (GroupVariation::Group3Var1):
        return RangeParser::FromDoubleBitfieldType<DoubleBitBinary>(range).Process(record, buffer, pHandler, pLogger);
    case (GroupVariation::Group10Var1):
        return RangeParser::FromBitfieldType<BinaryOutputStatus>(range).Process(record, buffer, pHandler, pLogger);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group3Var2);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group10Var2);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group20Var1);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group20Var2);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group20Var3);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group20Var4);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group20Var5);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group20Var6);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group20Var7);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group20Var8);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var1);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var2);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var3);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var4);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var5);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var6);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var7);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var8);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var9);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var10);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var11);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group21Var12);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group30Var1);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group30Var2);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group30Var3);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group30Var4);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group30Var5);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group30Var6);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group31Var1);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group31Var2);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group31Var3);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group31Var4);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group31Var5);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group31Var6);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group31Var7);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group31Var8);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group34Var1);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group34Var2);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group34Var3);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group40Var1);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group40Var2);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group40Var3);
        MACRO_PARSE_OBJECTS_WITH_RANGE(Group40Var4);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group50Var4);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group102Var1);

        MACRO_PARSE_OBJECTS_WITH_RANGE(Group121Var1);

    case (GroupVariation::Group80Var1):
        return RangeParser::FromBitfieldType<IINValue>(range).Process(record, buffer, pHandler, pLogger);

    case (GroupVariation::Group110Var0):
        return ParseRangeOfOctetData(buffer, record, range, pLogger, pHandler);

    case (GroupVariation::Group112Var0):
        return ParseRangeOfOctetData(buffer, record, range, pLogger, pHandler);

    case (GroupVariation::Group0Var0):
    case (GroupVariation::Group0Var255):
        return ParseRangeOfDeviceAttributes(buffer, record, range, pLogger, pHandler);

    default:
        FORMAT_LOGGER_BLOCK(pLogger, flags::WARN, "Unsupported qualifier/object - %s - %i / %i",
                            QualifierCodeSpec::to_human_string(record.GetQualifierCode()), record.group,
                            record.variation);

        return ParseResult::INVALID_OBJECT_QUALIFIER;
    }
}

ParseResult RangeParser::ParseRangeOfOctetData(
    ser4cpp::rseq_t& buffer, const HeaderRecord& record, const Range& range, Logger* pLogger, IAPDUHandler* pHandler)
{
    if (record.variation > 0)
    {
        const auto COUNT = range.Count();
        auto size = record.variation * COUNT;
        if (buffer.length() < size)
        {
            SIMPLE_LOGGER_BLOCK(pLogger, flags::WARN, "Not enough data for specified octet objects");
            return ParseResult::NOT_ENOUGH_DATA_FOR_OBJECTS;
        }

        if (pHandler)
        {
            auto read = [range, record](ser4cpp::rseq_t& buffer, uint32_t pos) -> Indexed<OctetString> {
                const auto octetData = buffer.take(record.variation);
                OctetString octets(Buffer(octetData, octetData.length()));
                buffer.advance(record.variation);
                return WithIndex(octets, range.start + pos);
            };

            auto collection = CreateBufferedCollection<Indexed<OctetString>>(buffer, COUNT, read);

            pHandler->OnHeader(RangeHeader(record, range), collection);
        }

        buffer.advance(size);
        return ParseResult::OK;
    }
    else
    {
        SIMPLE_LOGGER_BLOCK(pLogger, flags::WARN, "Octet string variation 0 may only be used in requests");
        return ParseResult::INVALID_OBJECT;
    }
}

ParseResult RangeParser::ParseRangeOfDeviceAttributes(
    ser4cpp::rseq_t& buffer, const HeaderRecord& record, const Range& range, Logger* pLogger, IAPDUHandler* pHandler)
{
    // Group 0 device attributes: each object in the range is a variable-length
    // attribute value encoded as type_code(1) + length(1) + payload(length).
    const auto COUNT = range.Count();

    for (uint32_t i = 0; i < COUNT; ++i)
    {
        if (buffer.length() < 2)
        {
            SIMPLE_LOGGER_BLOCK(pLogger, flags::WARN, "Not enough data for device attribute type/length");
            return ParseResult::NOT_ENOUGH_DATA_FOR_OBJECTS;
        }

        const uint8_t typeCode = buffer[0];
        const uint8_t payloadLen = buffer[1];
        const size_t totalObjLen = 2 + static_cast<size_t>(payloadLen);

        if (buffer.length() < totalObjLen)
        {
            SIMPLE_LOGGER_BLOCK(pLogger, flags::WARN, "Not enough data for device attribute payload");
            return ParseResult::NOT_ENOUGH_DATA_FOR_OBJECTS;
        }

        if (pHandler)
        {
            // Build DeviceAttributeValue from the raw bytes
            const auto objData = buffer.take(totalObjLen);
            const uint8_t set = static_cast<uint8_t>(range.start + i);

            DeviceAttributeValue attr;
            const uint8_t* payload = static_cast<const uint8_t*>(objData) + 2;

            switch (typeCode)
            {
            case 1: // VisibleString
            {
                attr.type = DeviceAttrType::VISIBLE_STRING;
                attr.stringValue.assign(reinterpret_cast<const char*>(payload), payloadLen);
                break;
            }
            case 2: // UnsignedInt
            {
                attr.type = DeviceAttrType::UNSIGNED_INT;
                attr.unsignedValue = 0;
                if (payloadLen == 1)
                    attr.unsignedValue = payload[0];
                else if (payloadLen == 2)
                {
                    uint16_t v = 0;
                    std::memcpy(&v, payload, 2);
                    attr.unsignedValue = v;
                }
                else if (payloadLen == 4)
                {
                    std::memcpy(&attr.unsignedValue, payload, 4);
                }
                break;
            }
            case 3: // SignedInt
            {
                attr.type = DeviceAttrType::SIGNED_INT;
                attr.signedValue = 0;
                if (payloadLen == 1)
                    attr.signedValue = static_cast<int8_t>(payload[0]);
                else if (payloadLen == 2)
                {
                    uint16_t raw = 0;
                    std::memcpy(&raw, payload, 2);
                    attr.signedValue = static_cast<int16_t>(raw);
                }
                else if (payloadLen == 4)
                {
                    uint32_t raw = 0;
                    std::memcpy(&raw, payload, 4);
                    attr.signedValue = static_cast<int32_t>(raw);
                }
                break;
            }
            case 4: // FloatingPoint
            {
                attr.type = DeviceAttrType::FLOATING_POINT;
                if (payloadLen == 4)
                {
                    float f32;
                    std::memcpy(&f32, payload, 4);
                    attr.floatValue = static_cast<double>(f32);
                }
                else if (payloadLen == 8)
                {
                    std::memcpy(&attr.floatValue, payload, 8);
                }
                break;
            }
            case 5: // OctetString
            {
                attr.type = DeviceAttrType::OCTET_STRING;
                attr.rawValue.assign(payload, payload + payloadLen);
                break;
            }
            case 6: // BitString
            {
                attr.type = DeviceAttrType::BIT_STRING;
                attr.rawValue.assign(payload, payload + payloadLen);
                break;
            }
            case 7: // DNP3Time
            {
                attr.type = DeviceAttrType::DNP3_TIME;
                if (payloadLen == 6)
                {
                    attr.timeValue = 0;
                    for (int j = 0; j < 6; ++j)
                    {
                        attr.timeValue |= static_cast<uint64_t>(payload[j]) << (j * 8);
                    }
                }
                break;
            }
            case 8: // Unicode
            {
                attr.type = DeviceAttrType::UNICODE;
                attr.stringValue.assign(reinterpret_cast<const char*>(payload), payloadLen);
                break;
            }
            case 254: // AttrList
            {
                attr.type = DeviceAttrType::ATTR_LIST;
                attr.rawValue.assign(payload, payload + payloadLen);
                break;
            }
            case 255: // ExtAttrList
            {
                attr.type = DeviceAttrType::EXT_ATTR_LIST;
                attr.rawValue.assign(payload, payload + payloadLen);
                break;
            }
            default: {
                attr.type = DeviceAttrType::OCTET_STRING;
                attr.rawValue.assign(payload, payload + payloadLen);
                break;
            }
            }

            pHandler->OnDeviceAttribute(set, record.variation, attr);
        }

        buffer.advance(totalObjLen);
    }

    return ParseResult::OK;
}

} // namespace opendnp3
