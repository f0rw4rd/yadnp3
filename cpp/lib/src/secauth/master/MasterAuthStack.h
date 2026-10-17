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
#ifndef OPENDNP3_MASTERAUTHSTACK_H
#define OPENDNP3_MASTERAUTHSTACK_H

#include "master/MasterStack.h"
#include "secauth/master/MasterAuthContext.h"

#include "opendnp3/secauth/IMasterApplicationSA.h"
#include "opendnp3/secauth/IMasterSA.h"
#include "opendnp3/secauth/MasterAuthStackConfig.h"

namespace opendnp3
{

/**
 * A master stack whose context is a Secure Authentication master
 */
class MasterAuthStack final : public MasterStack, public IMasterSA
{
public:
    MasterAuthStack(const Logger& logger,
                    const std::shared_ptr<exe4cpp::StrandExecutor>& executor,
                    const std::shared_ptr<ISOEHandler>& SOEHandler,
                    const std::shared_ptr<IMasterApplicationSA>& application,
                    const std::shared_ptr<IMasterScheduler>& scheduler,
                    const std::shared_ptr<IOHandler>& iohandler,
                    const std::shared_ptr<IResourceManager>& manager,
                    const MasterAuthStackConfig& config);

    static std::shared_ptr<MasterAuthStack> Create(const Logger& logger,
                                                   const std::shared_ptr<exe4cpp::StrandExecutor>& executor,
                                                   const std::shared_ptr<ISOEHandler>& SOEHandler,
                                                   const std::shared_ptr<IMasterApplicationSA>& application,
                                                   const std::shared_ptr<IMasterScheduler>& scheduler,
                                                   const std::shared_ptr<IOHandler>& iohandler,
                                                   const std::shared_ptr<IResourceManager>& manager,
                                                   const MasterAuthStackConfig& config);

    // --------- Implement IMasterSA ---------

    void AddUser(User user, const std::string& userName, const UpdateKey& key) override;

    KeyStatus GetSessionKeyStatus(User user) override;

    void ChangeUserStatus(const UserStatusChange& change, const TaskConfig& config) override;

private:
    const std::shared_ptr<MAuthContext> authContext;
};

} // namespace opendnp3

#endif
