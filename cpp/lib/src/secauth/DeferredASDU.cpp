/*
 * Copyright 2013-2022 Step Function I/O, LLC
 * Copyright 2024-2026 f0rw4rd (experimental fork)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "secauth/DeferredASDU.h"

#include <algorithm>
#include <cstring>

namespace opendnp3
{

bool DeferredASDU::Set(const ser4cpp::rseq_t& asdu)
{
    if (asdu.length() > MAX_ASDU_SIZE)
    {
        return false;
    }
    m_size = static_cast<uint32_t>(asdu.length());
    if (m_size > 0)
    {
        // an empty sequence's pointer may be null, which memcpy doesn't allow even for no bytes
        std::memcpy(m_buffer.data(), asdu, m_size);
    }
    m_isSet = true;
    return true;
}

ser4cpp::rseq_t DeferredASDU::Get() const
{
    if (!m_isSet)
    {
        return ser4cpp::rseq_t::empty();
    }
    return ser4cpp::rseq_t(m_buffer.data(), m_size);
}

} // namespace opendnp3
