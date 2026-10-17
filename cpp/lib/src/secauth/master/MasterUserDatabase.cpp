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
#include "secauth/master/MasterUserDatabase.h"

namespace opendnp3
{

void MasterUserDatabase::AddOrUpdate(uint16_t userNum, const std::string& userName, const UpdateKey& key)
{
    users[userNum] = {userName, key};
}

bool MasterUserDatabase::GetUpdateKey(uint16_t userNum, UpdateKey& key) const
{
    const auto it = users.find(userNum);
    if (it == users.end() || !it->second.key.IsValid())
    {
        return false;
    }
    key = it->second.key;
    return true;
}

bool MasterUserDatabase::UserExists(uint16_t userNum) const
{
    return users.find(userNum) != users.end();
}

bool MasterUserDatabase::GetUserName(uint16_t userNum, std::string& userName) const
{
    const auto it = users.find(userNum);
    if (it == users.end())
    {
        return false;
    }
    userName = it->second.name;
    return true;
}

bool MasterUserDatabase::GetUserNum(const std::string& userName, uint16_t& userNum) const
{
    for (const auto& user : users)
    {
        if (user.second.name == userName)
        {
            userNum = user.first;
            return true;
        }
    }
    return false;
}

std::vector<uint16_t> MasterUserDatabase::GetUserNums() const
{
    std::vector<uint16_t> nums;
    for (const auto& user : users)
    {
        nums.push_back(user.first);
    }
    return nums;
}

} // namespace opendnp3
