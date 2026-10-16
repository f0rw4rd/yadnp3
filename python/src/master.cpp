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

#include "opendnp3/app/AnalogCommandEvent.h"
#include "opendnp3/app/BinaryCommandEvent.h"
#include "opendnp3/app/DeviceAttributes.h"
#include "opendnp3/app/Indexed.h"
#include "opendnp3/app/MeasurementTypes.h"
#include "opendnp3/app/OctetString.h"
#include "opendnp3/app/parsing/ICollection.h"
#include "opendnp3/master/CommandPointResult.h"
#include "opendnp3/master/CommandResultCallbackT.h"
#include "opendnp3/master/CommandSet.h"
#include "opendnp3/master/FileOperationResult.h"
#include "opendnp3/master/HeaderInfo.h"
#include "opendnp3/master/HeaderTypes.h"
#include "opendnp3/master/ICommandProcessor.h"
#include "opendnp3/master/ICommandTaskResult.h"
#include "opendnp3/master/IMaster.h"
#include "opendnp3/master/IMasterApplication.h"
#include "opendnp3/master/IMasterOperations.h"
#include "opendnp3/master/IMasterScan.h"
#include "opendnp3/master/ISOEHandler.h"
#include "opendnp3/master/ResponseInfo.h"
#include "opendnp3/master/TaskConfig.h"
#include "opendnp3/master/TaskInfo.h"

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <vector>

namespace py = pybind11;
using namespace opendnp3;

// GIL-safe shared_ptr for py::function.
// Ensures the GIL is held when the py::function destructor runs,
// preventing crashes when C++ threads destroy callbacks during shutdown.
inline std::shared_ptr<py::function> make_safe_callback(py::function&& func)
{
    return std::shared_ptr<py::function>(new py::function(std::move(func)), [](py::function* ptr) {
        if (Py_IsInitialized())
        {
            py::gil_scoped_acquire gil;
            delete ptr;
        }
    });
}

// Helper: convert ICollection<T> to std::vector<T>
template<class T> std::vector<T> ToVector(const ICollection<T>& coll)
{
    std::vector<T> items;
    items.reserve(coll.Count());
    coll.ForeachItem([&](const T& v) { items.push_back(v); });
    return items;
}

// Trampoline for ISOEHandler
// Each Process() override converts the ICollection to a std::vector before
// dispatching to the Python override so that Python receives a plain list.
// All overrides are wrapped in try/catch so that Python exceptions on ASIO
// strand threads are safely discarded instead of causing std::terminate().
class PySOEHandler : public ISOEHandler
{
public:
    using ISOEHandler::ISOEHandler;

    void BeginFragment(const ResponseInfo& info) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(void, ISOEHandler, BeginFragment, info);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::BeginFragment");
        }
        catch (const std::exception&)
        {
        }
    }

    void EndFragment(const ResponseInfo& info) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(void, ISOEHandler, EndFragment, info);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::EndFragment");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<Binary>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<DoubleBitBinary>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<Analog>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<Counter>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<FrozenCounter>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<BinaryOutputStatus>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<AnalogOutputStatus>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<OctetString>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<TimeAndInterval>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<BinaryCommandEvent>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<AnalogCommandEvent>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<Indexed<AnalogInputDeadband>>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void Process(const HeaderInfo& info, const ICollection<DNPTime>& values) override
    {
        auto items = ToVector(values);
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "Process");
            if (override_fn)
                override_fn(info, items);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::Process");
        }
        catch (const std::exception&)
        {
        }
    }

    void OnRawAPDU(const ResponseInfo& info, const uint8_t* data, size_t length) override
    {
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "OnRawAPDU");
            if (override_fn)
                override_fn(info, py::bytes(reinterpret_cast<const char*>(data), length));
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::OnRawAPDU");
        }
        catch (const std::exception&)
        {
        }
    }

    void OnDeviceAttribute(const HeaderInfo& info,
                           uint8_t set,
                           uint8_t variation,
                           const DeviceAttributeValue& value) override
    {
        try
        {
            py::gil_scoped_acquire gil;
            py::function override_fn = py::get_override(this, "OnDeviceAttribute");
            if (override_fn)
                override_fn(info, set, variation, value);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PySOEHandler::OnDeviceAttribute");
        }
        catch (const std::exception&)
        {
        }
    }
};

