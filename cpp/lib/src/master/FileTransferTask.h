/*
 * Copyright 2013-2022 Step Function I/O, LLC
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
#ifndef OPENDNP3_FILETRANSFERTASK_H
#define OPENDNP3_FILETRANSFERTASK_H

#include "master/IMasterTask.h"
#include "master/TaskPriority.h"

#include "opendnp3/master/FileOperationResult.h"

#include <string>
#include <vector>

namespace opendnp3
{

/**
 * Multi-step master task for reading a file from an outstation.
 *
 * Protocol flow:
 *   1. OPEN_FILE (FC=0x19) with Group70Var3 -> receive Group70Var4 (command status)
 *   2. READ (FC=0x01) with Group70Var5 (block 0) -> receive Group70Var5 (file data)
 *      Repeat for each block until final block flag is set
 *   3. CLOSE_FILE (FC=0x1A) with Group70Var4 -> receive Group70Var4
 */
class FileTransferTask final : public IMasterTask
{
public:
    FileTransferTask(const std::shared_ptr<TaskContext>& context,
                     IMasterApplication& app,
                     const std::string& filename,
                     uint32_t authKey,
                     FileReadCallbackT callback,
                     const Logger& logger,
                     const TaskConfig& config);

    char const* Name() const override
    {
        return "File Read";
    }
    int Priority() const override
    {
        return priority::COMMAND;
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
    void Initialize() override;
    ResponseResult ProcessResponse(const APDUResponseHeader& header, const ser4cpp::rseq_t& objects) override;
    void OnTaskComplete(TaskCompletion result, Timestamp now) override;

    /// Write a Group70Var3 (file command) free-format object into the request
    bool WriteGroup70Var3(APDURequest& request);

    /// Write a Group70Var5 (file transport) request for reading block N
    bool WriteGroup70Var5Read(APDURequest& request);

    /// Write a Group70Var4 (file command status) for closing
    bool WriteGroup70Var4Close(APDURequest& request);

    /// Parse Group70Var4 (file command status) from response
    bool ParseGroup70Var4(const ser4cpp::rseq_t& objects);

    /// Parse Group70Var5 (file transport) from response
    bool ParseGroup70Var5(const ser4cpp::rseq_t& objects);

    /// Parse Group70Var6 (file transport status) from response
    bool ParseGroup70Var6(const ser4cpp::rseq_t& objects);

    enum class State
    {
        OPEN_FILE,
        READ_BLOCK,
        CLOSE_FILE
    };

    const std::string filename;
    const uint32_t authKey;
    FileReadCallbackT callback;

    State state;
    uint32_t fileHandle;
    uint32_t fileSize;
    uint32_t expectedBlockNum;
    uint16_t maxBlockSize;
    FileStatus lastStatus;
    std::vector<uint8_t> fileData;
    bool lastBlockReceived;
    uint16_t requestId;
};

} // namespace opendnp3

#endif
