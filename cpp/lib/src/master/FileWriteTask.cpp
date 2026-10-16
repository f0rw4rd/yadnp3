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
#include "FileWriteTask.h"

#include "app/APDURequest.h"
#include "logging/LogMacros.h"

#include "opendnp3/gen/FunctionCode.h"
#include "opendnp3/logging/LogLevels.h"

#include <ser4cpp/serialization/LittleEndian.h>

#include <algorithm>
#include <cstring>

namespace opendnp3
{

FileWriteTask::FileWriteTask(const std::shared_ptr<TaskContext>& context,
                             IMasterApplication& app,
                             const std::string& filename,
                             std::vector<uint8_t> data,
                             FilePermissions permissions,
                             FileMode mode,
                             uint32_t authKey,
                             FileWriteCallbackT callback,
                             const Logger& logger,
                             const TaskConfig& config)
    : IMasterTask(context, app, TaskBehavior::SingleExecutionNoRetry(), logger, config),
      filename(filename),
      fileData(std::move(data)),
      permissions(permissions),
      fileMode(mode),
      authKey(authKey),
      callback(std::move(callback)),
      state(State::OPEN_FILE),
      fileHandle(0),
      currentBlockNum(0),
      maxBlockSize(2032),
      lastStatus(FileStatus::SUCCESS),
      requestId(0)
{
}

void FileWriteTask::Initialize()
{
    state = State::OPEN_FILE;
    fileHandle = 0;
    currentBlockNum = 0;
    maxBlockSize = 2032;
    lastStatus = FileStatus::SUCCESS;
}

bool FileWriteTask::BuildRequest(APDURequest& request, uint8_t seq)
{
    request.SetControl(AppControlField(true, true, false, false, seq));

    switch (state)
    {
    case State::OPEN_FILE:
        request.SetFunction(FunctionCode::OPEN_FILE);
        return WriteGroup70Var3Open(request);

    case State::WRITE_BLOCK:
        request.SetFunction(FunctionCode::WRITE);
        return WriteGroup70Var5Block(request);

    case State::CLOSE_FILE:
        request.SetFunction(FunctionCode::CLOSE_FILE);
        return WriteGroup70Var4Close(request);
    }

    return false;
}

bool FileWriteTask::WriteGroup70Var3Open(APDURequest& request)
{
    const uint16_t fileNameLen = static_cast<uint16_t>(filename.size());
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

    ser4cpp::UInt16::write_to(wseq, permissions.ToRaw());
    ser4cpp::UInt32::write_to(wseq, authKey);                                // auth_key
    ser4cpp::UInt32::write_to(wseq, static_cast<uint32_t>(fileData.size())); // file_size
    ser4cpp::UInt16::write_to(wseq, static_cast<uint16_t>(fileMode));        // mode
    ser4cpp::UInt16::write_to(wseq, maxBlockSize);
    ser4cpp::UInt16::write_to(wseq, requestId);

    writer.WriteRawBytes(buf, 26);
    writer.WriteRawBytes(reinterpret_cast<const uint8_t*>(filename.data()), fileNameLen);

    return true;
}

bool FileWriteTask::WriteGroup70Var5Block(APDURequest& request)
{
    // Calculate block boundaries
    const size_t offset = static_cast<size_t>(currentBlockNum) * maxBlockSize;
    if (offset >= fileData.size())
    {
        // All data has been written, close
        state = State::CLOSE_FILE;
        request.SetFunction(FunctionCode::CLOSE_FILE);
        return WriteGroup70Var4Close(request);
    }

    const size_t remaining = fileData.size() - offset;
    const size_t blockSize = std::min(remaining, static_cast<size_t>(maxBlockSize));
    const bool isLastBlock = (offset + blockSize >= fileData.size());

    const uint16_t dataLen = static_cast<uint16_t>(8 + blockSize);
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

    uint32_t blockField = currentBlockNum;
    if (isLastBlock)
    {
        blockField |= 0x80000000;
    }

    uint8_t hdr[8];
    auto wseq = ser4cpp::wseq_t(hdr, 8);
    ser4cpp::UInt32::write_to(wseq, fileHandle);
    ser4cpp::UInt32::write_to(wseq, blockField);
    writer.WriteRawBytes(hdr, 8);

    if (blockSize > 0)
    {
        writer.WriteRawBytes(fileData.data() + offset, blockSize);
    }

    return true;
}

bool FileWriteTask::WriteGroup70Var4Close(APDURequest& request)
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
    ser4cpp::UInt32::write_to(wseq, 0); // file_size
    ser4cpp::UInt16::write_to(wseq, 0); // max_block_size
    ser4cpp::UInt16::write_to(wseq, requestId);
    ser4cpp::UInt8::write_to(wseq, 0); // status_code = success
    writer.WriteRawBytes(buf, 13);

