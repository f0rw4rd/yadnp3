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

#include "opendnp3/DNP3Manager.h"
#include "opendnp3/channel/IChannel.h"
#include "opendnp3/channel/IChannelListener.h"
#include "opendnp3/channel/TLSConfig.h"
#include "opendnp3/logging/ILogHandler.h"
#include "opendnp3/outstation/IFileHandler.h"

#include "gil_safe.h"

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace opendnp3;

// Trampoline for ILogHandler so Python can subclass it
// Wrapped in try/catch so that Python exceptions on ASIO threads are safely
// discarded instead of causing std::terminate().
class PyLogHandler : public ILogHandler
{
public:
    using ILogHandler::ILogHandler;

    void log(ModuleId module, const char* id, LogLevel level, char const* location, char const* message) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(void, ILogHandler, log, module, id, level, location, message);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyLogHandler::log");
        }
        catch (const std::exception&)
        {
        }
    }
};

// Trampoline for IChannelListener so Python can subclass it
class PyChannelListener : public IChannelListener
{
public:
    using IChannelListener::IChannelListener;

    void OnStateChange(ChannelState state) override
    {
        try
        {
            YADNP3_OVERRIDE_PURE(void, IChannelListener, OnStateChange, state);
        }
        catch (py::error_already_set& e)
        {
            py::gil_scoped_acquire gil;
            e.discard_as_unraisable("PyChannelListener::OnStateChange");
        }
        catch (const std::exception&)
        {
        }
    }
};

