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
#ifndef OPENDNP3_USERSTATUSCHANGE_H
#define OPENDNP3_USERSTATUSCHANGE_H

#include "opendnp3/gen/KeyChangeMethod.h"
#include "opendnp3/gen/UserOperation.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace opendnp3
{

/**
 * A user status change (g120v10) to add, delete or change a user on the outstation. The authority makes it,
 * certification data included; the master only sends it.
 */
struct UserStatusChange
{
    UserStatusChange(KeyChangeMethod keyChangeMethod,
                     UserOperation userOperation,
                     uint32_t statusChangeSeqNum,
                     uint16_t userRole,
                     uint16_t userRoleExpDays,
                     std::string userName,
                     std::vector<uint8_t> userPublicKey,
                     std::vector<uint8_t> certificationData)
        : keyChangeMethod(keyChangeMethod),
          userOperation(userOperation),
          statusChangeSeqNum(statusChangeSeqNum),
          userRole(userRole),
          userRoleExpDays(userRoleExpDays),
          userName(std::move(userName)),
          userPublicKey(std::move(userPublicKey)),
          certificationData(std::move(certificationData))
    {
    }

    KeyChangeMethod keyChangeMethod;
    UserOperation userOperation;
    /// the authority's status change sequence number (SCS)
    uint32_t statusChangeSeqNum;
    uint16_t userRole;
    /// days until the user's role expires
    uint16_t userRoleExpDays;
    std::string userName;
    /// the user's public key, for an asymmetric key change method; empty otherwise
    std::vector<uint8_t> userPublicKey;
    /// the authority's certification of the change
    std::vector<uint8_t> certificationData;
};

} // namespace opendnp3

#endif
