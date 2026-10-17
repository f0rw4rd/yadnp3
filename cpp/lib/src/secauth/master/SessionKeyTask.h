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
#ifndef OPENDNP3_SESSIONKEYTASK_H
#define OPENDNP3_SESSIONKEYTASK_H

#include "master/IMasterTask.h"
#include "master/TaskPriority.h"
#include "secauth/SessionKeys.h"
#include "secauth/SessionStore.h"
#include "secauth/master/KeyWrap.h"
#include "secauth/master/MasterUserDatabase.h"

#include "opendnp3/crypto/ICryptoProvider.h"
#include "opendnp3/master/MasterParams.h"

#include <exe4cpp/IExecutor.h>

#include <vector>

namespace opendnp3
{

/**
 * Sets a user's session keys: asks for the key status (g120v4, answered by g120v5), then sends new keys wrapped in the
 * user's update key (g120v6), which the outstation answers with a key status carrying an HMAC over the key change.
 */
class SessionKeyTask final : public IMasterTask
{
public:
    SessionKeyTask(const std::shared_ptr<TaskContext>& context,
                   IMasterApplication& application,
                   const MasterParams& params,
                   TimeDuration changeInterval,
                   const Logger& logger,
                   User user,
                   ICryptoProvider& crypto,
                   const MasterUserDatabase& users,
                   SessionStore& sessions,
                   std::shared_ptr<exe4cpp::IExecutor> executor);

    char const* Name() const override
    {
        return "set session keys";
    }

    int Priority() const override
    {
        return priority::SESSION_KEY;
    }

    bool BlocksLowerPriority() const override
    {
        return false;
    }

    bool IsRecurring() const override
    {
        return true;
    }

    bool IsAuthTask() const override
    {
        return true;
    }

    bool RetriesOnFailure() const override
    {
        return true;
    }

    bool BuildRequest(APDURequest& request, uint8_t seq) override;

private:
    enum class State : uint8_t
    {
        GetStatus,
        ChangeKey
    };

    MasterTaskType GetTaskType() const override
    {
        return MasterTaskType::SET_SESSION_KEYS;
    }

    void Initialize() override;

    ResponseResult ProcessResponse(const APDUResponseHeader& response, const ser4cpp::rseq_t& objects) override;

    ResponseResult OnStatusResponse(const ser4cpp::rseq_t& objects);

    ResponseResult OnChangeResponse(const ser4cpp::rseq_t& objects);

    ICryptoProvider* crypto;
    const MasterUserDatabase* users;
    SessionStore* sessions;
    const std::shared_ptr<exe4cpp::IExecutor> executor;

    State state = State::GetStatus;
    uint32_t keyChangeSeqNum = 0;
    SessionKeys keys;
    KeyWrapBuffer keyWrapBuffer;
    // the key change request as sent, which the outstation's HMAC covers
    std::vector<uint8_t> keyChangeRequest;
};

} // namespace opendnp3

#endif