// Trampoline for IMasterApplication
class PyMasterApplication : public IMasterApplication
{
public:
    using IMasterApplication::IMasterApplication;

    void OnReceiveIIN(const IINField& iin) override
    {
        try
        {
            PYBIND11_OVERRIDE(void, IMasterApplication, OnReceiveIIN, iin);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyMasterApplication::OnReceiveIIN");
        }
        catch (const std::exception&)
        {
        }
    }

    void OnTaskStart(MasterTaskType type, TaskId id) override
    {
        try
        {
            PYBIND11_OVERRIDE(void, IMasterApplication, OnTaskStart, type, id);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyMasterApplication::OnTaskStart");
        }
        catch (const std::exception&)
        {
        }
    }

    void OnTaskComplete(const TaskInfo& info) override
    {
        try
        {
            PYBIND11_OVERRIDE(void, IMasterApplication, OnTaskComplete, info);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyMasterApplication::OnTaskComplete");
        }
        catch (const std::exception&)
        {
        }
    }

    void OnOpen() override
    {
        try
        {
            PYBIND11_OVERRIDE(void, IMasterApplication, OnOpen);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyMasterApplication::OnOpen");
        }
        catch (const std::exception&)
        {
        }
    }

    void OnClose() override
    {
        try
        {
            PYBIND11_OVERRIDE(void, IMasterApplication, OnClose);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyMasterApplication::OnClose");
        }
        catch (const std::exception&)
        {
        }
    }

    bool AssignClassDuringStartup() override
    {
        try
        {
            PYBIND11_OVERRIDE(bool, IMasterApplication, AssignClassDuringStartup);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyMasterApplication::AssignClassDuringStartup");
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    UTCTimestamp Now() override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(UTCTimestamp, IMasterApplication, Now);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyMasterApplication::Now");
        }
        catch (const std::exception&)
        {
        }
        return UTCTimestamp(0);
    }
};

// Snapshot of ICommandTaskResult data, safe to use after the callback returns
struct CommandTaskResultSnapshot
{
    TaskCompletion summary;
    std::vector<CommandPointResult> results;

    static CommandTaskResultSnapshot From(const ICommandTaskResult& src)
    {
        CommandTaskResultSnapshot snap;
        snap.summary = src.summary;
        src.ForeachItem([&](const CommandPointResult& v) { snap.results.push_back(v); });
        return snap;
    }
};

