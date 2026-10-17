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
#ifndef OPENDNP3_BITFIELDRANGEWRITEITERATOR_H
#define OPENDNP3_BITFIELDRANGEWRITEITERATOR_H

#include "opendnp3/gen/DoubleBit.h"

#include <ser4cpp/serialization/LittleEndian.h>

namespace opendnp3
{

// A facade for writing APDUs to an external buffer: a start/stop range of packed values of BITS bits each, the first
// in the least significant bits (1 for binaries and binary output status, 2 for double-bit binaries)
template<class IndexType, unsigned BITS = 1> class BitfieldRangeWriteIterator
{
public:
    static BitfieldRangeWriteIterator Null()
    {
        auto buffer = ser4cpp::wseq_t::empty();
        return BitfieldRangeWriteIterator(0, buffer);
    }

    BitfieldRangeWriteIterator(typename IndexType::type_t start_, ser4cpp::wseq_t& position_)
        : start(start_),
          count(0),
          maxCount(0),
          isValid(position_.length() >= (2 * IndexType::size)),
          range(position_),
          pPosition(&position_)
    {
        if (isValid)
        {
            IndexType::write_to(range, start_);
            pPosition->advance(2 * IndexType::size);
            maxCount = (pPosition->length() * 8) / BITS;
        }
    }

    ~BitfieldRangeWriteIterator()
    {
        if (isValid && count > 0)
        {
            typename IndexType::type_t stop = start + count - 1;
            IndexType::write_to(range, stop);

            const size_t num = (static_cast<size_t>(count) * BITS + 7) / 8;

            pPosition->advance(num);
        }
    }

    bool Write(bool value)
    {
        return WriteBits(value ? 1 : 0);
    }

    bool Write(DoubleBit value)
    {
        return WriteBits(static_cast<uint8_t>(value));
    }

    bool WriteBits(uint8_t value)
    {
        if (isValid && count < maxCount)
        {
            const size_t position = static_cast<size_t>(count) * BITS;
            const auto byte = position / 8;
            const auto bit = position % 8;

            if (bit == 0)
            {
                (*pPosition)[byte] = 0; // initialize byte to 0
            }

            const auto mask = static_cast<uint8_t>((1u << BITS) - 1);
            (*pPosition)[byte] = static_cast<uint8_t>((*pPosition)[byte] | ((value & mask) << bit));

            ++count;
            return true;
        }
        else
        {
            return false;
        }
    }

    bool IsValid() const
    {
        return isValid;
    }

private:
    typename IndexType::type_t start;
    typename IndexType::type_t count;

    size_t maxCount;

    bool isValid;

    ser4cpp::wseq_t range; // make a copy to record where we write the range
    ser4cpp::wseq_t* pPosition;
};

} // namespace opendnp3

#endif
