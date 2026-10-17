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
#include "secauth/master/MasterAuthContext.h"

#include "app/APDURequest.h"
#include "app/parsing/APDUParser.h"
#include "app/parsing/ObjectHeaderParser.h"
#include "gen/objects/Group120.h"
#include "logging/LogMacros.h"
#include "secauth/Crypto.h"
#include "secauth/HMACProvider.h"
#include "secauth/SingleObjectHandlers.h"
#include "secauth/master/UserStatusChangeTask.h"

#include "opendnp3/logging/LogLevels.h"

namespace opendnp3
{

std::shared_ptr<MAuthContext> MAuthContext::Create(const Addresses& addresses,
                                                   const Logger& logger,
                                                   const std::shared_ptr<exe4cpp::IExecutor>& executor,
                                                   std::shared_ptr<ILowerLayer> lower,
                                                   const std::shared_ptr<ISOEHandler>& SOEHandler,
                                                   const std::shared_ptr<IMasterApplicationSA>& application,
                                                   std::shared_ptr<IMasterScheduler> scheduler,
                                                   const MasterParams& params,
                                                   const MasterAuthSettings& settings)
{
    return std::shared_ptr<MAuthContext>(new MAuthContext(addresses, logger, executor, std::move(lower), SOEHandler,
                                                          application, std::move(scheduler), params, settings));
}

MAuthContext::MAuthContext(const Addresses& addresses,
                           const Logger& logger,
                           const std::shared_ptr<exe4cpp::IExecutor>& executor,
                           std::shared_ptr<ILowerLayer> lower,
                           const std::shared_ptr<ISOEHandler>& SOEHandler,
                           const std::shared_ptr<IMasterApplicationSA>& application,
                           std::shared_ptr<IMasterScheduler> scheduler,
                           const MasterParams& params,
                           const MasterAuthSettings& settings)
    : MContext(addresses, logger, executor, std::move(lower), SOEHandler, application, std::move(scheduler), params),
      settings(settings),
      applicationSA(application),
      sessions(settings.sessionKeyTimeout, settings.maxAuthMsgCount)
{
    challengeReplyBuffer.fill(0);
}

bool MAuthContext::OnLowerLayerUp()
{
    if (!MContext::OnLowerLayerUp())
    {
        return false;
    }

    for (const auto userNum : this->users.GetUserNums())
    {
        this->DemandSessionKeys(userNum);
    }

    return true;
}

bool MAuthContext::OnLowerLayerDown()
{
    if (!MContext::OnLowerLayerDown())
    {
        return false;
    }

    // the scheduler has dropped the session key tasks, and new keys are set on the next connection
    this->sessionKeyTasks.clear();
    this->sessions.Clear();
    this->lastRequest.clear();
    return true;
}

void MAuthContext::RecordLastRequest(const ser4cpp::rseq_t& apdu)
{
    const uint8_t* bytes = apdu;
    this->lastRequest.assign(bytes, bytes + apdu.length());
}

void MAuthContext::AddUser(User user, const std::string& userName, const UpdateKey& key)
{
    this->users.AddOrUpdate(user.GetId(), userName, key);

    // keys set with the old update key are no longer good
    this->sessions.Invalidate(user.GetId());

    if (this->isOnline)
    {
        this->DemandSessionKeys(user.GetId());
    }
}

KeyStatus MAuthContext::GetSessionKeyStatus(User user)
{
    return this->sessions.GetKeyStatus(user.GetId(), this->Now());
}

void MAuthContext::ChangeUserStatus(const UserStatusChange& change, const TaskConfig& config)
{
    const auto timeout = this->Now() + this->params.taskStartTimeout;
    this->ScheduleAdhocTask(std::make_shared<UserStatusChangeTask>(this->tasks.context, *this->application, timeout,
                                                                   this->logger, change, config));
}

void MAuthContext::DemandSessionKeys(uint16_t userNum)
{
    const auto iter = this->sessionKeyTasks.find(userNum);
    if (iter != this->sessionKeyTasks.end())
    {
        this->scheduler->Demand(iter->second);
        return;
    }

    auto task = std::make_shared<SessionKeyTask>(this->tasks.context, *this->application, this->params,
                                                 this->settings.sessionChangeInterval, this->logger, User(userNum),
                                                 this->crypto, this->users, this->sessions, this->executor);
    this->sessionKeyTasks[userNum] = task;
    this->ScheduleAdhocTask(task);
}

void MAuthContext::OnParsedHeader(const ser4cpp::rseq_t& apdu,
                                  const APDUResponseHeader& header,
                                  const ser4cpp::rseq_t& objects)
{
    if (header.function == FunctionCode::AUTH_RESPONSE)
    {
        this->OnReceiveAuthResponse(apdu, header, objects);
        return;
    }

    MContext::OnParsedHeader(apdu, header, objects);
}

void MAuthContext::OnReceiveAuthResponse(const ser4cpp::rseq_t& apdu,
                                         const APDUResponseHeader& header,
                                         const ser4cpp::rseq_t& objects)
{
    if (this->tstate != TaskState::WAIT_FOR_RESPONSE || !this->activeTask)
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Ignoring unexpected authentication response");
        return;
    }

    // a Secure Authentication task asked for it: its own answer
    if (this->activeTask->IsAuthTask())
    {
        this->ProcessResponse(header, objects);
        return;
    }

