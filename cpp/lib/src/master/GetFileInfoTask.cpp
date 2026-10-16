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
#include "GetFileInfoTask.h"

#include "app/APDURequest.h"
#include "logging/LogMacros.h"

#include "opendnp3/gen/FunctionCode.h"
#include "opendnp3/logging/LogLevels.h"

#include <ser4cpp/serialization/LittleEndian.h>

#include <algorithm>
#include <cstring>

namespace opendnp3
{

GetFileInfoTask::GetFileInfoTask(const std::shared_ptr<TaskContext>& context,
                                 IMasterApplication& app,
                                 const std::string& filename,
                                 FileInfoCallbackT callback,
                                 const Logger& logger,
                                 const TaskConfig& config)
    : IMasterTask(context, app, TaskBehavior::SingleExecutionNoRetry(), logger, config),
      filename(filename),
      callback(std::move(callback)),
      lastStatus(FileStatus::SUCCESS)
{
}

bool GetFileInfoTask::BuildRequest(APDURequest& request, uint8_t seq)
{
    request.SetControl(AppControlField(true, true, false, false, seq));
    request.SetFunction(FunctionCode::GET_FILE_INFO);

    // Group70Var7: file descriptor naming the file, the other fields zero
    //   file_name_offset(u16), file_name_length(u16), file_type(u16), file_size(u32),
    //   time_of_creation(6 bytes), permissions(u16), request_id(u16), file_name(variable)
    const uint16_t fixedFieldsLen = 20;
    if (filename.size() > 0xFFFFu - fixedFieldsLen)
        return false;
    const uint16_t fileNameLen = static_cast<uint16_t>(filename.size());
    const uint16_t dataLen = fixedFieldsLen + fileNameLen;
    const size_t totalLen = 3 + 1 + 2 + dataLen;

    auto writer = request.GetWriter();
    if (writer.Remaining() < totalLen)
        return false;

    if (!writer.WriteHeader(GroupVariationID(70, 7), QualifierCode::UINT8_CNT_UINT16_FREE_FORMAT))
        return false;

    uint8_t countBuf[1] = {1};
    writer.WriteRawBytes(countBuf, 1);

    uint8_t lenBuf[2];
    lenBuf[0] = static_cast<uint8_t>(dataLen & 0xFF);
    lenBuf[1] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
    writer.WriteRawBytes(lenBuf, 2);

    uint8_t buf[20] = {0};
    auto wseq = ser4cpp::wseq_t(buf, 20);
    ser4cpp::UInt16::write_to(wseq, fixedFieldsLen); // file_name_offset
    ser4cpp::UInt16::write_to(wseq, fileNameLen);    // file_name_length
    writer.WriteRawBytes(buf, 20);

    writer.WriteRawBytes(reinterpret_cast<const uint8_t*>(filename.data()), fileNameLen);

    return true;
}

bool GetFileInfoTask::ParseGroup70Var7(const ser4cpp::rseq_t& objects)
{
    // Group70Var7 - File Descriptor
    // Wire format: file_name_offset(u16), file_name_length(u16),
    //   file_type(u16), file_size(u32), time_of_creation(6 bytes),
    //   permissions(u16), request_id(u16), file_name(variable)
    auto data = objects;

    if (data.length() < 3)
        return false;

    uint8_t group = data[0];
    uint8_t var = data[1];
    uint8_t qc = data[2];
    data.advance(3);

    if (group != 70 || var != 7 || qc != 0x5B)
    {
        // Could be a Group70Var4 error response
        if (group == 70 && var == 4 && qc == 0x5B)
        {
            if (data.length() < 1)
                return false;
            uint8_t count = data[0];
            data.advance(1);
            if (count < 1 || data.length() < 2)
                return false;
            uint16_t dLen = 0;
            ser4cpp::LittleEndian::read(data, dLen);
            if (data.length() < dLen || dLen < 13)
                return false;

            data.advance(4); // fileHandle
            data.advance(4); // fileSize
            data.advance(2); // maxBlockSize
            data.advance(2); // requestId
            lastStatus = static_cast<FileStatus>(data[0]);
            return true;
        }
        return false;
    }

    if (data.length() < 1)
        return false;
    uint8_t count = data[0];
    data.advance(1);

    if (count < 1)
        return false;

    if (data.length() < 2)
        return false;
    uint16_t dataLen = 0;
    ser4cpp::LittleEndian::read(data, dataLen);

    if (data.length() < dataLen || dataLen < 20)
        return false;

    uint16_t fileNameOffset = 0;
    uint16_t fileNameLength = 0;
    uint16_t fileType = 0;
    uint32_t fileSize = 0;

    ser4cpp::LittleEndian::read(data, fileNameOffset);
    ser4cpp::LittleEndian::read(data, fileNameLength);
    ser4cpp::LittleEndian::read(data, fileType);
    ser4cpp::LittleEndian::read(data, fileSize);

    // time_of_creation: 6 bytes
    uint64_t timeOfCreation = 0;
    for (int j = 0; j < 6; ++j)
    {
        timeOfCreation |= static_cast<uint64_t>(data[0]) << (j * 8);
        data.advance(1);
    }

    uint16_t permissions = 0;
    uint16_t requestId = 0;
    ser4cpp::LittleEndian::read(data, permissions);
    ser4cpp::LittleEndian::read(data, requestId);

    // Extract filename
    const auto nameLen = std::min(static_cast<size_t>(fileNameLength), data.length());
    std::string name;
    if (nameLen > 0)
    {
        name.assign(reinterpret_cast<const char*>(static_cast<const uint8_t*>(data)), nameLen);
    }

    result.fileName = name;
    result.type = static_cast<FileType>(fileType);
    result.size = fileSize;
    result.timeOfCreation = timeOfCreation;
    result.permissions = FilePermissions::FromRaw(permissions);
    result.requestId = requestId;
    lastStatus = FileStatus::SUCCESS;

    return true;
}

IMasterTask::ResponseResult GetFileInfoTask::ProcessResponse(const APDUResponseHeader& header,
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

    if (!ParseGroup70Var7(objects))
    {
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    return ResponseResult::OK_FINAL;
}

void GetFileInfoTask::OnTaskComplete(TaskCompletion result, Timestamp /*now*/)
{
    if (callback)
    {
        FileInfoResult res;
        res.summary = result;
        res.statusCode = lastStatus;
        res.info = this->result;
        callback(res);
    }
}

} // namespace opendnp3
