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
#include "DeleteFileTask.h"

#include "app/APDURequest.h"

#include "opendnp3/gen/FunctionCode.h"

#include <ser4cpp/serialization/LittleEndian.h>

namespace opendnp3
{

DeleteFileTask::DeleteFileTask(const std::shared_ptr<TaskContext>& context,
                               IMasterApplication& app,
                               const std::string& filename,
                               FileOperationCallbackT callback,
                               const Logger& logger,
                               const TaskConfig& config)
    : IMasterTask(context, app, TaskBehavior::SingleExecutionNoRetry(), logger, config),
      filename(filename),
      callback(std::move(callback)),
      lastStatus(FileStatus::SUCCESS)
{
}

bool DeleteFileTask::BuildRequest(APDURequest& request, uint8_t seq)
{
    request.SetControl(AppControlField(true, true, false, false, seq));
    request.SetFunction(FunctionCode::DELETE_FILE);

    // Group70Var3: file command naming the file, the other fields zero
    //   file_name_offset(u16), file_name_length(u16), time_of_creation(6 bytes), permissions(u16),
    //   authentication_key(u32), file_size(u32), operational_mode(u16), maximum_block_size(u16),
    //   request_id(u16), file_name(variable)
    const uint16_t fixedFieldsLen = 26;
    if (filename.size() > 0xFFFFu - fixedFieldsLen)
        return false;
    const uint16_t fileNameLen = static_cast<uint16_t>(filename.size());
    const uint16_t dataLen = fixedFieldsLen + fileNameLen;
    const size_t totalLen = 3 + 1 + 2 + dataLen;

    auto writer = request.GetWriter();
    if (writer.Remaining() < totalLen)
        return false;

    if (!writer.WriteHeader(GroupVariationID(70, 3), QualifierCode::UINT8_CNT_UINT16_FREE_FORMAT))
        return false;

    uint8_t countBuf[1] = {1};
    writer.WriteRawBytes(countBuf, 1);

    uint8_t lenBuf[2];
    lenBuf[0] = static_cast<uint8_t>(dataLen & 0xFF);
    lenBuf[1] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
    writer.WriteRawBytes(lenBuf, 2);

    uint8_t buf[26] = {0};
    auto wseq = ser4cpp::wseq_t(buf, 26);
    ser4cpp::UInt16::write_to(wseq, fixedFieldsLen); // file_name_offset
    ser4cpp::UInt16::write_to(wseq, fileNameLen);    // file_name_length
    writer.WriteRawBytes(buf, 26);

    writer.WriteRawBytes(reinterpret_cast<const uint8_t*>(filename.data()), fileNameLen);

    return true;
}

IMasterTask::ResponseResult DeleteFileTask::ProcessResponse(const APDUResponseHeader& header,
                                                            const ser4cpp::rseq_t& objects)
{
    if (!ValidateSingleResponse(header))
    {
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    if (header.IIN.IsSet(IINBit::FUNC_NOT_SUPPORTED) || header.IIN.IsSet(IINBit::PARAM_ERROR))
    {
        lastStatus = FileStatus::NOT_EXIST;
        return ResponseResult::OK_FINAL;
    }

    // Try to parse Group70Var4 if present
    if (!objects.is_empty() && objects.length() >= 3 && objects[0] == 70 && objects[1] == 4)
    {
        auto data = objects;
        data.advance(3); // skip header

        if (data.length() >= 1)
        {
            data.advance(1); // skip count

            if (data.length() >= 2)
            {
                uint16_t dataLen = 0;
                ser4cpp::LittleEndian::read(data, dataLen);

                if (data.length() >= dataLen && dataLen >= 13)
                {
                    data.advance(4); // fileHandle
                    data.advance(4); // fileSize
                    data.advance(2); // maxBlockSize
                    data.advance(2); // requestId
                    lastStatus = static_cast<FileStatus>(data[0]);
                }
            }
        }
    }

    return ResponseResult::OK_FINAL;
}

void DeleteFileTask::OnTaskComplete(TaskCompletion result, Timestamp /*now*/)
{
    if (callback)
    {
        FileOperationResult res;
        res.summary = result;
        res.statusCode = lastStatus;
        callback(res);
    }
}

} // namespace opendnp3
