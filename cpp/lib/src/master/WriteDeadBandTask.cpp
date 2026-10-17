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
#include "WriteDeadBandTask.h"

#include "app/APDURequest.h"

#include "opendnp3/gen/FunctionCode.h"

#include <ser4cpp/serialization/LittleEndian.h>

namespace opendnp3
{

WriteDeadBandTask::WriteDeadBandTask(const std::shared_ptr<TaskContext>& context,
                                     IMasterApplication& app,
                                     std::vector<Indexed<AnalogInputDeadband>> deadBands,
                                     FileOperationCallbackT callback,
                                     const Logger& logger,
                                     const TaskConfig& config)
    : IMasterTask(context, app, TaskBehavior::SingleExecutionNoRetry(), logger, config),
      deadBands(std::move(deadBands)),
      callback(std::move(callback))
{
}

bool WriteDeadBandTask::BuildRequest(APDURequest& request, uint8_t seq)
{
    request.SetControl(AppControlField(true, true, false, false, seq));
    request.SetFunction(FunctionCode::WRITE);

    auto writer = request.GetWriter();

    // Group34Var3 = single-precision float, 4 bytes per value
    // We use qualifier 0x28 (UINT16_CNT_UINT16_INDEX): 2-byte count + (2-byte index + 4-byte value) per item
    const size_t headerSize = 3;  // group(1) + variation(1) + qualifier(1)
    const size_t countSize = 2;   // 16-bit count
    const size_t perItem = 2 + 4; // 16-bit index + 4-byte float value
    const size_t totalNeeded = headerSize + countSize + deadBands.size() * perItem;

    if (writer.Remaining() < totalNeeded)
        return false;

    if (!writer.WriteHeader(GroupVariationID(34, 3), QualifierCode::UINT16_CNT_UINT16_INDEX))
        return false;

    // Write 16-bit count
    uint8_t countBuf[2];
    uint16_t count = static_cast<uint16_t>(deadBands.size());
    countBuf[0] = static_cast<uint8_t>(count & 0xFF);
    countBuf[1] = static_cast<uint8_t>((count >> 8) & 0xFF);
    writer.WriteRawBytes(countBuf, 2);

    // Write each (index, value) pair
    for (const auto& db : deadBands)
    {
        // 16-bit index
        uint8_t indexBuf[2];
        uint16_t idx = static_cast<uint16_t>(db.index);
        indexBuf[0] = static_cast<uint8_t>(idx & 0xFF);
        indexBuf[1] = static_cast<uint8_t>((idx >> 8) & 0xFF);
        writer.WriteRawBytes(indexBuf, 2);

        // 4-byte float value, little-endian whatever the host's byte order
        uint8_t valBuf[4];
        ser4cpp::wseq_t valSeq(valBuf, 4);
        ser4cpp::SingleFloat::write_to(valSeq, static_cast<float>(db.value.value));
        writer.WriteRawBytes(valBuf, 4);
    }

    return true;
}

IMasterTask::ResponseResult WriteDeadBandTask::ProcessResponse(const APDUResponseHeader& header,
                                                               const ser4cpp::rseq_t& objects)
{
    return ValidateNullResponse(header, objects) ? ResponseResult::OK_FINAL : ResponseResult::ERROR_BAD_RESPONSE;
}

void WriteDeadBandTask::OnTaskComplete(TaskCompletion result, Timestamp /*now*/)
{
    if (callback)
    {
        FileOperationResult res;
        res.summary = result;
        res.statusCode = (result == TaskCompletion::SUCCESS) ? FileStatus::SUCCESS : FileStatus::FATAL;
        callback(res);
    }
}

} // namespace opendnp3
