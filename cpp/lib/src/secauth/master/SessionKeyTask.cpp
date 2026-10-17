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
#include "secauth/master/SessionKeyTask.h"

#include "app/parsing/APDUParser.h"
#include "gen/objects/Group120.h"
#include "logging/LogMacros.h"
#include "secauth/AuthSizes.h"
#include "secauth/Crypto.h"
#include "secauth/HMACProvider.h"
#include "secauth/master/KeyStatusHandler.h"

#include "opendnp3/crypto/SecureCompare.h"
#include "opendnp3/logging/LogLevels.h"

#include <ser4cpp/serialization/LittleEndian.h>

namespace opendnp3
{

SessionKeyTask::SessionKeyTask(const std::shared_ptr<TaskContext>& context,
                               IMasterApplication& application,
                               const MasterParams& params,
                               TimeDuration changeInterval,
                               const Logger& logger,
                               User user,
                               ICryptoProvider& crypto,
                               const MasterUserDatabase& users,
                               SessionStore& sessions,
                               std::shared_ptr<exe4cpp::IExecutor> executor)
    : IMasterTask(context,
                  application,
                  TaskBehavior::ImmediatePeriodic(changeInterval, params.taskRetryPeriod, params.maxTaskRetryPeriod),
                  logger,
                  TaskConfig(TaskId::Undefined(), nullptr, user)),
      crypto(&crypto),
      users(&users),
      sessions(&sessions),
      executor(std::move(executor))
{
}

void SessionKeyTask::Initialize()
{
    this->state = State::GetStatus;
    this->keyChangeRequest.clear();
}

bool SessionKeyTask::BuildRequest(APDURequest& request, uint8_t seq)
{
    request.ConfigureHeader(FunctionCode::AUTH_REQUEST, seq);

    if (this->state == State::GetStatus)
    {
        Group120Var4 statusRequest;
        statusRequest.userNum = this->GetUser().GetId();
        return request.GetWriter().WriteSingleValue<ser4cpp::UInt8, Group120Var4>(QualifierCode::UINT8_CNT,
                                                                                   statusRequest);
    }

    const Group120Var6 change(this->keyChangeSeqNum, this->GetUser().GetId(), this->keyWrapBuffer.GetWrappedData());
    if (!request.GetWriter().WriteFreeFormat(change))
    {
        return false;
    }

    // the outstation's HMAC covers the key change message: keep a copy, the transmit buffer is reused
    const auto apdu = request.ToRSeq();
    const uint8_t* bytes = apdu;
    this->keyChangeRequest.assign(bytes, bytes + apdu.length());
    return true;
}

IMasterTask::ResponseResult SessionKeyTask::ProcessResponse(const APDUResponseHeader& response,
                                                            const ser4cpp::rseq_t& objects)
{
    if (response.function != FunctionCode::AUTH_RESPONSE)
    {
        SIMPLE_LOG_BLOCK(logger, flags::WARN, "Session key request not answered by an authentication response");
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    if (!ValidateSingleResponse(response))
    {
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    return (this->state == State::GetStatus) ? this->OnStatusResponse(objects) : this->OnChangeResponse(objects);
}

IMasterTask::ResponseResult SessionKeyTask::OnStatusResponse(const ser4cpp::rseq_t& objects)
{
    KeyStatusHandler handler(this->logger);
    if (IsFailure(APDUParser::Parse(objects, handler, this->logger)))
    {
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    Group120Var5 status;
    ser4cpp::rseq_t rawObject;
    if (!handler.GetStatus(status, rawObject))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Response did not contain a key status object");
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    if (status.userNum != this->GetUser().GetId())
    {
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Key status for user %u, not %u", status.userNum,
                         this->GetUser().GetId());
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    if (!AuthSizes::ChallengeDataSizeWithinLimits(static_cast<uint32_t>(status.challengeData.length())))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Key status challenge data size outside of limits");
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    UpdateKey updateKey;
    if (!this->users->GetUpdateKey(this->GetUser().GetId(), updateKey))
    {
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "No update key for user %u", this->GetUser().GetId());
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    const auto key = updateKey.GetView();
    if (status.keyWrapAlgo != key.algorithm)
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "The outstation's key wrap algorithm doesn't match the update key");
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    std::error_code ec;
    this->keys.DeriveFrom(*this->crypto, SessionKeySize(AuthSizes::MIN_SESSION_KEY_SIZE_BYTES), ec);
    if (ec)
    {
        FORMAT_LOG_BLOCK(this->logger, flags::ERR, "Unable to derive session keys: %s", ec.message().c_str());
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    if (!this->keyWrapBuffer.Wrap(this->crypto->GetAESKeyWrap(), ser4cpp::rseq_t(key.data.data, key.data.length),
                                  this->keys.GetView(), rawObject, this->logger))
    {
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    this->keyChangeSeqNum = status.keyChangeSeqNum;
    this->state = State::ChangeKey;
    return ResponseResult::OK_REPEAT;
}

IMasterTask::ResponseResult SessionKeyTask::OnChangeResponse(const ser4cpp::rseq_t& objects)
{
    KeyStatusHandler handler(this->logger);
    if (IsFailure(APDUParser::Parse(objects, handler, this->logger)))
    {
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    Group120Var5 status;
    ser4cpp::rseq_t rawObject;
    if (!handler.GetStatus(status, rawObject))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Response did not contain a key status object");
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    if (status.userNum != this->GetUser().GetId())
    {
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Key status for user %u, not %u", status.userNum,
                         this->GetUser().GetId());
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    HMACMode mode;
    if (!Crypto::TryGetHMACMode(status.hmacAlgo, mode))
    {
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Unsupported HMAC algorithm in key status: %s",
                         HMACTypeSpec::to_human_string(status.hmacAlgo));
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    HMACProvider hmac(*this->crypto, mode);
    std::error_code ec;
    const auto expected = hmac.Compute(this->keys.GetView().monitorKey,
                                       {ser4cpp::rseq_t(this->keyChangeRequest.data(), this->keyChangeRequest.size())},
                                       ec);
    if (ec)
    {
        FORMAT_LOG_BLOCK(this->logger, flags::ERR, "Error calculating HMAC: %s", ec.message().c_str());
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    if (!SecureEquals(expected, status.hmacValue))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Key status after the key change has a bad HMAC");
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    // a key status with a good HMAC but no OK status: the outstation didn't take the keys
    if (status.keyStatus != KeyStatus::OK)
    {
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Outstation reports key status %s after the key change",
                         KeyStatusSpec::to_human_string(status.keyStatus));
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    this->sessions->SetKeys(this->GetUser().GetId(), this->keys.GetView(), Timestamp(this->executor->get_time()));
    return ResponseResult::OK_FINAL;
}

} // namespace opendnp3