void init_master(py::module_& m)
{
    // ResponseInfo
    py::class_<ResponseInfo>(m, "ResponseInfo", "Info about a response or unsolicited response")
        .def_readonly("unsolicited", &ResponseInfo::unsolicited)
        .def_readonly("fir", &ResponseInfo::fir)
        .def_readonly("fin", &ResponseInfo::fin);

    // TaskConfig
    py::class_<TaskConfig>(m, "TaskConfig", "Configuration for a master task")
        .def_static("Default", &TaskConfig::Default);

    // ICommandTaskResult (snapshot copy, safe to use outside callbacks)
    py::class_<CommandTaskResultSnapshot>(m, "ICommandTaskResult", "Result of a command task operation")
        .def_readonly("summary", &CommandTaskResultSnapshot::summary)
        .def(
            "to_list", [](const CommandTaskResultSnapshot& self) { return self.results; },
            "Get the results as a Python list");

    // ISOEHandler
    py::class_<ISOEHandler, PySOEHandler, std::shared_ptr<ISOEHandler>>(
        m, "ISOEHandler", "Sequence-of-events callback interface for receiving measurements from a master")
        .def(py::init<>())
        .def("BeginFragment", &ISOEHandler::BeginFragment, py::arg("info"))
        .def("EndFragment", &ISOEHandler::EndFragment, py::arg("info"))
        .def("OnDeviceAttribute", &ISOEHandler::OnDeviceAttribute, py::arg("info"), py::arg("set"),
             py::arg("variation"), py::arg("value"), "Called when a device attribute (Group 0) is received");

    // IMasterApplication
    py::class_<IMasterApplication, PyMasterApplication, std::shared_ptr<IMasterApplication>>(
        m, "IMasterApplication", "Master application callback interface")
        .def(py::init<>())
        .def("OnReceiveIIN", &IMasterApplication::OnReceiveIIN, py::arg("iin"))
        .def("OnTaskStart", &IMasterApplication::OnTaskStart, py::arg("type"), py::arg("id"))
        .def("OnTaskComplete", &IMasterApplication::OnTaskComplete, py::arg("info"))
        .def("OnOpen", &IMasterApplication::OnOpen)
        .def("OnClose", &IMasterApplication::OnClose)
        .def("AssignClassDuringStartup", &IMasterApplication::AssignClassDuringStartup);

    // IMasterScan
    py::class_<IMasterScan, std::shared_ptr<IMasterScan>>(m, "IMasterScan", "Handle for a running scan")
        .def("Demand", &IMasterScan::Demand, py::call_guard<py::gil_scoped_release>());

    // IMaster (inherits IStack + IMasterOperations)
    py::class_<IMaster, std::shared_ptr<IMaster>>(m, "IMaster", "Running master session")
        .def("Enable", &IMaster::Enable, py::call_guard<py::gil_scoped_release>())
        .def("Disable", &IMaster::Disable, py::call_guard<py::gil_scoped_release>())
        .def("Shutdown", &IMaster::Shutdown, py::call_guard<py::gil_scoped_release>())
        .def("SetLogFilters", &IMaster::SetLogFilters, py::arg("filters"))
        .def(
            "Scan",
            [](IMaster& self, const std::vector<Header>& headers, std::shared_ptr<ISOEHandler> soe,
               const TaskConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(soe));
                py::gil_scoped_release release;
                self.Scan(headers, safe_soe, config);
            },
            py::arg("headers"), py::arg("soe_handler"), py::arg("config") = TaskConfig::Default(),
            "Initiate a one-shot scan using custom headers")
        .def(
            "ScanClasses",
            [](IMaster& self, const ClassField& field, std::shared_ptr<ISOEHandler> soe, const TaskConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(soe));
                py::gil_scoped_release release;
                self.ScanClasses(field, safe_soe, config);
            },
            py::arg("field"), py::arg("soe_handler"), py::arg("config") = TaskConfig::Default(),
            "Initiate a one-shot class-based scan")
        .def(
            "AddClassScan",
            [](IMaster& self, const ClassField& field, TimeDuration period, std::shared_ptr<ISOEHandler> soe,
               const TaskConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(soe));
                py::gil_scoped_release release;
                return self.AddClassScan(field, period, safe_soe, config);
            },
            py::arg("field"), py::arg("period"), py::arg("soe_handler"), py::arg("config") = TaskConfig::Default(),
            "Add a recurring class scan")
        .def("Write", &IMaster::Write, py::arg("value"), py::arg("index"), py::arg("config") = TaskConfig::Default(),
             py::call_guard<py::gil_scoped_release>())
        .def("Restart", &IMaster::Restart, py::arg("op"), py::arg("callback"),
             py::arg("config") = TaskConfig::Default(), py::call_guard<py::gil_scoped_release>())
        .def("PerformFunction", &IMaster::PerformFunction, py::arg("name"), py::arg("func"), py::arg("headers"),
             py::arg("config") = TaskConfig::Default(), py::call_guard<py::gil_scoped_release>())
        .def("Freeze", &IMaster::Freeze, py::arg("type"), py::arg("headers"), py::arg("config") = TaskConfig::Default(),
             "Issue a freeze request to the outstation", py::call_guard<py::gil_scoped_release>())
        .def(
            "AddScan",
            [](IMaster& self, TimeDuration period, const std::vector<Header>& headers,
               std::shared_ptr<ISOEHandler> soe, const TaskConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(soe));
                py::gil_scoped_release release;
                return self.AddScan(period, headers, safe_soe, config);
            },
            py::arg("period"), py::arg("headers"), py::arg("soe_handler"), py::arg("config") = TaskConfig::Default(),
            "Add a recurring user-defined scan from a vector of headers")
        .def(
            "AddAllObjectsScan",
            [](IMaster& self, GroupVariationID gvId, TimeDuration period, std::shared_ptr<ISOEHandler> soe,
               const TaskConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(soe));
                py::gil_scoped_release release;
                return self.AddAllObjectsScan(gvId, period, safe_soe, config);
            },
            py::arg("gvId"), py::arg("period"), py::arg("soe_handler"), py::arg("config") = TaskConfig::Default(),
            "Add a recurring all-objects scan for a group/variation")
        .def(
            "AddRangeScan",
            [](IMaster& self, GroupVariationID gvId, uint16_t start, uint16_t stop, TimeDuration period,
               std::shared_ptr<ISOEHandler> soe, const TaskConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(soe));
                py::gil_scoped_release release;
                return self.AddRangeScan(gvId, start, stop, period, safe_soe, config);
            },
            py::arg("gvId"), py::arg("start"), py::arg("stop"), py::arg("period"), py::arg("soe_handler"),
            py::arg("config") = TaskConfig::Default(), "Add a recurring range-based scan")
        .def(
            "ScanAllObjects",
            [](IMaster& self, GroupVariationID gvId, std::shared_ptr<ISOEHandler> soe, const TaskConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(soe));
                py::gil_scoped_release release;
                self.ScanAllObjects(gvId, safe_soe, config);
            },
            py::arg("gvId"), py::arg("soe_handler"), py::arg("config") = TaskConfig::Default(),
            "Initiate a single all-objects scan for a group/variation")
        .def(
            "ScanRange",
            [](IMaster& self, GroupVariationID gvId, uint16_t start, uint16_t stop, std::shared_ptr<ISOEHandler> soe,
               const TaskConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(soe));
                py::gil_scoped_release release;
                self.ScanRange(gvId, start, stop, safe_soe, config);
            },
            py::arg("gvId"), py::arg("start"), py::arg("stop"), py::arg("soe_handler"),
            py::arg("config") = TaskConfig::Default(), "Initiate a single range-based scan")
        .def(
            "SelectAndOperate",
            [](IMaster& self, const ControlRelayOutputBlock& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.SelectAndOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Select and operate a CROB at the given index")
        .def(
            "SelectAndOperate",
            [](IMaster& self, const AnalogOutputInt16& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.SelectAndOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default())
        .def(
            "SelectAndOperate",
            [](IMaster& self, const AnalogOutputInt32& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.SelectAndOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default())
        .def(
            "SelectAndOperate",
            [](IMaster& self, const AnalogOutputFloat32& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.SelectAndOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default())
        .def(
            "SelectAndOperate",
            [](IMaster& self, const AnalogOutputDouble64& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.SelectAndOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default())
        .def(
            "DirectOperate",
            [](IMaster& self, const ControlRelayOutputBlock& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.DirectOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Direct operate a CROB at the given index")
        .def(
            "DirectOperate",
            [](IMaster& self, const AnalogOutputInt16& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.DirectOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default())
        .def(
            "DirectOperate",
            [](IMaster& self, const AnalogOutputInt32& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.DirectOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default())
        .def(
            "DirectOperate",
            [](IMaster& self, const AnalogOutputFloat32& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.DirectOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default())
        .def(
            "DirectOperate",
            [](IMaster& self, const AnalogOutputDouble64& command, uint16_t index, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const ICommandTaskResult& result) mutable {
                    auto snap = CommandTaskResultSnapshot::From(result);
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                self.DirectOperate(command, index, safe_cb, config);
            },
            py::arg("command"), py::arg("index"), py::arg("callback"), py::arg("config") = TaskConfig::Default())
        // ===== File Transfer Methods =====
        .def(
            "ReadFile",
            [](IMaster& self, const std::string& filename, py::function callback, const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileReadResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.ReadFile(filename, safe_cb, config);
            },
            py::arg("filename"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Read a file from the outstation")
        .def(
            "ReadFileWithAuth",
            [](IMaster& self, const std::string& filename, uint32_t authKey, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileReadResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.ReadFile(filename, authKey, safe_cb, config);
            },
            py::arg("filename"), py::arg("authKey"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Read a file from the outstation with an auth key")
        .def(
            "WriteFile",
            [](IMaster& self, const std::string& filename, py::bytes data, FilePermissions permissions,
               py::function callback, const TaskConfig& config) {
                std::string raw = data;
                std::vector<uint8_t> vec(raw.begin(), raw.end());
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileWriteResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.WriteFile(filename, vec, permissions, safe_cb, config);
            },
            py::arg("filename"), py::arg("data"), py::arg("permissions"), py::arg("callback"),
            py::arg("config") = TaskConfig::Default(), "Write a file to the outstation")
        .def(
            "WriteFileWithAuth",
            [](IMaster& self, const std::string& filename, py::bytes data, FilePermissions permissions, FileMode mode,
               uint32_t authKey, py::function callback, const TaskConfig& config) {
                std::string raw = data;
                std::vector<uint8_t> vec(raw.begin(), raw.end());
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileWriteResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.WriteFile(filename, vec, permissions, mode, authKey, safe_cb, config);
            },
            py::arg("filename"), py::arg("data"), py::arg("permissions"), py::arg("mode"), py::arg("authKey"),
            py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Write a file to the outstation with explicit mode and auth key")
        .def(
            "DeleteFile",
            [](IMaster& self, const std::string& filename, py::function callback, const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileOperationResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.DeleteFile(filename, safe_cb, config);
            },
            py::arg("filename"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Delete a file on the outstation")
        .def(
            "GetFileInfo",
            [](IMaster& self, const std::string& filename, py::function callback, const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileInfoResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.GetFileInfo(filename, safe_cb, config);
            },
            py::arg("filename"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Get file information from the outstation")
        .def(
            "ReadDirectory",
            [](IMaster& self, const std::string& path, py::function callback, const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const DirectoryReadResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.ReadDirectory(path, safe_cb, config);
            },
            py::arg("path"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Read a directory listing from the outstation")
        .def(
            "AbortFile",
            [](IMaster& self, uint32_t fileHandle, py::function callback, const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileOperationResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.AbortFile(fileHandle, safe_cb, config);
            },
            py::arg("fileHandle"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Abort an in-progress file transfer")
        .def(
            "AuthenticateFile",
            [](IMaster& self, const std::string& username, const std::string& password, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileAuthResult_t& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.AuthenticateFile(username, password, safe_cb, config);
            },
            py::arg("username"), py::arg("password"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Authenticate for file transfer operations")
        // ===== Dead-band and Link Status Methods =====
        .def(
            "WriteDeadBands",
            [](IMaster& self, const std::vector<Indexed<AnalogInputDeadband>>& deadBands, py::function callback,
               const TaskConfig& config) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](const FileOperationResult& result) mutable {
                    auto snap = result;
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(snap);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.WriteDeadBands(deadBands, safe_cb, config);
            },
            py::arg("deadBands"), py::arg("callback"), py::arg("config") = TaskConfig::Default(),
            "Write analog input dead-band values to the outstation")
        .def(
            "CheckLinkStatus",
            [](IMaster& self, py::function callback) {
                auto cb_ptr = make_safe_callback(std::move(callback));
                auto safe_cb = [cb_ptr](bool success) mutable {
                    py::gil_scoped_acquire gil;
                    (*cb_ptr)(success);
                    cb_ptr.reset();
                };
                py::gil_scoped_release release;
                self.CheckLinkStatus(safe_cb);
            },
            py::arg("callback"), "Check link status with the outstation");
}
