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

#include "gil_safe.h"

#include "opendnp3/outstation/ICommandHandler.h"
#include "opendnp3/outstation/IFileHandler.h"
#include "opendnp3/outstation/IOutstation.h"
#include "opendnp3/outstation/IOutstationApplication.h"

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace opendnp3;

// Trampoline for ICommandHandler
// All overrides are wrapped in try/catch so that Python exceptions on ASIO
// strand threads are safely discarded instead of causing std::terminate().
class PyCommandHandler : public ICommandHandler
{
public:
    using ICommandHandler::ICommandHandler;

    void Begin() override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(void, ICommandHandler, Begin);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Begin");
        }
        catch (const std::exception&)
        {
        }
    }

    void End() override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(void, ICommandHandler, End);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::End");
        }
        catch (const std::exception&)
        {
        }
    }

    CommandStatus Select(const ControlRelayOutputBlock& command, uint16_t index) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(CommandStatus, ICommandHandler, Select, command, index);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Select");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Operate(const ControlRelayOutputBlock& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Operate");
            if (override_fn)
                return override_fn(command, index, py::cast(handler, py::return_value_policy::reference), opType)
                    .cast<CommandStatus>();
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Operate");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Select(const AnalogOutputInt16& command, uint16_t index) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(CommandStatus, ICommandHandler, Select, command, index);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Select");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Operate(const AnalogOutputInt16& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Operate");
            if (override_fn)
                return override_fn(command, index, py::cast(handler, py::return_value_policy::reference), opType)
                    .cast<CommandStatus>();
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Operate");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Select(const AnalogOutputInt32& command, uint16_t index) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(CommandStatus, ICommandHandler, Select, command, index);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Select");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Operate(const AnalogOutputInt32& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Operate");
            if (override_fn)
                return override_fn(command, index, py::cast(handler, py::return_value_policy::reference), opType)
                    .cast<CommandStatus>();
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Operate");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Select(const AnalogOutputFloat32& command, uint16_t index) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(CommandStatus, ICommandHandler, Select, command, index);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Select");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Operate(const AnalogOutputFloat32& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Operate");
            if (override_fn)
                return override_fn(command, index, py::cast(handler, py::return_value_policy::reference), opType)
                    .cast<CommandStatus>();
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Operate");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Select(const AnalogOutputDouble64& command, uint16_t index) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(CommandStatus, ICommandHandler, Select, command, index);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Select");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }

    CommandStatus Operate(const AnalogOutputDouble64& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Operate");
            if (override_fn)
                return override_fn(command, index, py::cast(handler, py::return_value_policy::reference), opType)
                    .cast<CommandStatus>();
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyCommandHandler::Operate");
        }
        catch (const std::exception&)
        {
        }
        return CommandStatus::NOT_SUPPORTED;
    }
};

// Trampoline for IOutstationApplication
class PyOutstationApplication : public IOutstationApplication
{
public:
    using IOutstationApplication::IOutstationApplication;

