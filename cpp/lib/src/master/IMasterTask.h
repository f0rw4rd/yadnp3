/*
 * Copyright 2013-2022 Step Function I/O, LLC
 * Modified 2024-2026 f0rw4rd (experimental fork)
 *
 * Licensed to Green Energy Corp (www.greenenergycorp.com) and Step Function I/O
 * LLC (https://stepfunc.io) under one or more contributor license agreements.
 * See the NOTICE file distributed with this work for additional information
 * regarding copyright ownership. Green Energy Corp and Step Function I/O LLC license
 * this file to you under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License. You may obtain
 * a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef OPENDNP3_IMASTERTASK_H
#define OPENDNP3_IMASTERTASK_H

#include "app/APDUHeader.h"
#include "app/APDURequest.h"
#include "master/TaskBehavior.h"
#include "master/TaskContext.h"

#include "opendnp3/logging/Logger.h"
#include "opendnp3/master/IMasterApplication.h"
#include "opendnp3/master/TaskConfig.h"

#include <exe4cpp/IExecutor.h>
#include <exe4cpp/Typedefs.h>

namespace opendnp3
{

/**
 * A generic interface for defining master request/response style tasks
 */
class IMasterTask : private Uncopyable
{

public:
    enum class ResponseResult : uint8_t
    {
        /// The response was bad, the task has failed
        ERROR_BAD_RESPONSE,

        /// The response was good and the task is complete
        OK_FINAL,

        /// The response was good and the task should repeat the format, transmit, and await response sequence
        OK_REPEAT,

        /// The response was good and the task should continue executing. Restart the response timer, and increment
        /// expected SEQ#.
        OK_CONTINUE
    };

    IMasterTask(std::shared_ptr<TaskContext> context,
                IMasterApplication& app,
                TaskBehavior behavior,
                const Logger& logger,
                TaskConfig config);

    virtual ~IMasterTask();

    /**
     *
     * @return	the name of the task
     */
    virtual char const* Name() const = 0;

    /**
     * The task's priority. Lower numbers are higher priority.
     */
    virtual int Priority() const = 0;

    /**
     * Indicates if the task should be rescheduled (true) or discarded
     * after a single execution (false)
     */
    virtual bool IsRecurring() const = 0;

    /**
     * The time when this task can run again.
     */
    Timestamp ExpirationTime() const;

    /**
     * Helper to test if the task is expired
     */
    bool IsExpired(const Timestamp& now) const
    {
        return now >= this->ExpirationTime();
    }

    /**
     * The time when this task expires if it is unable to start
     */
    Timestamp StartExpirationTime() const;

    /**
     * Build a request APDU.
     *
     * Return false if some kind of internal error prevents the task for formatting the request.
     */
    virtual bool BuildRequest(APDURequest& request, uint8_t seq) = 0;

    /**
     * Whether this task expects a response from the outstation.
     * Tasks using no-response function codes (e.g. DIRECT_OPERATE_NR, IMMED_FREEZE_NR)
     * should return false so the master does not start a response timer.
     */
    virtual bool ExpectsResponse() const
    {
        return true;
    }

    /**
     * Handler for responses
     */
    ResponseResult OnResponse(const APDUResponseHeader& response, const ser4cpp::rseq_t& objects, Timestamp now);

    /**
     * Called when a response times out
     */
    void OnResponseTimeout(Timestamp now);

    /**
     * Called when the layer closes while the task is executing.
     */
    void OnLowerLayerClose(Timestamp now);

    /**
     * The start timeout expired before the task could be run
     */
    void OnStartTimeout(Timestamp now);

    /**
     * Called when the master is unable to format the request associated with the task
     */
    void OnMessageFormatError(Timestamp now);

    /**
     * Called when the task's user is not defined on the master (SA only)
     */
    void OnNoUser(Timestamp now);

    /**
     * Called when the outstation fails the request's authentication, or reports an error for it (SA only)
     */
    void OnAuthenticationFailure(Timestamp now);

    /**
     * Called when the outstation reports the request's user is not authorized for it (SA only)
     */
    void OnAuthorizationFailure(Timestamp now);

    /**
     * Called when the task first starts, before the first request is formatted
     */
    void OnStart();

    /**
     * The Secure Authentication user the task's requests are made as
     */
    User GetUser() const
    {
        return this->config.user;
    }

    /**
     * A Secure Authentication task runs without a session, and receives its authentication responses itself
     */
    virtual bool IsAuthTask() const
    {
        return false;
    }

    /**
     * Called once the request of a task that expects no response has been sent
     */
    void OnRequestSent(Timestamp now);

    /**
     * Set the expiration time to minimum. The scheduler must also be informed
     */
    void SetMinExpiration();

    /**
     * Check if the task is blocked from executing by another task
     */
    bool IsBlocked() const
    {
        return this->context->IsBlocked(*this);
    }

protected:
    // called during OnStart() to initialize any state for a new run
    virtual void Initialize() {}

    virtual ResponseResult ProcessResponse(const APDUResponseHeader& response, const ser4cpp::rseq_t& objects) = 0;

    void CompleteTask(TaskCompletion result, Timestamp now);

    virtual void OnTaskComplete(TaskCompletion result, Timestamp now) {}

    virtual bool IsEnabled() const
    {
        return true;
    }

    virtual MasterTaskType GetTaskType() const = 0;

    const std::shared_ptr<TaskContext> context;
    IMasterApplication* const application;
    Logger logger;

    // Validation helpers for various behaviors to avoid deep inheritance
    bool ValidateSingleResponse(const APDUResponseHeader& header);
    bool ValidateNullResponse(const APDUResponseHeader& header, const ser4cpp::rseq_t& objects);
    bool ValidateNoObjects(const ser4cpp::rseq_t& objects);
    bool ValidateInternalIndications(const APDUResponseHeader& header);

private:
    /**
     * Allows tasks to enter a blocking mode where lower priority
     * tasks cannot run until this task completes
     */
    virtual bool BlocksLowerPriority() const = 0;

    /**
     * A task that must not stop on a failure (the session keys) retries with the back-off of a timeout instead of
     * being disabled
     */
    virtual bool RetriesOnFailure() const
    {
        return false;
    }

    IMasterTask();

    TaskConfig config;
    TaskBehavior behavior;
};

} // namespace opendnp3

#endif
