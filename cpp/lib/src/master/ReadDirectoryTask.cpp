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
#include "ReadDirectoryTask.h"

#include "app/APDURequest.h"
#include "logging/LogMacros.h"

#include "opendnp3/gen/FunctionCode.h"
#include "opendnp3/logging/LogLevels.h"

#include <ser4cpp/serialization/LittleEndian.h>

#include <algorithm>
#include <cstring>

namespace opendnp3
{

ReadDirectoryTask::ReadDirectoryTask(const std::shared_ptr<TaskContext>& context,
                                     IMasterApplication& app,
                                     const std::string& directoryPath,
                                     DirectoryReadCallbackT callback,
                                     const Logger& logger,
                                     const TaskConfig& config)
    : IMasterTask(context, app, TaskBehavior::SingleExecutionNoRetry(), logger, config),
      directoryPath(directoryPath),
      callback(std::move(callback)),
      state(State::OPEN_FILE),
      fileHandle(0),
      expectedBlockNum(0),
      maxBlockSize(2048),
      lastStatus(FileStatus::SUCCESS),
      lastBlockReceived(false),
      requestId(0)
{
}

void ReadDirectoryTask::Initialize()
{
    state = State::OPEN_FILE;
    fileHandle = 0;
    expectedBlockNum = 0;
    maxBlockSize = 2048;
    lastStatus = FileStatus::SUCCESS;
    rawData.clear();
    entries.clear();
    lastBlockReceived = false;
}

bool ReadDirectoryTask::BuildRequest(APDURequest& request, uint8_t seq)
{
    request.SetControl(AppControlField(true, true, false, false, seq));

    switch (state)
    {
    case State::OPEN_FILE:
        request.SetFunction(FunctionCode::OPEN_FILE);
        return WriteGroup70Var3(request);

    case State::READ_BLOCK:
        request.SetFunction(FunctionCode::READ);
        return WriteGroup70Var5Read(request);

    case State::CLOSE_FILE:
        request.SetFunction(FunctionCode::CLOSE_FILE);
        return WriteGroup70Var4Close(request);
    }

    return false;
}

bool ReadDirectoryTask::WriteGroup70Var3(APDURequest& request)
{
    const uint16_t fileNameLen = static_cast<uint16_t>(directoryPath.size());
    const uint16_t fixedFieldsLen = 26;
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

    uint8_t buf[26];
    auto wseq = ser4cpp::wseq_t(buf, 26);

    const uint16_t nameOffset = fixedFieldsLen;
    ser4cpp::UInt16::write_to(wseq, nameOffset);
    ser4cpp::UInt16::write_to(wseq, fileNameLen);

    // time_of_creation: 6 bytes of zeros
    for (int i = 0; i < 6; ++i)
    {
        ser4cpp::UInt8::write_to(wseq, 0);
    }

    ser4cpp::UInt16::write_to(wseq, 0x01FF);                                // permissions
    ser4cpp::UInt32::write_to(wseq, 0);                                     // auth_key
    ser4cpp::UInt32::write_to(wseq, 0);                                     // file_size
    ser4cpp::UInt16::write_to(wseq, static_cast<uint16_t>(FileMode::READ)); // mode
    ser4cpp::UInt16::write_to(wseq, maxBlockSize);
    ser4cpp::UInt16::write_to(wseq, requestId);

    writer.WriteRawBytes(buf, 26);
    writer.WriteRawBytes(reinterpret_cast<const uint8_t*>(directoryPath.data()), fileNameLen);

    return true;
}

bool ReadDirectoryTask::WriteGroup70Var5Read(APDURequest& request)
{
    const uint16_t dataLen = 8;
    const size_t totalLen = 3 + 1 + 2 + dataLen;

    auto writer = request.GetWriter();
    if (writer.Remaining() < totalLen)
        return false;

    if (!writer.WriteHeader(GroupVariationID(70, 5), QualifierCode::UINT8_CNT_UINT16_FREE_FORMAT))
        return false;

    uint8_t countBuf[1] = {1};
    writer.WriteRawBytes(countBuf, 1);

    uint8_t lenBuf[2];
    lenBuf[0] = static_cast<uint8_t>(dataLen & 0xFF);
    lenBuf[1] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
    writer.WriteRawBytes(lenBuf, 2);

    uint8_t buf[8];
    auto wseq = ser4cpp::wseq_t(buf, 8);
    ser4cpp::UInt32::write_to(wseq, fileHandle);
    ser4cpp::UInt32::write_to(wseq, expectedBlockNum);
    writer.WriteRawBytes(buf, 8);

    return true;
}

bool ReadDirectoryTask::WriteGroup70Var4Close(APDURequest& request)
{
    const uint16_t dataLen = 13;
    const size_t totalLen = 3 + 1 + 2 + dataLen;

    auto writer = request.GetWriter();
    if (writer.Remaining() < totalLen)
        return false;

    if (!writer.WriteHeader(GroupVariationID(70, 4), QualifierCode::UINT8_CNT_UINT16_FREE_FORMAT))
        return false;

    uint8_t countBuf[1] = {1};
    writer.WriteRawBytes(countBuf, 1);

    uint8_t lenBuf[2];
    lenBuf[0] = static_cast<uint8_t>(dataLen & 0xFF);
    lenBuf[1] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
    writer.WriteRawBytes(lenBuf, 2);

    uint8_t buf[13];
    auto wseq = ser4cpp::wseq_t(buf, 13);
    ser4cpp::UInt32::write_to(wseq, fileHandle);
    ser4cpp::UInt32::write_to(wseq, 0);
    ser4cpp::UInt16::write_to(wseq, 0);
    ser4cpp::UInt16::write_to(wseq, requestId);
    ser4cpp::UInt8::write_to(wseq, 0);
    writer.WriteRawBytes(buf, 13);

    return true;
}

bool ReadDirectoryTask::ParseGroup70Var4(const ser4cpp::rseq_t& objects)
{
    auto data = objects;

    if (data.length() < 3)
        return false;

    uint8_t group = data[0];
    uint8_t var = data[1];
    uint8_t qc = data[2];
    data.advance(3);

    if (group != 70 || var != 4 || qc != 0x5B)
        return false;

    if (data.length() < 1)
        return false;
    data.advance(1);

    if (data.length() < 2)
        return false;
    uint16_t dataLen = 0;
    ser4cpp::LittleEndian::read(data, dataLen);

    if (data.length() < dataLen || dataLen < 13)
        return false;

    ser4cpp::LittleEndian::read(data, fileHandle);

    uint32_t fSize = 0;
    ser4cpp::LittleEndian::read(data, fSize);

    ser4cpp::LittleEndian::read(data, maxBlockSize);

    uint16_t rId = 0;
    ser4cpp::LittleEndian::read(data, rId);

    lastStatus = static_cast<FileStatus>(data[0]);
    data.advance(1);

    return true;
}

bool ReadDirectoryTask::ParseGroup70Var5(const ser4cpp::rseq_t& objects)
{
    auto data = objects;

    if (data.length() < 3)
        return false;

    uint8_t group = data[0];
    uint8_t var = data[1];
    uint8_t qc = data[2];
    data.advance(3);

    if (group != 70 || var != 5 || qc != 0x5B)
        return false;

    if (data.length() < 1)
        return false;
    data.advance(1);

    if (data.length() < 2)
        return false;
    uint16_t dataLen = 0;
    ser4cpp::LittleEndian::read(data, dataLen);

    if (data.length() < dataLen || dataLen < 8)
        return false;

    uint32_t handle = 0;
    ser4cpp::LittleEndian::read(data, handle);

    uint32_t blockNum = 0;
    ser4cpp::LittleEndian::read(data, blockNum);

    lastBlockReceived = (blockNum & 0x80000000) != 0;
    blockNum &= 0x7FFFFFFF;

    // the blocks must be of the directory opened, in order
    if (handle != fileHandle)
    {
        lastStatus = FileStatus::HANDLE_EXPIRED;
        return false;
    }
    if (blockNum != expectedBlockNum)
    {
        lastStatus = FileStatus::BLOCK_SEQ;
        return false;
    }

    const size_t payloadLen = dataLen - 8;
    if (payloadLen > 0 && data.length() >= payloadLen)
    {
        rawData.insert(rawData.end(), static_cast<const uint8_t*>(data),
                       static_cast<const uint8_t*>(data) + payloadLen);
    }

    expectedBlockNum = blockNum + 1;
    return true;
}

bool ReadDirectoryTask::ParseGroup70Var6(const ser4cpp::rseq_t& objects)
{
    auto data = objects;

    if (data.length() < 3)
        return false;

    uint8_t group = data[0];
    uint8_t var = data[1];
    uint8_t qc = data[2];
    data.advance(3);

    if (group != 70 || var != 6 || qc != 0x5B)
        return false;

    if (data.length() < 1)
        return false;
    data.advance(1);

    if (data.length() < 2)
        return false;
    uint16_t dataLen = 0;
    ser4cpp::LittleEndian::read(data, dataLen);

    if (data.length() < dataLen || dataLen < 9)
        return false;

    uint32_t handle = 0;
    ser4cpp::LittleEndian::read(data, handle);

    uint32_t blockNum = 0;
    ser4cpp::LittleEndian::read(data, blockNum);

    lastStatus = static_cast<FileStatus>(data[0]);
    data.advance(1);

    return true;
}

bool ReadDirectoryTask::ParseDirectoryEntries()
{
    // The raw data is a concatenation of Group70Var7 entries (without the
    // free-format wrapper -- just the entry payloads back-to-back).
    //
    // Each entry:
    //   file_name_offset(u16), file_name_size(u16), file_type(u16),
    //   file_size(u32), time_of_creation(6 bytes), permissions(u16),
    //   file_name(variable)
    //
    // The fixed part is 18 bytes (2+2+2+4+6+2), then filename.

    const uint8_t* ptr = rawData.data();
    size_t remaining = rawData.size();

    while (remaining >= 18)
    {
        uint16_t fileNameOffset = static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
        uint16_t fileNameSize = static_cast<uint16_t>(ptr[2]) | (static_cast<uint16_t>(ptr[3]) << 8);
        uint16_t fileType = static_cast<uint16_t>(ptr[4]) | (static_cast<uint16_t>(ptr[5]) << 8);
        uint32_t fileSize = static_cast<uint32_t>(ptr[6]) | (static_cast<uint32_t>(ptr[7]) << 8)
            | (static_cast<uint32_t>(ptr[8]) << 16) | (static_cast<uint32_t>(ptr[9]) << 24);

        uint64_t timeOfCreation = 0;
        for (int j = 0; j < 6; ++j)
        {
            timeOfCreation |= static_cast<uint64_t>(ptr[10 + j]) << (j * 8);
        }

        uint16_t permissions = static_cast<uint16_t>(ptr[16]) | (static_cast<uint16_t>(ptr[17]) << 8);

        // Total entry size = fileNameOffset + fileNameSize
        // (fileNameOffset is the offset from start of entry to filename)
        size_t entrySize = static_cast<size_t>(fileNameOffset) + static_cast<size_t>(fileNameSize);

        if (entrySize > remaining || entrySize < 18)
            break;

        std::string fileName;
        if (fileNameSize > 0 && fileNameOffset <= remaining)
        {
            size_t nameAvailable = std::min(static_cast<size_t>(fileNameSize), remaining - fileNameOffset);
            fileName.assign(reinterpret_cast<const char*>(ptr + fileNameOffset), nameAvailable);
        }

        FileInfo info;
        info.fileName = fileName;
        info.type = static_cast<FileType>(fileType);
        info.size = fileSize;
        info.timeOfCreation = timeOfCreation;
        info.permissions = FilePermissions::FromRaw(permissions);
        entries.push_back(info);

        ptr += entrySize;
        remaining -= entrySize;
    }

    return true;
}

IMasterTask::ResponseResult ReadDirectoryTask::ProcessResponse(const APDUResponseHeader& header,
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

    switch (state)
    {
    case State::OPEN_FILE: {
        if (!ParseGroup70Var4(objects))
        {
            return ResponseResult::ERROR_BAD_RESPONSE;
        }

        if (lastStatus != FileStatus::SUCCESS)
        {
            return ResponseResult::OK_FINAL;
        }

        state = State::READ_BLOCK;
        expectedBlockNum = 0;
        return ResponseResult::OK_REPEAT;
    }

    case State::READ_BLOCK: {
        if (objects.length() >= 3 && objects[0] == 70)
        {
            if (objects[1] == 5)
            {
                if (!ParseGroup70Var5(objects))
                {
                    if (lastStatus == FileStatus::SUCCESS)
                    {
                        lastStatus = FileStatus::FATAL;
                    }
                    state = State::CLOSE_FILE;
                    return ResponseResult::OK_REPEAT;
                }

                if (lastBlockReceived)
                {
                    state = State::CLOSE_FILE;
                    return ResponseResult::OK_REPEAT;
                }

                return ResponseResult::OK_REPEAT;
            }
            else if (objects[1] == 6)
            {
                ParseGroup70Var6(objects);
                state = State::CLOSE_FILE;
                return ResponseResult::OK_REPEAT;
            }
        }

        lastStatus = FileStatus::FATAL;
        state = State::CLOSE_FILE;
        return ResponseResult::OK_REPEAT;
    }

    case State::CLOSE_FILE: {
        return ResponseResult::OK_FINAL;
    }
    }

    return ResponseResult::ERROR_BAD_RESPONSE;
}

void ReadDirectoryTask::OnTaskComplete(TaskCompletion result, Timestamp /*now*/)
{
    if (callback)
    {
        // Parse directory entries from raw data if successful
        if (result == TaskCompletion::SUCCESS && lastStatus == FileStatus::SUCCESS && !rawData.empty())
        {
            ParseDirectoryEntries();
        }

        DirectoryReadResult res;
        res.summary = result;
        res.statusCode = lastStatus;
        res.entries = std::move(entries);
        callback(res);
    }
}

} // namespace opendnp3
