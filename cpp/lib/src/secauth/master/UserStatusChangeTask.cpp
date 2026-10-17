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
#include "secauth/master/UserStatusChangeTask.h"

#include "gen/objects/Group120.h"
#include "logging/LogMacros.h"

#include "opendnp3/logging/LogLevels.h"

namespace opendnp3
{

namespace
{
ser4cpp::rseq_t View(const std::vector<uint8_t>& bytes)
{
    return ser4cpp::rseq_t(bytes.data(), bytes.size());
}
} // namespace

UserStatusChangeTask::UserStatusChangeTask(const std::shared_ptr<TaskContext>& context,
                                           IMasterApplication& application,
                                           const Timestamp& startTimeout,
                                           const Logger& logger,
                                           UserStatusChange change,
                                           const TaskConfig& config)
    : IMasterTask(context, application, TaskBehavior::SingleExecutionNoRetry(startTimeout), logger, config),
      change(std::move(change))
{
}

bool UserStatusChangeTask::BuildRequest(APDURequest& request, uint8_t seq)
{
    request.ConfigureHeader(FunctionCode::AUTH_REQUEST, seq);

    const ser4cpp::rseq_t name(reinterpret_cast<const uint8_t*>(this->change.userName.data()),
                               this->change.userName.size());
    const Group120Var10 object(this->change.keyChangeMethod, this->change.userOperation,
                               this->change.statusChangeSeqNum, this->change.userRole, this->change.userRoleExpDays,
                               name, View(this->change.userPublicKey), View(this->change.certificationData));
    return request.GetWriter().WriteFreeFormat(object);
}

IMasterTask::ResponseResult UserStatusChangeTask::ProcessResponse(const APDUResponseHeader& response,
                                                                  const ser4cpp::rseq_t& objects)
{
    if (response.function != FunctionCode::RESPONSE)
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "User status change not answered by a response");
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    return ValidateNullResponse(response, objects) ? ResponseResult::OK_FINAL : ResponseResult::ERROR_BAD_RESPONSE;
}

} // namespace opendnp3
