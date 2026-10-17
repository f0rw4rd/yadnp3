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
#include "secauth/SessionStore.h"

namespace opendnp3
{

// --- SessionEntry ---

void SessionEntry::SetKeys(const SessionKeysView& view, const Timestamp& now)
{
    keys.SetKeys(view);
    status = KeyStatus::OK;
    authCount = 0;
    expirationTime = now + keyDuration;
}

KeyStatus SessionEntry::GetKeyStatus(const Timestamp& now) const
{
    if (status != KeyStatus::OK)
    {
        return status;
    }
    return CheckTimeValidity(now);
}

KeyStatus SessionEntry::TryGetKeyView(SessionKeysView& view, const Timestamp& now) const
{
    auto result = GetKeyStatus(now);
    if (result == KeyStatus::OK)
    {
        view = keys.GetView();
    }
    return result;
}

KeyStatus SessionEntry::IncrementAuthCount(const Timestamp& now)
{
    if (status != KeyStatus::OK)
    {
        return status;
    }

    auto result = CheckTimeValidity(now);
    if (result != KeyStatus::OK)
    {
        status = result;
        return result;
    }

    ++authCount;
    if (authCount >= maxAuthCount)
    {
        status = KeyStatus::COMM_FAIL;
        return status;
    }

    return KeyStatus::OK;
}

KeyStatus SessionEntry::CheckTimeValidity(const Timestamp& now) const
{
    if (now >= expirationTime)
    {
        return KeyStatus::COMM_FAIL;
    }
    return KeyStatus::OK;
}

// --- SessionStore ---

void SessionStore::SetKeys(uint16_t userNum, const SessionKeysView& view, const Timestamp& now)
{
    auto& entry = GetOrCreate(userNum);
    entry.SetKeys(view, now);
}

KeyStatus SessionStore::GetKeyStatus(uint16_t userNum, const Timestamp& now) const
{
    auto entry = Find(userNum);
    if (!entry)
    {
        return KeyStatus::NOT_INIT;
    }
    return entry->GetKeyStatus(now);
}

KeyStatus SessionStore::TryGetKeyView(uint16_t userNum, SessionKeysView& view, const Timestamp& now) const
{
    auto entry = Find(userNum);
    if (!entry)
    {
        return KeyStatus::NOT_INIT;
    }
    return entry->TryGetKeyView(view, now);
}

void SessionStore::Invalidate(uint16_t userNum)
{
    sessions.erase(userNum);
}

void SessionStore::Clear()
{
    sessions.clear();
}

KeyStatus SessionStore::IncrementAuthCount(uint16_t userNum, const Timestamp& now)
{
    auto it = sessions.find(userNum);
    if (it == sessions.end())
    {
        return KeyStatus::NOT_INIT;
    }
    return it->second.IncrementAuthCount(now);
}

SessionEntry& SessionStore::GetOrCreate(uint16_t userNum)
{
    auto it = sessions.find(userNum);
    if (it == sessions.end())
    {
        auto result = sessions.emplace(userNum, SessionEntry(keyDuration, maxAuthCount));
        return result.first->second;
    }
    return it->second;
}

const SessionEntry* SessionStore::Find(uint16_t userNum) const
{
    auto it = sessions.find(userNum);
    if (it != sessions.end())
    {
        return &it->second;
    }
    return nullptr;
}

} // namespace opendnp3
