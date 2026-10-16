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

// Master fuzzer: starts tasks chosen by the first byte, then feeds responses, each [flags][len][len bytes]. With
// flag 0x01, the response's sequence number is set to that of the last request, so it's processed
#include "utils/MasterTestFixture.h"

#include <master/HeaderConversions.h>

#include <ser4cpp/util/HexConversions.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace opendnp3;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;

    const uint8_t setup = data[0];
    MasterParams params = NoStartupTasks();
    if (setup & 0x80)
    {
        params.startupIntegrityClassMask = ClassField::AllClasses();
        params.disableUnsolOnStartup = true;
        params.unsolClassMask = ClassField::AllEventClasses();
    }
    MasterTestFixture t(params);
    t.context->OnLowerLayerUp();

    switch (setup & 0x0F)
    {
    case 0:
        t.context->ReadFile("f", [](const FileReadResult&) {});
        break;
    case 1:
        t.context->ReadDirectory("/d", [](const DirectoryReadResult&) {});
        break;
    case 2:
        t.context->GetFileInfo("f", [](const FileInfoResult&) {});
        break;
    case 3:
        t.context->WriteFile("f", std::vector<uint8_t>(5000, 0x55), FilePermissions(), [](const FileWriteResult&) {});
        break;
    case 4:
        t.context->SelectAndOperate(CommandSet({WithIndex(ControlRelayOutputBlock(OperationType::LATCH_ON), 1),
                                                WithIndex(ControlRelayOutputBlock(OperationType::LATCH_OFF), 3)}),
                                    [](const ICommandTaskResult&) {}, TaskConfig::Default());
        break;
    case 5:
        t.context->DirectOperate(CommandSet({WithIndex(AnalogOutputDouble64(1.5), 2)}), [](const ICommandTaskResult&) {},
                                 TaskConfig::Default());
        break;
    case 6:
        t.context->AuthenticateFile("u", "p", [](const FileAuthResult_t&) {});
        break;
    case 7:
        t.context->DeleteFile("f", [](const FileOperationResult&) {});
        break;
    case 8:
        t.context->Restart(RestartType::COLD, [](const RestartOperationResult&) {});
        break;
    default:
        t.context->Scan(ConvertToLambda({Header::AllObjects(60, 1), Header::AllObjects(0, 254)}), t.meas,
                        TaskConfig::Default());
        break;
    }
    t.exe->run_many();

    uint8_t lastSeq = 0;
    size_t pos = 1;
    while (pos + 2 <= size)
    {
        const uint8_t flags = data[pos];
        const size_t len = data[pos + 1];
        pos += 2;
        const size_t n = std::min(len, size - pos);
        std::vector<uint8_t> apdu(data + pos, data + pos + n);
        pos += n;

        while (!t.lower->HasNoData())
        {
            const auto hex = t.lower->PopWriteAsHex();
            if (hex.size() >= 2)
                lastSeq = static_cast<uint8_t>(std::stoul(hex.substr(0, 2), nullptr, 16) & 0x0F);
            t.context->OnTxReady();
        }

        if ((flags & 0x01) && !apdu.empty())
            apdu[0] = static_cast<uint8_t>((apdu[0] & 0xF0) | lastSeq);

        t.context->OnReceive(Message(Addresses(1024, 1), ser4cpp::rseq_t(apdu.data(), apdu.size())));
        t.exe->run_many();
        if (flags & 0x02)
            t.exe->advance_to_next_timer() && t.exe->run_many();
        if (flags & 0x04)
        {
            t.context->OnLowerLayerDown();
            t.context->OnLowerLayerUp();
            t.exe->run_many();
        }
    }
    return 0;
}
