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
#ifndef OPENDNP3_USERSTATUSCHANGETASK_H
#define OPENDNP3_USERSTATUSCHANGETASK_H

#include "master/IMasterTask.h"
#include "master/TaskPriority.h"

#include "opendnp3/secauth/UserStatusChange.h"

namespace opendnp3
{

/**
 * Sends a user status change (g120v10). It is a critical request, which the outstation challenges; it answers an
 * accepted change with a null response.
 */
class UserStatusChangeTask final : public IMasterTask
{
public:
    UserStatusChangeTask(const std::shared_ptr<TaskContext>& context,
                         IMasterApplication& application,
                         const Timestamp& startTimeout,
                         const Logger& logger,
                         UserStatusChange change,
                         const TaskConfig& config);

    char const* Name() const override
    {
        return "user status change";
    }

    int Priority() const override
    {
        return priority::USER_STATUS_CHANGE;
    }

    bool BlocksLowerPriority() const override
    {
        return false;
    }

    bool IsRecurring() const override
    {
        return false;
    }

    bool BuildRequest(APDURequest& request, uint8_t seq) override;

private:
    MasterTaskType GetTaskType() const override
    {
        return MasterTaskType::USER_TASK;
    }

    ResponseResult ProcessResponse(const APDUResponseHeader& response, const ser4cpp::rseq_t& objects) override;

    const UserStatusChange change;
};

} // namespace opendnp3

#endif