    bool SupportsWriteAbsoluteTime() override
    {
        try
        {
            PYBIND11_OVERRIDE(bool, IOutstationApplication, SupportsWriteAbsoluteTime);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::SupportsWriteAbsoluteTime");
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    bool WriteAbsoluteTime(const UTCTimestamp& timestamp) override
    {
        try
        {
            PYBIND11_OVERRIDE(bool, IOutstationApplication, WriteAbsoluteTime, timestamp);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::WriteAbsoluteTime");
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    bool SupportsAssignClass() override
    {
        try
        {
            PYBIND11_OVERRIDE(bool, IOutstationApplication, SupportsAssignClass);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::SupportsAssignClass");
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    ApplicationIIN GetApplicationIIN() const override
    {
        try
        {
            PYBIND11_OVERRIDE(ApplicationIIN, IOutstationApplication, GetApplicationIIN);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::GetApplicationIIN");
        }
        catch (const std::exception&)
        {
        }
        return ApplicationIIN{};
    }

    RestartMode ColdRestartSupport() const override
    {
        try
        {
            PYBIND11_OVERRIDE(RestartMode, IOutstationApplication, ColdRestartSupport);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::ColdRestartSupport");
        }
        catch (const std::exception&)
        {
        }
        return RestartMode::UNSUPPORTED;
    }

    RestartMode WarmRestartSupport() const override
    {
        try
        {
            PYBIND11_OVERRIDE(RestartMode, IOutstationApplication, WarmRestartSupport);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::WarmRestartSupport");
        }
        catch (const std::exception&)
        {
        }
        return RestartMode::UNSUPPORTED;
    }

    uint16_t ColdRestart() override
    {
        try
        {
            PYBIND11_OVERRIDE(uint16_t, IOutstationApplication, ColdRestart);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::ColdRestart");
        }
        catch (const std::exception&)
        {
        }
        return 65535;
    }

    uint16_t WarmRestart() override
    {
        try
        {
            PYBIND11_OVERRIDE(uint16_t, IOutstationApplication, WarmRestart);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::WarmRestart");
        }
        catch (const std::exception&)
        {
        }
        return 65535;
    }

    DNPTime Now() override
    {
        try
        {
            PYBIND11_OVERRIDE(DNPTime, IOutstationApplication, Now);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::Now");
        }
        catch (const std::exception&)
        {
        }
        return DNPTime(0);
    }

    bool SupportsWriteTimeAndInterval() override
    {
        try
        {
            PYBIND11_OVERRIDE(bool, IOutstationApplication, SupportsWriteTimeAndInterval);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::SupportsWriteTimeAndInterval");
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    void RecordClassAssignment(AssignClassType type, PointClass clazz, uint16_t start, uint16_t stop) override
    {
        try
        {
            PYBIND11_OVERRIDE(void, IOutstationApplication, RecordClassAssignment, type, clazz, start, stop);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::RecordClassAssignment");
        }
        catch (const std::exception&)
        {
        }
    }

    void OnConfirmProcessed(bool is_unsolicited, uint32_t num_class1, uint32_t num_class2, uint32_t num_class3) override
    {
        try
        {
            PYBIND11_OVERRIDE(void, IOutstationApplication, OnConfirmProcessed, is_unsolicited, num_class1, num_class2,
                              num_class3);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyOutstationApplication::OnConfirmProcessed");
        }
        catch (const std::exception&)
        {
        }
    }
};

// Trampoline for IFileHandler
class PyFileHandler : public IFileHandler
{
public:
    using IFileHandler::IFileHandler;

    FileCommandResult GetFileInfo(const std::string& filename) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(FileCommandResult, IFileHandler, GetFileInfo, filename);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyFileHandler::GetFileInfo");
        }
        catch (const std::exception&)
        {
        }
        return FileCommandResult{FileStatus::FATAL};
    }

    FileOpenResult OpenFile(const std::string& filename,
                            uint32_t authKey,
                            FilePermissions permissions,
                            FileMode mode,
                            uint16_t maxBlockSize,
                            uint16_t requestId) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(FileOpenResult, IFileHandler, OpenFile, filename, authKey, permissions, mode,
                                   maxBlockSize, requestId);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyFileHandler::OpenFile");
        }
        catch (const std::exception&)
        {
        }
        return FileOpenResult{FileStatus::FATAL};
    }

    FileBlockResult ReadBlock(uint32_t fileHandle, uint32_t blockNum) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(FileBlockResult, IFileHandler, ReadBlock, fileHandle, blockNum);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyFileHandler::ReadBlock");
        }
        catch (const std::exception&)
        {
        }
        return FileBlockResult{FileStatus::FATAL};
    }

    FileStatus WriteBlock(
        uint32_t fileHandle, uint32_t blockNum, bool lastBlock, const uint8_t* data, size_t size) override
    {
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "WriteBlock");
            if (override_fn)
                return override_fn(fileHandle, blockNum, lastBlock,
                                   py::bytes(reinterpret_cast<const char*>(data), size))
                    .cast<FileStatus>();
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyFileHandler::WriteBlock");
        }
        catch (const std::exception&)
        {
        }
        return FileStatus::FATAL;
    }

    FileStatus CloseFile(uint32_t fileHandle, uint16_t requestId) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(FileStatus, IFileHandler, CloseFile, fileHandle, requestId);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyFileHandler::CloseFile");
        }
        catch (const std::exception&)
        {
        }
        return FileStatus::FATAL;
    }

    FileStatus DeleteFile(const std::string& filename) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(FileStatus, IFileHandler, DeleteFile, filename);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyFileHandler::DeleteFile");
        }
        catch (const std::exception&)
        {
        }
        return FileStatus::FATAL;
    }

    void AbortFile(uint32_t fileHandle) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(void, IFileHandler, AbortFile, fileHandle);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyFileHandler::AbortFile");
        }
        catch (const std::exception&)
        {
        }
    }

    FileAuthResult AuthenticateFile(const std::string& username, const std::string& password) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(FileAuthResult, IFileHandler, AuthenticateFile, username, password);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyFileHandler::AuthenticateFile");
        }
        catch (const std::exception&)
        {
        }
        return FileAuthResult{};
    }
};