    // otherwise it is about the active task's request, and must have its sequence number
    if (header.control.SEQ != this->solSeq)
    {
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Ignoring authentication response with sequence %u, not %u",
                         header.control.SEQ, this->solSeq.Get());
        return;
    }

    if (!(header.control.FIR && header.control.FIN))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Ignoring authentication response without FIR and FIN");
        return;
    }

    auto gv = GroupVariation::UNKNOWN;
    if (!ObjectHeaderParser::ReadFirstGroupVariation(objects, gv))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Ignoring authentication response with a bad first header");
        return;
    }

    switch (gv)
    {
    case (GroupVariation::Group120Var1):
        this->OnAuthChallenge(apdu, header, objects);
        return;
    case (GroupVariation::Group120Var7):
        this->OnAuthError(header, objects);
        return;
    default:
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Ignoring authentication response with %s",
                         GroupVariationSpec::to_human_string(gv));
        return;
    }
}

void MAuthContext::OnAuthChallenge(const ser4cpp::rseq_t& apdu,
                                   const APDUResponseHeader& header,
                                   const ser4cpp::rseq_t& objects)
{
    ChallengeHandler handler;
    const auto result = APDUParser::Parse(objects, handler, this->logger);
    if (!(result == ParseResult::OK && handler.IsValid()))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Ignoring malformed challenge");
        return;
    }

    const auto& challenge = handler.value;
    if (!AuthSizes::ChallengeDataSizeWithinLimits(static_cast<uint32_t>(challenge.challengeData.length())))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Ignoring challenge with challenge data size outside of limits");
        return;
    }

    if (this->lastRequest.empty())
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Ignoring challenge with no request to answer it for");
        return;
    }

    HMACMode mode;
    if (!Crypto::TryGetHMACMode(challenge.hmacAlgo, mode))
    {
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Challenge asks for an unsupported HMAC algorithm: %s",
                         HMACTypeSpec::to_human_string(challenge.hmacAlgo));
        this->FailActiveTask(false);
        return;
    }

    const auto user = this->activeTask->GetUser();
    SessionKeysView keys;
    if (this->sessions.TryGetKeyView(user.GetId(), keys, this->Now()) != KeyStatus::OK)
    {
        FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Challenged, but no valid session keys for user %u",
                         user.GetId());
        this->FailActiveTask(false);
        this->DemandSessionKeys(user.GetId());
        return;
    }

    if (this->isSending)
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Ignoring challenge while transmitting");
        return;
    }

    HMACProvider hmac(this->crypto, mode);
    std::error_code ec;
    const auto hmacValue = hmac.Compute(
        keys.controlKey, {apdu, ser4cpp::rseq_t(this->lastRequest.data(), this->lastRequest.size())}, ec);
    if (ec)
    {
        FORMAT_LOG_BLOCK(this->logger, flags::ERR, "Error calculating HMAC: %s", ec.message().c_str());
        this->FailActiveTask(false);
        return;
    }

    APDURequest reply(ser4cpp::wseq_t(this->challengeReplyBuffer.data(), this->challengeReplyBuffer.size()));
    reply.ConfigureHeader(FunctionCode::AUTH_REQUEST, header.control.SEQ);
    if (!reply.GetWriter().WriteFreeFormat(Group120Var2(challenge.challengeSeqNum, user.GetId(), hmacValue)))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::ERR, "Unable to write challenge reply");
        this->FailActiveTask(false);
        return;
    }

    this->Transmit(reply.ToRSeq());

    // the reply takes another round trip
    this->StartResponseTimer();

    if (this->sessions.IncrementAuthCount(user.GetId(), this->Now()) != KeyStatus::OK)
    {
        // the keys are used up: change them
        this->DemandSessionKeys(user.GetId());
    }
}

void MAuthContext::OnAuthError(const APDUResponseHeader& header, const ser4cpp::rseq_t& objects)
{
    ErrorHandler handler;
    const auto result = APDUParser::Parse(objects, handler, this->logger);
    if (!(result == ParseResult::OK && handler.IsValid()))
    {
        SIMPLE_LOG_BLOCK(this->logger, flags::WARN, "Ignoring malformed authentication error");
        return;
    }

    const auto code = handler.value.errorCode;
    FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Outstation reports authentication error: %s",
                     AuthErrorCodeSpec::to_human_string(code));

    this->FailActiveTask(code == AuthErrorCode::AUTHORIZATION_FAILED);

    // the outstation doesn't take the session keys: set them again
    if (code == AuthErrorCode::AUTHENTICATION_FAILED)
    {
        this->sessions.Invalidate(handler.value.userNum);
        if (this->users.UserExists(handler.value.userNum))
        {
            this->DemandSessionKeys(handler.value.userNum);
        }
    }
}

void MAuthContext::FailActiveTask(bool authorization)
{
    // the outstation answered the request, so its sequence number is used
    this->responseTimer.cancel();
    this->solSeq.Increment();

    const auto now = this->Now();
    if (authorization)
    {
        this->activeTask->OnAuthorizationFailure(now);
    }
    else
    {
        this->activeTask->OnAuthenticationFailure(now);
    }

    this->CompleteActiveTask();
    this->tstate = TaskState::IDLE;
}

} // namespace opendnp3