    return true;
}

bool FileWriteTask::ParseGroup70Var4(const ser4cpp::rseq_t& objects)
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
    data.advance(1); // count

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

    return true;
}

bool FileWriteTask::ParseGroup70Var6(const ser4cpp::rseq_t& objects)
{
    auto data = objects;

    if (data.length() < 3)
        return false;

    uint8_t group = data[0];
    uint8_t var = data[1];
    uint8_t qc = data[2];
    data.advance(3);

    if (group != 70 || qc != 0x5B)
        return false;

    // Could be var 4 (command status) or var 6 (transport status)
    if (var != 4 && var != 6)
        return false;

    if (data.length() < 1)
        return false;
    data.advance(1); // count

    if (data.length() < 2)
        return false;
    uint16_t dataLen = 0;
    ser4cpp::LittleEndian::read(data, dataLen);

    if (var == 6)
    {
        if (data.length() < dataLen || dataLen < 9)
            return false;

        uint32_t handle = 0;
        ser4cpp::LittleEndian::read(data, handle);

        uint32_t blockNum = 0;
        ser4cpp::LittleEndian::read(data, blockNum);

        lastStatus = static_cast<FileStatus>(data[0]);

        // the acknowledgement must be of the file opened, and of the block just written
        if (handle != fileHandle)
        {
            lastStatus = FileStatus::HANDLE_EXPIRED;
            return false;
        }
        if ((blockNum & 0x7FFFFFFF) != currentBlockNum)
        {
            lastStatus = FileStatus::BLOCK_SEQ;
            return false;
        }
    }
    else // var == 4
    {
        if (data.length() < dataLen || dataLen < 13)
            return false;

        uint32_t handle = 0;
        ser4cpp::LittleEndian::read(data, handle);

        uint32_t fSize = 0;
        ser4cpp::LittleEndian::read(data, fSize);

        uint16_t mbs = 0;
        ser4cpp::LittleEndian::read(data, mbs);

        uint16_t rId = 0;
        ser4cpp::LittleEndian::read(data, rId);

        lastStatus = static_cast<FileStatus>(data[0]);
    }

    return true;
}

IMasterTask::ResponseResult FileWriteTask::ProcessResponse(const APDUResponseHeader& header,
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

        if (maxBlockSize == 0)
            maxBlockSize = 2032;

        // Cap to fit within a default APDU (2048) minus overhead (16 bytes for
        // APDU header, Group70Var5 header, count, length prefix, handle, block)
        if (maxBlockSize > 2032)
            maxBlockSize = 2032;

        state = State::WRITE_BLOCK;
        currentBlockNum = 0;
        return ResponseResult::OK_REPEAT;
    }

    case State::WRITE_BLOCK: {
        // Parse the write acknowledgment
        if (!ParseGroup70Var6(objects))
        {
            // a block not acknowledged fails the write
            if (lastStatus == FileStatus::SUCCESS)
            {
                lastStatus = FileStatus::FATAL;
            }
            state = State::CLOSE_FILE;
            return ResponseResult::OK_REPEAT;
        }

        if (lastStatus != FileStatus::SUCCESS)
        {
            state = State::CLOSE_FILE;
            return ResponseResult::OK_REPEAT;
        }

        // Advance to next block
        currentBlockNum++;

        const size_t offset = static_cast<size_t>(currentBlockNum) * maxBlockSize;
        if (offset >= fileData.size())
        {
            // All blocks written, close
            state = State::CLOSE_FILE;
            return ResponseResult::OK_REPEAT;
        }

        return ResponseResult::OK_REPEAT;
    }

    case State::CLOSE_FILE: {
        return ResponseResult::OK_FINAL;
    }
    }

    return ResponseResult::ERROR_BAD_RESPONSE;
}

void FileWriteTask::OnTaskComplete(TaskCompletion result, Timestamp /*now*/)
{
    if (callback)
    {
        FileWriteResult res;
        res.summary = result;
        res.statusCode = lastStatus;
        callback(res);
    }
}

} // namespace opendnp3
