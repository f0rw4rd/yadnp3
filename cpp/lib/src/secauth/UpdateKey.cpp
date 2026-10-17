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
#include "opendnp3/secauth/UpdateKey.h"

#include <algorithm>
#include <cstring>

namespace opendnp3
{

UpdateKey::UpdateKey() : m_algorithm(KeyWrapAlgorithm::UNDEFINED), m_size(0)
{
    m_buffer.fill(0);
}

UpdateKey::UpdateKey(uint8_t repeat, KeyWrapAlgorithm algorithm) : m_algorithm(algorithm), m_size(0)
{
    switch (algorithm)
    {
    case KeyWrapAlgorithm::AES_128:
        m_size = UPDATE_KEY_SIZE_128;
        break;
    case KeyWrapAlgorithm::AES_256:
        m_size = UPDATE_KEY_SIZE_256;
        break;
    default:
        m_size = 0;
        m_algorithm = KeyWrapAlgorithm::UNDEFINED;
        break;
    }
    m_buffer.fill(repeat);
}

UpdateKey::UpdateKey(const Buffer& key) : m_algorithm(KeyWrapAlgorithm::UNDEFINED), m_size(0)
{
    m_buffer.fill(0);
    Initialize(key);
}

UpdateKey::View UpdateKey::GetView() const
{
    return View(m_algorithm, Buffer(m_buffer.data(), m_size));
}

bool UpdateKey::Initialize(const Buffer& key)
{
    auto algo = GetKeyWrapAlgorithm(static_cast<uint32_t>(key.length));
    if (algo == KeyWrapAlgorithm::UNDEFINED)
    {
        m_algorithm = KeyWrapAlgorithm::UNDEFINED;
        m_size = 0;
        return false;
    }

    m_algorithm = algo;
    m_size = static_cast<uint8_t>(key.length);
    std::memcpy(m_buffer.data(), key.data, m_size);
    return true;
}

KeyWrapAlgorithm UpdateKey::GetKeyWrapAlgorithm(uint32_t size)
{
    switch (size)
    {
    case UPDATE_KEY_SIZE_128:
        return KeyWrapAlgorithm::AES_128;
    case UPDATE_KEY_SIZE_256:
        return KeyWrapAlgorithm::AES_256;
    default:
        return KeyWrapAlgorithm::UNDEFINED;
    }
}

} // namespace opendnp3
