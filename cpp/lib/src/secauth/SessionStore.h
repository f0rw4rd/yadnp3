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
#ifndef OPENDNP3_SESSIONSTORE_H
#define OPENDNP3_SESSIONSTORE_H

#include "secauth/SessionKeys.h"

#include "opendnp3/gen/KeyStatus.h"
#include "opendnp3/util/TimeDuration.h"
#include "opendnp3/util/Timestamp.h"
#include "opendnp3/util/Uncopyable.h"

#include <cstdint>
#include <map>

namespace opendnp3
{

/**
 * Tracks session key state for a single user, including
 * key expiration time and authenticated message count.
 */
class SessionEntry
{
public:
    SessionEntry() : status(KeyStatus::NOT_INIT), authCount(0), maxAuthCount(0) {}

    SessionEntry(const TimeDuration& duration, uint32_t maxAuth)
        : status(KeyStatus::NOT_INIT), authCount(0), maxAuthCount(maxAuth), keyDuration(duration)
    {
    }

    void SetKeys(const SessionKeysView& view, const Timestamp& now);

    KeyStatus GetKeyStatus(const Timestamp& now) const;

    KeyStatus TryGetKeyView(SessionKeysView& view, const Timestamp& now) const;

    KeyStatus IncrementAuthCount(const Timestamp& now);

private:
    KeyStatus CheckTimeValidity(const Timestamp& now) const;

    KeyStatus status;
    SessionKeys keys;
    Timestamp expirationTime;
    uint32_t authCount;
    uint32_t maxAuthCount;
    TimeDuration keyDuration;
};

/**
 * Per-user session key storage.
 * Maps user numbers to SessionEntry instances.
 */
class SessionStore : private Uncopyable
{
public:
    SessionStore(const TimeDuration& keyDuration, uint32_t maxAuthCount)
        : keyDuration(keyDuration), maxAuthCount(maxAuthCount)
    {
    }

    /// Set session keys for a user
    void SetKeys(uint16_t userNum, const SessionKeysView& view, const Timestamp& now);

    /// Get the key status for a user
    KeyStatus GetKeyStatus(uint16_t userNum, const Timestamp& now) const;

    /// Try to get the session key view for a user
    KeyStatus TryGetKeyView(uint16_t userNum, SessionKeysView& view, const Timestamp& now) const;

    /// Increment the auth count for a user
    KeyStatus IncrementAuthCount(uint16_t userNum, const Timestamp& now);

    /// Forget a user's session keys, so new ones must be set
    void Invalidate(uint16_t userNum);

    /// Forget every user's session keys
    void Clear();

private:
    TimeDuration keyDuration;
    uint32_t maxAuthCount;
    mutable std::map<uint16_t, SessionEntry> sessions;

    SessionEntry& GetOrCreate(uint16_t userNum);
    const SessionEntry* Find(uint16_t userNum) const;
};

} // namespace opendnp3

#endif
