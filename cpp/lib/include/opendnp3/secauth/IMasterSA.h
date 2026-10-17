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
#ifndef OPENDNP3_IMASTERSA_H
#define OPENDNP3_IMASTERSA_H

#include "opendnp3/app/User.h"
#include "opendnp3/gen/KeyStatus.h"
#include "opendnp3/master/IMaster.h"
#include "opendnp3/master/TaskConfig.h"
#include "opendnp3/secauth/UpdateKey.h"
#include "opendnp3/secauth/UserStatusChange.h"

#include <string>

namespace opendnp3
{

/**
 * A master with Secure Authentication (IEEE 1815-2012, SAv5). A task's requests are made as the user of its
 * TaskConfig, the default user if it names none.
 */
class IMasterSA : public virtual IMaster
{
public:
    virtual ~IMasterSA() {}

    /**
     * Add a user, or replace its name and update key. The user's session keys are set again.
     *
     * @param user the user number, the same as on the outstation
     * @param userName the user's name
     * @param key the user's update key, as configured on the outstation
     */
    virtual void AddUser(User user, const std::string& userName, const UpdateKey& key) = 0;

    /**
     * @return the user's session key status, as the master sees it
     */
    virtual KeyStatus GetSessionKeyStatus(User user) = 0;

    /**
     * Send a user status change (g120v10), made and certified by the authority, to add, delete or change a user on
     * the outstation. It is a critical request: the config's user must have session keys to answer its challenge.
     */
    virtual void ChangeUserStatus(const UserStatusChange& change, const TaskConfig& config = TaskConfig::Default()) = 0;
};

} // namespace opendnp3

#endif
