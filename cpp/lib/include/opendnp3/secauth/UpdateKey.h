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
#ifndef OPENDNP3_UPDATEKEY_H
#define OPENDNP3_UPDATEKEY_H

#include "opendnp3/gen/KeyWrapAlgorithm.h"
#include "opendnp3/util/Buffer.h"

#include <array>
#include <cstdint>

namespace opendnp3
{

/**
 * Container for a SA5 Update Key.
 * Stores the key data and the associated key wrap algorithm.
 * Supports 128-bit (16 byte) and 256-bit (32 byte) keys.
 */
class UpdateKey
{
public:
    static const uint8_t UPDATE_KEY_SIZE_128 = 16;
    static const uint8_t UPDATE_KEY_SIZE_256 = 32;

    /// A read-only view of the update key
    struct View
    {
        View() : algorithm(KeyWrapAlgorithm::UNDEFINED) {}
        View(KeyWrapAlgorithm algorithm_, Buffer key_) : algorithm(algorithm_), data(key_) {}

        KeyWrapAlgorithm algorithm;
        Buffer data;
    };

    /// Construct an invalid (undefined) key
    UpdateKey();

    /// Test constructor: fills key with repeated byte value
    UpdateKey(uint8_t repeat, KeyWrapAlgorithm algorithm);

    /// Construct from raw key data. Key size determines the algorithm.
    explicit UpdateKey(const Buffer& key);

    /// Get a read-only view of the key
    View GetView() const;

    /// Check if the key is valid (has been initialized with valid data)
    bool IsValid() const
    {
        return m_algorithm != KeyWrapAlgorithm::UNDEFINED;
    }

    /**
     * Initialize from raw key data.
     * Only accepts 128-bit (16 byte) or 256-bit (32 byte) keys.
     * @return true if key size was valid, false otherwise
     */
    bool Initialize(const Buffer& key);

private:
    static KeyWrapAlgorithm GetKeyWrapAlgorithm(uint32_t size);

    KeyWrapAlgorithm m_algorithm;
    std::array<uint8_t, UPDATE_KEY_SIZE_256> m_buffer;
    uint8_t m_size;
};

} // namespace opendnp3

#endif
