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
#ifndef OPENDNP3_MASTERAUTHCONTEXT_H
#define OPENDNP3_MASTERAUTHCONTEXT_H

#include "crypto/CryptoProvider.h"
#include "master/MasterContext.h"
#include "secauth/AuthSizes.h"
#include "secauth/SessionStore.h"
#include "secauth/master/MasterUserDatabase.h"
#include "secauth/master/SessionKeyTask.h"

#include "opendnp3/secauth/IMasterApplicationSA.h"
#include "opendnp3/secauth/MasterAuthSettings.h"
#include "opendnp3/secauth/UserStatusChange.h"

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace opendnp3
{

/**
 * A master with Secure Authentication (IEEE 1815-2012, SAv5): it keeps session keys for each of its users, and
 * answers the outstation's challenges of its critical requests.
 */
class MAuthContext final : public MContext
{
public:
    static std::shared_ptr<MAuthContext> Create(const Addresses& addresses,
                                                const Logger& logger,
                                                const std::shared_ptr<exe4cpp::IExecutor>& executor,
                                                std::shared_ptr<ILowerLayer> lower,
                                                const std::shared_ptr<ISOEHandler>& SOEHandler,
                                                const std::shared_ptr<IMasterApplicationSA>& application,
                                                std::shared_ptr<IMasterScheduler> scheduler,
                                                const MasterParams& params,
                                                const MasterAuthSettings& settings);

    bool OnLowerLayerUp() override;

    bool OnLowerLayerDown() override;

    void OnParsedHeader(const ser4cpp::rseq_t& apdu,
                        const APDUResponseHeader& header,
                        const ser4cpp::rseq_t& objects) override;

    void RecordLastRequest(const ser4cpp::rseq_t& apdu) override;

    /// adds the user, or replaces its name and update key; the user's session keys are set again
    void AddUser(User user, const std::string& userName, const UpdateKey& key);

    /// the user's session key status, as the master sees it
    KeyStatus GetSessionKeyStatus(User user);

    /// sends a user status change (g120v10) from the authority
    void ChangeUserStatus(const UserStatusChange& change, const TaskConfig& config);

private:
    MAuthContext(const Addresses& addresses,
                 const Logger& logger,
                 const std::shared_ptr<exe4cpp::IExecutor>& executor,
                 std::shared_ptr<ILowerLayer> lower,
                 const std::shared_ptr<ISOEHandler>& SOEHandler,
                 const std::shared_ptr<IMasterApplicationSA>& application,
                 std::shared_ptr<IMasterScheduler> scheduler,
                 const MasterParams& params,
                 const MasterAuthSettings& settings);

    void OnReceiveAuthResponse(const ser4cpp::rseq_t& apdu,
                               const APDUResponseHeader& header,
                               const ser4cpp::rseq_t& objects);

    void OnAuthChallenge(const ser4cpp::rseq_t& apdu, const APDUResponseHeader& header, const ser4cpp::rseq_t& objects);

    void OnAuthError(const APDUResponseHeader& header, const ser4cpp::rseq_t& objects);

    enum class Failure
    {
        AUTHENTICATION,
        AUTHORIZATION,
        NO_USER
    };

    // the outstation answered the active task's request with a failure: the task is complete
    void FailActiveTask(Failure failure);

    // starts the user's session key task, or runs it again now
    void DemandSessionKeys(uint16_t userNum);

    Timestamp Now() const
    {
        return Timestamp(this->executor->get_time());
    }

    const MasterAuthSettings settings;
    const std::shared_ptr<IMasterApplicationSA> applicationSA;
    CryptoProvider crypto;
    MasterUserDatabase users;
    SessionStore sessions;
    std::map<uint16_t, std::shared_ptr<SessionKeyTask>> sessionKeyTasks;

    // the last request sent, which a challenge covers: a copy, the transmit buffer is reused by confirms
    std::vector<uint8_t> lastRequest;
    std::array<uint8_t, AuthSizes::MAX_MASTER_CHALLENGE_REPLY_FRAG_SIZE> challengeReplyBuffer;
};

} // namespace opendnp3

#endif