void init_channel(py::module_& m)
{
    // ModuleId
    py::class_<ModuleId>(m, "ModuleId")
        .def(py::init<>())
        .def(py::init<int32_t>(), py::arg("level"))
        .def_readwrite("value", &ModuleId::value);

    // ILogHandler
    py::class_<ILogHandler, PyLogHandler, std::shared_ptr<ILogHandler>>(m, "ILogHandler",
                                                                        "Callback interface for log messages")
        .def(py::init<>())
        .def("log", &ILogHandler::log, py::arg("module"), py::arg("id"), py::arg("level"), py::arg("location"),
             py::arg("message"));

    // IChannelListener
    py::class_<IChannelListener, PyChannelListener, std::shared_ptr<IChannelListener>>(
        m, "IChannelListener", "Callback interface for channel state changes")
        .def(py::init<>())
        .def("OnStateChange", &IChannelListener::OnStateChange, py::arg("state"));

    // IChannel
    py::class_<IChannel, std::shared_ptr<IChannel>>(m, "IChannel", "Communication channel")
        .def("GetLogFilters", &IChannel::GetLogFilters, py::call_guard<py::gil_scoped_release>())
        .def("SetLogFilters", &IChannel::SetLogFilters, py::arg("filters"))
        .def(
            "AddMaster",
            [](IChannel& self, const std::string& id, std::shared_ptr<ISOEHandler> SOEHandler,
               std::shared_ptr<IMasterApplication> application, const MasterStackConfig& config) {
                auto safe_soe = gil_safe_shared(std::move(SOEHandler));
                auto safe_app = gil_safe_shared(std::move(application));
                py::gil_scoped_release release;
                return self.AddMaster(id, safe_soe, safe_app, config);
            },
            py::arg("id"), py::arg("SOEHandler"), py::arg("application"), py::arg("config"),
            "Add a master session to this channel")
        .def(
            "AddOutstation",
            [](IChannel& self, const std::string& id, std::shared_ptr<ICommandHandler> commandHandler,
               std::shared_ptr<IOutstationApplication> application, const OutstationStackConfig& config,
               std::shared_ptr<IFileHandler> fileHandler) {
                auto safe_cmd = gil_safe_shared(std::move(commandHandler));
                auto safe_app = gil_safe_shared(std::move(application));
                auto safe_fh = gil_safe_shared(std::move(fileHandler));
                py::gil_scoped_release release;
                return self.AddOutstation(id, safe_cmd, safe_app, config, safe_fh);
            },
            py::arg("id"), py::arg("commandHandler"), py::arg("application"), py::arg("config"),
            py::arg("fileHandler") = std::shared_ptr<IFileHandler>(nullptr),
            "Add an outstation session to this channel")
        .def("Shutdown", &IChannel::Shutdown, py::call_guard<py::gil_scoped_release>());

    // TLSConfig
    py::class_<TLSConfig>(m, "TLSConfig", "TLS configuration information")
        .def(py::init<const std::string&, const std::string&, const std::string&, bool, bool, bool, bool,
                      const std::string&>(),
             py::arg("peerCertFilePath"), py::arg("localCertFilePath"), py::arg("privateKeyFilePath"),
             py::arg("allowTLSv10") = false, py::arg("allowTLSv11") = false, py::arg("allowTLSv12") = true,
             py::arg("allowTLSv13") = true, py::arg("cipherList") = "")
        .def_readwrite("peerCertFilePath", &TLSConfig::peerCertFilePath)
        .def_readwrite("localCertFilePath", &TLSConfig::localCertFilePath)
        .def_readwrite("privateKeyFilePath", &TLSConfig::privateKeyFilePath)
        .def_readwrite("allowTLSv10", &TLSConfig::allowTLSv10)
        .def_readwrite("allowTLSv11", &TLSConfig::allowTLSv11)
        .def_readwrite("allowTLSv12", &TLSConfig::allowTLSv12)
        .def_readwrite("allowTLSv13", &TLSConfig::allowTLSv13)
        .def_readwrite("cipherList", &TLSConfig::cipherList)
        .def_property(
            "verifyCallback",
            [](const TLSConfig& self) -> py::object {
                return self.verifyCallback ? py::cast(self.verifyCallback) : py::none();
            },
            [](TLSConfig& self, py::object cb) {
                if (cb.is_none())
                {
                    self.verifyCallback = nullptr;
                    return;
                }
                auto cb_ptr = std::shared_ptr<py::function>(new py::function(py::reinterpret_borrow<py::function>(cb)),
                                                            [](py::function* ptr) {
                                                                if (Py_IsInitialized())
                                                                {
                                                                    py::gil_scoped_acquire gil;
                                                                    delete ptr;
                                                                }
                                                            });
                self.verifyCallback = [cb_ptr](bool preverified, int depth, const std::string& subject,
                                               const std::string& certDER) -> bool {
                    if (!Py_IsInitialized())
                        return preverified;
                    py::gil_scoped_acquire gil;
                    return (*cb_ptr)(preverified, depth, subject, py::bytes(certDER.data(), certDER.size()))
                        .cast<bool>();
                };
            },
            "Optional callback(preverified: bool, depth: int, subject: str, cert_der: bytes) -> bool");

    // DNP3Manager
    // the destructor shuts the manager down and joins its threads, which may be waiting for the GIL
    py::class_<DNP3Manager, std::unique_ptr<DNP3Manager, GilReleasingDeleter<DNP3Manager>>>(
        m, "DNP3Manager", "Root DNP3 object used to create channels and sessions")
        .def(py::init([](uint32_t concurrencyHint, std::shared_ptr<ILogHandler> handler) {
                 return new DNP3Manager(concurrencyHint, gil_safe_shared(std::move(handler)));
             }),
             py::arg("concurrencyHint"), py::arg("handler") = std::shared_ptr<ILogHandler>())
        .def("Shutdown", &DNP3Manager::Shutdown, py::call_guard<py::gil_scoped_release>())
        .def(
            "AddTCPClient",
            [](DNP3Manager& self, const std::string& id, const LogLevels& levels, const ChannelRetry& retry,
               const std::vector<IPEndpoint>& hosts, const std::string& local,
               std::shared_ptr<IChannelListener> listener) {
                auto safe_listener = gil_safe_shared(std::move(listener));
                py::gil_scoped_release release;
                return self.AddTCPClient(id, levels, retry, hosts, local, safe_listener);
            },
            py::arg("id"), py::arg("levels"), py::arg("retry"), py::arg("hosts"), py::arg("local"), py::arg("listener"),
            "Add a persistent TCP client channel")
        .def(
            "AddOutstationTCPClient",
            [](DNP3Manager& self, const std::string& id, const LogLevels& levels, const ChannelRetry& retry,
               const std::vector<IPEndpoint>& hosts, const std::string& local,
               std::shared_ptr<IChannelListener> listener) {
                auto safe_listener = gil_safe_shared(std::move(listener));
                py::gil_scoped_release release;
                return self.AddOutstationTCPClient(id, levels, retry, hosts, local, safe_listener);
            },
            py::arg("id"), py::arg("levels"), py::arg("retry"), py::arg("hosts"), py::arg("local"), py::arg("listener"),
            "Add a TCP client channel for outstation use (connects to a remote master server)")
        .def(
            "AddTCPServer",
            [](DNP3Manager& self, const std::string& id, const LogLevels& levels, ServerAcceptMode mode,
               const IPEndpoint& endpoint, std::shared_ptr<IChannelListener> listener) {
                auto safe_listener = gil_safe_shared(std::move(listener));
                py::gil_scoped_release release;
                return self.AddTCPServer(id, levels, mode, endpoint, safe_listener);
            },
            py::arg("id"), py::arg("levels"), py::arg("mode"), py::arg("endpoint"), py::arg("listener"),
            "Add a persistent TCP server channel")
        .def(
            "AddTLSClient",
            [](DNP3Manager& self, const std::string& id, const LogLevels& levels, const ChannelRetry& retry,
               const std::vector<IPEndpoint>& hosts, const std::string& local, const TLSConfig& config,
               std::shared_ptr<IChannelListener> listener) {
                auto safe_listener = gil_safe_shared(std::move(listener));
                py::gil_scoped_release release;
                return self.AddTLSClient(id, levels, retry, hosts, local, config, safe_listener);
            },
            py::arg("id"), py::arg("levels"), py::arg("retry"), py::arg("hosts"), py::arg("local"), py::arg("config"),
            py::arg("listener"), "Add a persistent TLS client channel")
        .def(
            "AddTLSServer",
            [](DNP3Manager& self, const std::string& id, const LogLevels& levels, ServerAcceptMode mode,
               const IPEndpoint& endpoint, const TLSConfig& config, std::shared_ptr<IChannelListener> listener) {
                auto safe_listener = gil_safe_shared(std::move(listener));
                py::gil_scoped_release release;
                return self.AddTLSServer(id, levels, mode, endpoint, config, safe_listener);
            },
            py::arg("id"), py::arg("levels"), py::arg("mode"), py::arg("endpoint"), py::arg("config"),
            py::arg("listener"), "Add a persistent TLS server channel")
        .def(
            "AddUDPChannel",
            [](DNP3Manager& self, const std::string& id, const LogLevels& levels, const ChannelRetry& retry,
               const IPEndpoint& localEndpoint, const IPEndpoint& remoteEndpoint,
               std::shared_ptr<IChannelListener> listener) {
                auto safe_listener = gil_safe_shared(std::move(listener));
                py::gil_scoped_release release;
                return self.AddUDPChannel(id, levels, retry, localEndpoint, remoteEndpoint, safe_listener);
            },
            py::arg("id"), py::arg("levels"), py::arg("retry"), py::arg("localEndpoint"), py::arg("remoteEndpoint"),
            py::arg("listener"), "Add a persistent UDP channel")
        .def(
            "AddSerial",
            [](DNP3Manager& self, const std::string& id, const LogLevels& levels, const ChannelRetry& retry,
               SerialSettings settings, std::shared_ptr<IChannelListener> listener) {
                auto safe_listener = gil_safe_shared(std::move(listener));
                py::gil_scoped_release release;
                return self.AddSerial(id, levels, retry, settings, safe_listener);
            },
            py::arg("id"), py::arg("levels"), py::arg("retry"), py::arg("settings"), py::arg("listener"),
            "Add a persistent serial channel");
}
