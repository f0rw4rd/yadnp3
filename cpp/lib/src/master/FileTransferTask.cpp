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
#include "FileTransferTask.h"

#include "app/APDURequest.h"
#include "logging/LogMacros.h"

#include "opendnp3/gen/FunctionCode.h"
#include "opendnp3/logging/LogLevels.h"

#include <ser4cpp/serialization/LittleEndian.h>

#include <algorithm>
#include <cstring>

namespace opendnp3
{

FileTransferTask::FileTransferTask(const std::shared_ptr<TaskContext>& context,
                                   IMasterApplication& app,
                                   const std::string& filename,
                                   uint32_t authKey,
                                   FileReadCallbackT callback,
                                   const Logger& logger,
                                   const TaskConfig& config)
    : IMasterTask(context, app, TaskBehavior::SingleExecutionNoRetry(), logger, config),
      filename(filename),
      authKey(authKey),
      callback(std::move(callback)),
      state(State::OPEN_FILE),
      fileHandle(0),
      fileSize(0),
      expectedBlockNum(0),
      maxBlockSize(2048),
      lastStatus(FileStatus::SUCCESS),
      lastBlockReceived(false),
      requestId(0)
{
}

void FileTransferTask::Initialize()
{
    state = State::OPEN_FILE;
    fileHandle = 0;
    fileSize = 0;
    expectedBlockNum = 0;
    maxBlockSize = 2048;
    lastStatus = FileStatus::SUCCESS;
    fileData.clear();
    lastBlockReceived = false;
}

bool FileTransferTask::BuildRequest(APDURequest& request, uint8_t seq)
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

bool FileTransferTask::WriteGroup70Var3(APDURequest& request)
{
    // Group70Var3 wire format:
    //   file_name_offset(u16), file_name_length(u16),
    //   time_of_creation(6 bytes), permissions(u16),
    //   auth_key(u32), file_size(u32), mode(u16),
    //   max_block_size(u16), request_id(u16), file_name(variable)
    //
    // Wrapped in free-format: group(1) + var(1) + qc(1) + count(1) + length(2) + data

    const uint16_t fileNameLen = static_cast<uint16_t>(filename.size());
    const uint16_t fixedFieldsLen = 26; // 2+2+6+2+4+4+2+2+2
    const uint16_t dataLen = fixedFieldsLen + fileNameLen;
    const size_t totalLen = 3 + 1 + 2 + dataLen; // header(3) + count(1) + length(2) + data

    auto writer = request.GetWriter();
    if (writer.Remaining() < totalLen)
        return false;

    // Write object header: group=70, var=3, qualifier=0x5B
    if (!writer.WriteHeader(GroupVariationID(70, 3), QualifierCode::UINT8_CNT_UINT16_FREE_FORMAT))
        return false;

    // Count = 1
    uint8_t countBuf[1] = {1};
    writer.WriteRawBytes(countBuf, 1);

    // Object length (2 bytes LE)
    uint8_t lenBuf[2];
    lenBuf[0] = static_cast<uint8_t>(dataLen & 0xFF);
    lenBuf[1] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
    writer.WriteRawBytes(lenBuf, 2);

    // file_name_offset = fixed fields size = 26
    uint8_t buf[26];
    auto wseq = ser4cpp::wseq_t(buf, 26);

    const uint16_t nameOffset = fixedFieldsLen;
    ser4cpp::UInt16::write_to(wseq, nameOffset);  // file_name_offset
    ser4cpp::UInt16::write_to(wseq, fileNameLen); // file_name_length

    // time_of_creation: 6 bytes of zeros
    for (int i = 0; i < 6; ++i)
    {
        ser4cpp::UInt8::write_to(wseq, 0);
    }

    // permissions: 0x01FF (all permissions)
    ser4cpp::UInt16::write_to(wseq, 0x01FF);

    // auth_key
    ser4cpp::UInt32::write_to(wseq, authKey);

    // file_size: 0 (reading)
    ser4cpp::UInt32::write_to(wseq, 0);

    // mode: READ = 1
    ser4cpp::UInt16::write_to(wseq, static_cast<uint16_t>(FileMode::READ));

    // max_block_size
    ser4cpp::UInt16::write_to(wseq, maxBlockSize);

    // request_id
    ser4cpp::UInt16::write_to(wseq, requestId);

    writer.WriteRawBytes(buf, 26);

    // file_name
    writer.WriteRawBytes(reinterpret_cast<const uint8_t*>(filename.data()), fileNameLen);

    return true;
}

bool FileTransferTask::WriteGroup70Var5Read(APDURequest& request)
{
    // For reading, we send a READ (FC=0x01) with Group70Var5 containing
    // the file handle and the block number we want to read.
    // Wire format: file_handle(u32), block_number(u32)

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

bool FileTransferTask::WriteGroup70Var4Close(APDURequest& request)
{
    // CLOSE_FILE sends Group70Var4 with just the file handle
    // Wire format: file_handle(u32), file_size(u32), max_block_size(u16),
    //              request_id(u16), status_code(u8)

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

bool FileTransferTask::ParseGroup70Var4(const ser4cpp::rseq_t& objects)
{
    // Response contains free-format Group70Var4
    // Expected: header(3) + count(1) + per-object: length(2) + data(13+)
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
    uint8_t count = data[0];
    data.advance(1);

    if (count < 1)
        return false;

    if (data.length() < 2)
        return false;
    uint16_t dataLen = 0;
    ser4cpp::LittleEndian::read(data, dataLen);

    if (data.length() < dataLen || dataLen < 13)
        return false;

    ser4cpp::LittleEndian::read(data, fileHandle);

    ser4cpp::LittleEndian::read(data, fileSize);

    ser4cpp::LittleEndian::read(data, maxBlockSize);

    uint16_t rId = 0;
    ser4cpp::LittleEndian::read(data, rId);

    lastStatus = static_cast<FileStatus>(data[0]);
    data.advance(1);

    return true;
}

bool FileTransferTask::ParseGroup70Var5(const ser4cpp::rseq_t& objects)
{
    // Response contains free-format Group70Var5 (file transport)
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
    uint8_t count = data[0];
    data.advance(1);

    if (count < 1)
        return false;

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

    // the blocks must be of the file opened, in order, and no more than the file size it gave, if it gave one
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

    // Append file data
    const size_t payloadLen = dataLen - 8;
    if (fileSize != 0 && fileData.size() + payloadLen > fileSize)
    {
        lastStatus = FileStatus::BUFFER_OVERFLOW;
        return false;
    }
    if (payloadLen > 0 && data.length() >= payloadLen)
    {
        fileData.insert(fileData.end(), static_cast<const uint8_t*>(data),
                        static_cast<const uint8_t*>(data) + payloadLen);
    }

    expectedBlockNum = blockNum + 1;
    return true;
}

bool FileTransferTask::ParseGroup70Var6(const ser4cpp::rseq_t& objects)
{
    // Group70Var6 - file transport status (alternative response to read)
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
    uint8_t count = data[0];
    data.advance(1);

    if (count < 1)
        return false;

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

IMasterTask::ResponseResult FileTransferTask::ProcessResponse(const APDUResponseHeader& header,
                                                              const ser4cpp::rseq_t& objects)
{
    if (!ValidateSingleResponse(header))
    {
        return ResponseResult::ERROR_BAD_RESPONSE;
    }

    // Check IIN bits for function not supported
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
            // Open failed, no need to close
            return ResponseResult::OK_FINAL;
        }

        // Transition to reading blocks
        state = State::READ_BLOCK;
        expectedBlockNum = 0;
        return ResponseResult::OK_REPEAT;
    }

    case State::READ_BLOCK: {
        // The response could be Group70Var5 (data) or Group70Var6 (status/error)
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

        // Unexpected response, try to close
        lastStatus = FileStatus::FATAL;
        state = State::CLOSE_FILE;
        return ResponseResult::OK_REPEAT;
    }

    case State::CLOSE_FILE: {
        // We don't need to parse the close response strictly
        return ResponseResult::OK_FINAL;
    }
    }

    return ResponseResult::ERROR_BAD_RESPONSE;
}

void FileTransferTask::OnTaskComplete(TaskCompletion result, Timestamp /*now*/)
{
    if (callback)
    {
        FileReadResult res;
        res.summary = result;
        res.statusCode = lastStatus;
        if (result == TaskCompletion::SUCCESS && lastStatus == FileStatus::SUCCESS)
        {
            res.data = std::move(fileData);
        }
        callback(res);
    }
}

} // namespace opendnp3