void init_outstation(py::module_& m)
{
    // ICommandHandler
    py::class_<ICommandHandler, PyCommandHandler, std::shared_ptr<ICommandHandler>>(
        m, "ICommandHandler", "Callback interface for handling command requests at an outstation")
        .def(py::init<>())
        .def("Begin", &ICommandHandler::Begin)
        .def("End", &ICommandHandler::End);

    // IOutstationApplication
    py::class_<IOutstationApplication, PyOutstationApplication, std::shared_ptr<IOutstationApplication>>(
        m, "IOutstationApplication", "Outstation application callback interface")
        .def(py::init<>())
        .def("SupportsWriteAbsoluteTime", &IOutstationApplication::SupportsWriteAbsoluteTime)
        .def("WriteAbsoluteTime", &IOutstationApplication::WriteAbsoluteTime, py::arg("timestamp"))
        .def("SupportsAssignClass", &IOutstationApplication::SupportsAssignClass)
        .def("GetApplicationIIN", &IOutstationApplication::GetApplicationIIN)
        .def("ColdRestartSupport", &IOutstationApplication::ColdRestartSupport)
        .def("WarmRestartSupport", &IOutstationApplication::WarmRestartSupport)
        .def("ColdRestart", &IOutstationApplication::ColdRestart)
        .def("WarmRestart", &IOutstationApplication::WarmRestart)
        .def("Now", &IOutstationApplication::Now)
        .def("SupportsWriteTimeAndInterval", &IOutstationApplication::SupportsWriteTimeAndInterval)
        .def("RecordClassAssignment", &IOutstationApplication::RecordClassAssignment, py::arg("type"), py::arg("clazz"),
             py::arg("start"), py::arg("stop"))
        .def("OnConfirmProcessed", &IOutstationApplication::OnConfirmProcessed, py::arg("is_unsolicited"),
             py::arg("num_class1"), py::arg("num_class2"), py::arg("num_class3"));

    // IFileHandler
    py::class_<IFileHandler, PyFileHandler, std::shared_ptr<IFileHandler>>(
        m, "IFileHandler", "Callback interface for outstation file transfer operations")
        .def(py::init<>())
        .def("GetFileInfo", &IFileHandler::GetFileInfo, py::arg("filename"))
        .def("OpenFile", &IFileHandler::OpenFile, py::arg("filename"), py::arg("authKey"), py::arg("permissions"),
             py::arg("mode"), py::arg("maxBlockSize"), py::arg("requestId"))
        .def("ReadBlock", &IFileHandler::ReadBlock, py::arg("fileHandle"), py::arg("blockNum"))
        .def("CloseFile", &IFileHandler::CloseFile, py::arg("fileHandle"), py::arg("requestId"))
        .def("DeleteFile", &IFileHandler::DeleteFile, py::arg("filename"))
        .def("AbortFile", &IFileHandler::AbortFile, py::arg("fileHandle"))
        .def("AuthenticateFile", &IFileHandler::AuthenticateFile, py::arg("username"), py::arg("password"));

    // IOutstation
    py::class_<IOutstation, std::shared_ptr<IOutstation>>(m, "IOutstation", "Running outstation session")
        .def("Enable", &IOutstation::Enable, py::call_guard<py::gil_scoped_release>())
        .def("Disable", &IOutstation::Disable, py::call_guard<py::gil_scoped_release>())
        .def("Shutdown", &IOutstation::Shutdown, py::call_guard<py::gil_scoped_release>())
        .def("SetLogFilters", &IOutstation::SetLogFilters, py::arg("filters"))
        .def("SetRestartIIN", &IOutstation::SetRestartIIN, py::call_guard<py::gil_scoped_release>())
        .def("Apply", &IOutstation::Apply, py::arg("updates"), "Apply measurement updates to the outstation database",
             py::call_guard<py::gil_scoped_release>());
}
