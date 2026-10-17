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
#ifndef OPENDNP3_MASTERUSERDATABASE_H
#define OPENDNP3_MASTERUSERDATABASE_H

#include "opendnp3/secauth/UpdateKey.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace opendnp3
{

/**
 * The master's users: each user number has a name and an update key.
 */
class MasterUserDatabase final
{
public:
    /// adds the user, or replaces its name and update key
    void AddOrUpdate(uint16_t userNum, const std::string& userName, const UpdateKey& key);

    /// the user's update key, if the user exists and the key is valid
    bool GetUpdateKey(uint16_t userNum, UpdateKey& key) const;

    bool UserExists(uint16_t userNum) const;

    bool GetUserName(uint16_t userNum, std::string& userName) const;

    bool GetUserNum(const std::string& userName, uint16_t& userNum) const;

    std::vector<uint16_t> GetUserNums() const;

private:
    struct UserEntry
    {
        std::string name;
        UpdateKey key;
    };

    std::map<uint16_t, UserEntry> users;
};

} // namespace opendnp3

#endif
