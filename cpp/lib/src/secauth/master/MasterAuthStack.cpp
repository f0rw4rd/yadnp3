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
#include "secauth/master/MasterAuthStack.h"

namespace opendnp3
{

namespace
{
MasterStackConfig ToMasterStackConfig(const MasterAuthStackConfig& config)
{
    MasterStackConfig ret;
    ret.master = config.master;
    ret.link = config.link;
    return ret;
}
} // namespace

MasterAuthStack::MasterAuthStack(const Logger& logger,
                                 const std::shared_ptr<exe4cpp::StrandExecutor>& executor,
                                 const std::shared_ptr<ISOEHandler>& SOEHandler,
                                 const std::shared_ptr<IMasterApplicationSA>& application,
                                 const std::shared_ptr<IMasterScheduler>& scheduler,
                                 const std::shared_ptr<IOHandler>& iohandler,
                                 const std::shared_ptr<IResourceManager>& manager,
                                 const MasterAuthStackConfig& config)
    : MasterStack(logger,
                  executor,
                  application,
                  iohandler,
                  manager,
                  ToMasterStackConfig(config),
                  [&](const std::shared_ptr<ILowerLayer>& lower) -> std::shared_ptr<MContext> {
                      return MAuthContext::Create(Addresses(config.link.LocalAddr, config.link.RemoteAddr), logger,
                                                  executor, lower, SOEHandler, application, scheduler, config.master,
                                                  config.auth);
                  }),
      authContext(std::static_pointer_cast<MAuthContext>(this->mcontext))
{
}

std::shared_ptr<MasterAuthStack> MasterAuthStack::Create(const Logger& logger,
                                                         const std::shared_ptr<exe4cpp::StrandExecutor>& executor,
                                                         const std::shared_ptr<ISOEHandler>& SOEHandler,
                                                         const std::shared_ptr<IMasterApplicationSA>& application,
                                                         const std::shared_ptr<IMasterScheduler>& scheduler,
                                                         const std::shared_ptr<IOHandler>& iohandler,
                                                         const std::shared_ptr<IResourceManager>& manager,
                                                         const MasterAuthStackConfig& config)
{
    auto ret = std::make_shared<MasterAuthStack>(logger, executor, SOEHandler, application, scheduler, iohandler,
                                                 manager, config);
    ret->tstack.link->SetRouter(*ret);
    return ret;
}

void MasterAuthStack::AddUser(User user, const std::string& userName, const UpdateKey& key)
{
    auto add = [context = this->authContext, user, userName, key]() { context->AddUser(user, userName, key); };
    this->executor->post(add);
}

KeyStatus MasterAuthStack::GetSessionKeyStatus(User user)
{
    auto get = [context = this->authContext, user]() { return context->GetSessionKeyStatus(user); };
    return this->executor->return_from<KeyStatus>(get);
}

} // namespace opendnp3
