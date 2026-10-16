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

/*
 * Outstation driven by the dnp3c conformance plugin in conformance/.
 *
 * The configuration comes from the command line. Once the outstation listens it
 * prints "READY". Each line on stdin is a command, answered by one line on stdout
 * that starts with "OK" or "ERR". Lines starting with "EV" report what the master
 * did: operated an output, wrote the time, or assigned classes.
 *
 *   bi|dbi|cnt|ai|bo|ao <index> <value> <flags> <event 0|1>   update a point, answers "OK <ms> <quality>"
 *   freeze <index> <clear 0|1>                               freeze a counter, answers "OK <ms> <quality>"
 *   fcntflags <index> <flags>                                set a frozen counter's flags
 *   local <index>... | remote                                put binary outputs under local or remote control
 *   coldrestart <0|1>                                        enable or disable cold restart support
 *   now                                                      answers "OK <ms> <quality>"
 *   quit
 */

#include <opendnp3/logging/ILogHandler.h>
#include <opendnp3/DNP3Manager.h>
#include <opendnp3/logging/LogLevels.h>
#include <opendnp3/outstation/ICommandHandler.h>
#include <opendnp3/outstation/IOutstationApplication.h>
#include <opendnp3/outstation/UpdateBuilder.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace opendnp3;

namespace
{

constexpr uint16_t NUM_INPUTS = 10;
constexpr uint16_t NUM_OUTPUTS = 20;

std::mutex output_mutex;

void Emit(const std::string& line)
{
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << line << std::endl;
}

struct Options
{
    std::string address = "127.0.0.1";
    uint16_t port = 20000;
    uint16_t local_addr = 1024;
    uint16_t remote_addr = 1;
    bool unsolicited = false;
    int unsol_retries = -1;
    uint32_t sol_confirm_timeout_ms = 2000;
    uint32_t unsol_confirm_timeout_ms = 2000;
    uint32_t select_timeout_ms = 2000;
    uint32_t frag_size = 2048;
    uint32_t max_controls = 4;
    uint16_t event_buffer = 200;
    bool cold_restart = true;
    std::set<std::string> disabled;
    std::string log_levels = "normal";
};

uint32_t ToU32(const char* text)
{
    return static_cast<uint32_t>(std::strtoul(text, nullptr, 0));
}

Options ParseOptions(int argc, char* argv[])
{
    Options options;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string key = argv[i];
        const char* value = argv[i + 1];
        if (key == "--address")
            options.address = value;
        else if (key == "--port")
            options.port = static_cast<uint16_t>(ToU32(value));
        else if (key == "--local")
            options.local_addr = static_cast<uint16_t>(ToU32(value));
        else if (key == "--remote")
            options.remote_addr = static_cast<uint16_t>(ToU32(value));
        else if (key == "--unsol")
            options.unsolicited = ToU32(value) != 0;
        else if (key == "--unsol-retries")
            options.unsol_retries = std::atoi(value);
        else if (key == "--sol-confirm-timeout")
            options.sol_confirm_timeout_ms = ToU32(value);
        else if (key == "--unsol-confirm-timeout")
            options.unsol_confirm_timeout_ms = ToU32(value);
        else if (key == "--select-timeout")
            options.select_timeout_ms = ToU32(value);
        else if (key == "--frag")
            options.frag_size = ToU32(value);
        else if (key == "--max-controls")
            options.max_controls = ToU32(value);
        else if (key == "--event-buffer")
            options.event_buffer = static_cast<uint16_t>(ToU32(value));
        else if (key == "--cold-restart")
            options.cold_restart = ToU32(value) != 0;
        else if (key == "--log")
            options.log_levels = value;
        else if (key == "--disable")
        {
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, ','))
            {
                if (!item.empty())
                    options.disabled.insert(item);
            }
        }
        else
        {
            std::cerr << "unknown option: " << key << std::endl;
            std::exit(2);
        }
    }
    return options;
}

class Application final : public IOutstationApplication
{
public:
    std::atomic<bool> local_control{false};
    std::atomic<bool> cold_restart{true};

    DNPTime Now() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto elapsed
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_update);
        DNPTime result(last_timestamp + static_cast<uint64_t>(elapsed.count()));
        result.quality = (synchronized && elapsed <= refresh_rate) ? TimestampQuality::SYNCHRONIZED
                                                                    : TimestampQuality::UNSYNCHRONIZED;
        return result;
    }

    bool SupportsWriteAbsoluteTime() override
    {
        return true;
    }

    bool WriteAbsoluteTime(const UTCTimestamp& timestamp) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            last_timestamp = timestamp.msSinceEpoch;
            last_update = std::chrono::steady_clock::now();
            synchronized = true;
        }
        Emit("EV TIME " + std::to_string(timestamp.msSinceEpoch));
        return true;
    }

    bool SupportsAssignClass() override
    {
        return true;
    }

    void RecordClassAssignment(AssignClassType type, PointClass clazz, uint16_t start, uint16_t stop) override
    {
        Emit("EV ASSIGN " + std::to_string(static_cast<int>(type)) + " " + std::to_string(static_cast<int>(clazz)) + " "
             + std::to_string(start) + " " + std::to_string(stop));
    }

    ApplicationIIN GetApplicationIIN() const override
    {
        ApplicationIIN iin;
        iin.localControl = local_control;
        std::lock_guard<std::mutex> lock(mutex);
        iin.needTime = !synchronized || (std::chrono::steady_clock::now() - last_update) > refresh_rate / 2;
        return iin;
    }

    RestartMode ColdRestartSupport() const override
    {
        return cold_restart ? RestartMode::SUPPORTED_DELAY_FINE : RestartMode::UNSUPPORTED;
    }

    uint16_t ColdRestart() override
    {
        Emit("EV COLDRESTART");
        return 5000;
    }

private:
    mutable std::mutex mutex;
    const std::chrono::milliseconds refresh_rate{10000};
    bool synchronized = false;
    // the device's own clock until the master sets it
    uint64_t last_timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
};

class CommandHandler final : public ICommandHandler
{
public:
    CommandHandler(bool binary_disabled, bool analog_disabled)
        : binary_disabled(binary_disabled), analog_disabled(analog_disabled)
    {
    }

    void SetLocal(const std::set<uint16_t>& indices)
    {
        std::lock_guard<std::mutex> lock(mutex);
        local = indices;
    }

    void Begin() override {}
    void End() override {}

    CommandStatus Select(const ControlRelayOutputBlock& command, uint16_t index) override
    {
        return CheckCrob(command, index);
    }

    CommandStatus Operate(const ControlRelayOutputBlock& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        const auto status = CheckCrob(command, index);
        if (status == CommandStatus::SUCCESS)
        {
            Emit("EV CROB " + std::to_string(index));
        }
        return status;
    }

    CommandStatus Select(const AnalogOutputInt16& command, uint16_t index) override
    {
        return CheckAnalog(command.value, index);
    }
    CommandStatus Operate(const AnalogOutputInt16& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        return OperateAnalog(command.value, index, handler);
    }
    CommandStatus Select(const AnalogOutputInt32& command, uint16_t index) override
    {
        return CheckAnalog(command.value, index);
    }
    CommandStatus Operate(const AnalogOutputInt32& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        return OperateAnalog(command.value, index, handler);
    }
    CommandStatus Select(const AnalogOutputFloat32& command, uint16_t index) override
    {
        return CheckAnalog(command.value, index);
    }
    CommandStatus Operate(const AnalogOutputFloat32& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        return OperateAnalog(command.value, index, handler);
    }
    CommandStatus Select(const AnalogOutputDouble64& command, uint16_t index) override
    {
        return CheckAnalog(command.value, index);
    }
    CommandStatus Operate(const AnalogOutputDouble64& command,
                          uint16_t index,
                          IUpdateHandler& handler,
                          OperateType opType) override
    {
        return OperateAnalog(command.value, index, handler);
    }

private:
    struct CrobRule
    {
        std::vector<std::pair<OperationType, TripCloseCode>> codes;
        uint8_t count;
        uint32_t on;
        uint32_t off;
    };

    // the command each binary output accepts, as device.ini describes them
    static CrobRule RuleFor(uint16_t index)
    {
        using O = OperationType;
        using T = TripCloseCode;
        switch (index)
        {
        case 0:
            return {{{O::LATCH_ON, T::NUL}}, 1, 100, 200};
        case 1:
            return {{{O::PULSE_ON, T::TRIP}}, 5, 300, 400};
        case 2:
            return {{{O::LATCH_ON, T::NUL}, {O::LATCH_OFF, T::NUL}}, 10, 500, 600};
        case 3:
            return {{{O::PULSE_ON, T::TRIP}, {O::PULSE_ON, T::CLOSE}}, 15, 700, 800};
        case 4:
            return {{{O::LATCH_ON, T::NUL}, {O::LATCH_OFF, T::NUL}, {O::PULSE_ON, T::TRIP}, {O::PULSE_ON, T::CLOSE}},
                    20,
                    900,
                    1000};
        case 5:
            return {{{O::PULSE_ON, T::NUL}}, 25, 1100, 1200};
        case 6:
            return {{{O::LATCH_ON, T::NUL}}, 30, 1300, 1400};
        case 7:
            return {{{O::LATCH_OFF, T::NUL}}, 35, 1500, 1600};
        case 8:
            return {{{O::PULSE_ON, T::CLOSE}}, 40, 1700, 1800};
        case 9:
            return {{{O::PULSE_ON, T::TRIP}}, 45, 1900, 2000};
        default:
            return {{{O::LATCH_ON, T::NUL}}, 1, 100, 200};
        }
    }

    CommandStatus CheckCrob(const ControlRelayOutputBlock& command, uint16_t index)
    {
        if (binary_disabled || index >= NUM_OUTPUTS)
            return CommandStatus::NOT_SUPPORTED;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (local.count(index))
                return CommandStatus::LOCAL;
        }
        const auto rule = RuleFor(index);
        bool code_ok = false;
        for (const auto& code : rule.codes)
        {
            code_ok |= (command.opType == code.first && command.tcc == code.second);
        }
        const bool ok = code_ok && !command.clear && command.count == rule.count && command.onTimeMS == rule.on
            && command.offTimeMS == rule.off;
        return ok ? CommandStatus::SUCCESS : CommandStatus::FORMAT_ERROR;
    }

    CommandStatus CheckAnalog(double value, uint16_t index) const
    {
        if (analog_disabled || index >= NUM_OUTPUTS)
            return CommandStatus::NOT_SUPPORTED;
        switch (index)
        {
        case 0:
            return value == 10.0 ? CommandStatus::SUCCESS : CommandStatus::FORMAT_ERROR;
        case 1:
            return value == 20.0 ? CommandStatus::SUCCESS : CommandStatus::FORMAT_ERROR;
        default:
            if (index >= 10)
                return (value == 10.0 || value == 70000.0) ? CommandStatus::SUCCESS : CommandStatus::FORMAT_ERROR;
            return CommandStatus::NOT_SUPPORTED;
        }
    }

    CommandStatus OperateAnalog(double value, uint16_t index, IUpdateHandler& handler)
    {
        const auto status = CheckAnalog(value, index);
        if (status == CommandStatus::SUCCESS)
        {
            handler.Update(AnalogOutputStatus(value, Flags(0x01)), index, EventMode::Detect);
            std::ostringstream oss;
            oss.precision(17);
            oss << "EV AO " << index << " " << value;
            Emit(oss.str());
        }
        return status;
    }

    const bool binary_disabled;
    const bool analog_disabled;
    std::mutex mutex;
    std::set<uint16_t> local;
};

// stdout carries the control protocol, so the stack logs to stderr
class StderrLogger final : public ILogHandler
{
public:
    void log(ModuleId module, const char* id, LogLevel level, char const* location, char const* message) override
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::lock_guard<std::mutex> lock(output_mutex);
        std::cerr << "ms(" << ms << ") " << LogFlagToString(level) << " " << id << " - " << message << std::endl;
    }
};

std::string TimeText(const DNPTime& time)
{
    return std::to_string(time.value) + " " + std::to_string(static_cast<int>(time.quality));
}

LogLevels ParseLogLevels(const std::string& name)
{
    if (name == "all")
        return levels::ALL;
    if (name == "comms")
        return levels::NORMAL | levels::ALL_COMMS;
    return levels::NORMAL;
}

} // namespace

int main(int argc, char* argv[])
{
    const auto options = ParseOptions(argc, argv);
    const auto enabled = [&](const char* kind) { return options.disabled.count(kind) == 0; };

    auto app = std::make_shared<Application>();
    app->cold_restart = options.cold_restart;
    auto commands = std::make_shared<CommandHandler>(!enabled("bo"), !enabled("ao"));

    DatabaseConfig database;
    for (uint16_t i = 0; i < NUM_INPUTS; ++i)
    {
        if (enabled("bi"))
        {
            // binary inputs 8 and 9 start in class 2, as device.ini's class.assign options say
            database.binary_input[i].clazz = (i >= 8) ? PointClass::Class2 : PointClass::Class1;
            database.binary_input[i].evariation = EventBinaryVariation::Group2Var2;
        }
        if (enabled("dbi"))
        {
            database.double_binary[i].clazz = PointClass::Class1;
            database.double_binary[i].evariation = EventDoubleBinaryVariation::Group4Var2;
        }
        if (enabled("cnt"))
        {
            database.counter[i].clazz = PointClass::Class2;
            if (enabled("fcnt"))
            {
                database.frozen_counter[i].clazz = PointClass::Class1;
            }
        }
        if (enabled("ai"))
        {
            database.analog_input[i].clazz = PointClass::Class3;
            database.analog_input[i].deadband = 1;
        }
    }
    for (uint16_t i = 0; i < NUM_OUTPUTS; ++i)
    {
        if (enabled("bo"))
        {
            database.binary_output_status[i].clazz = PointClass::Class0;
        }
        if (enabled("ao"))
        {
            database.analog_output_status[i].clazz = PointClass::Class0;
            // the level 2 subset reports analog output status as 16-bit
            database.analog_output_status[i].svariation = StaticAnalogOutputStatusVariation::Group40Var2;
        }
    }

    OutstationStackConfig config(database);
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(options.event_buffer);
    config.outstation.params.allowUnsolicited = options.unsolicited;
    config.outstation.params.numUnsolRetries = options.unsol_retries < 0
        ? NumRetries::Infinite()
        : NumRetries::Fixed(static_cast<std::size_t>(options.unsol_retries));
    config.outstation.params.solConfirmTimeout = TimeDuration::Milliseconds(options.sol_confirm_timeout_ms);
    config.outstation.params.unsolConfirmTimeout = TimeDuration::Milliseconds(options.unsol_confirm_timeout_ms);
    config.outstation.params.selectTimeout = TimeDuration::Milliseconds(options.select_timeout_ms);
    config.outstation.params.maxTxFragSize = options.frag_size;
    config.outstation.params.maxControlsPerRequest = options.max_controls;
    config.link.LocalAddr = options.local_addr;
    config.link.RemoteAddr = options.remote_addr;
    config.link.KeepAliveTimeout = TimeDuration::Max();

    const auto log_levels = ParseLogLevels(options.log_levels);
    DNP3Manager manager(1, std::make_shared<StderrLogger>());
    auto channel = manager.AddTCPServer("server", log_levels, ServerAcceptMode::CloseExisting,
                                        IPEndpoint(options.address, options.port), nullptr);
    auto outstation = channel->AddOutstation("outstation", commands, app, config);

    // the starting values, as the plugin's mirror starts them: every point ONLINE, and off or zero but for the
    // counters, which have counted to 1 since their frozen value of 0, so a freeze changes every frozen counter
    {
        UpdateBuilder builder;
        for (uint16_t i = 0; i < NUM_INPUTS; ++i)
        {
            builder.Update(Binary(false, Flags(0x01)), i, EventMode::Suppress);
            builder.Update(DoubleBitBinary(DoubleBit::DETERMINED_OFF, Flags(0x01)), i, EventMode::Suppress);
            builder.Update(Counter(0, Flags(0x01)), i, EventMode::Suppress);
            builder.FreezeCounter(i, false, EventMode::Suppress);
            builder.Update(Counter(1, Flags(0x01)), i, EventMode::Suppress);
            builder.Update(Analog(0.0, Flags(0x01)), i, EventMode::Suppress);
        }
        for (uint16_t i = 0; i < NUM_OUTPUTS; ++i)
        {
            builder.Update(BinaryOutputStatus(false, Flags(0x01)), i, EventMode::Suppress);
            builder.Update(AnalogOutputStatus(0.0, Flags(0x01)), i, EventMode::Suppress);
        }
        outstation->Apply(builder.Build());
    }

    outstation->Enable();
    Emit("READY");

    std::string line;
    while (std::getline(std::cin, line))
    {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        if (cmd.empty())
            continue;
        if (cmd == "quit")
        {
            Emit("OK");
            break;
        }
        if (cmd == "now")
        {
            Emit("OK " + TimeText(app->Now()));
            continue;
        }
        if (cmd == "coldrestart")
        {
            int value = 1;
            in >> value;
            app->cold_restart = value != 0;
            Emit("OK");
            continue;
        }
        if (cmd == "local" || cmd == "remote")
        {
            std::set<uint16_t> indices;
            uint32_t index = 0;
            while (in >> index)
                indices.insert(static_cast<uint16_t>(index));
            commands->SetLocal(indices);
            app->local_control = !indices.empty();
            Emit("OK");
            continue;
        }
        if (cmd == "fcntflags")
        {
            uint32_t index = 0;
            uint32_t flags = 0;
            in >> index >> flags;
            UpdateBuilder builder;
            const auto idx = static_cast<uint16_t>(index);
            builder.Modify(FlagsType::FrozenCounter, idx, idx, static_cast<uint8_t>(flags));
            outstation->Apply(builder.Build());
            Emit("OK " + TimeText(app->Now()));
            continue;
        }
        if (cmd == "freeze")
        {
            uint32_t index = 0;
            int clear = 0;
            in >> index >> clear;
            UpdateBuilder builder;
            const auto time = app->Now();
            builder.FreezeCounter(static_cast<uint16_t>(index), clear != 0, EventMode::Detect);
            outstation->Apply(builder.Build());
            Emit("OK " + TimeText(time));
            continue;
        }

        uint32_t index = 0;
        double value = 0;
        uint32_t flags = 0;
        int event = 1;
        if (!(in >> index >> value >> flags >> event))
        {
            Emit("ERR bad arguments: " + line);
            continue;
        }
        const auto idx = static_cast<uint16_t>(index);
        const auto mode = event ? EventMode::Detect : EventMode::Suppress;
        const auto time = app->Now();
        const Flags f(static_cast<uint8_t>(flags));
        UpdateBuilder builder;
        if (cmd == "bi")
            builder.Update(Binary(value != 0, f, time), idx, mode);
        else if (cmd == "dbi")
            builder.Update(DoubleBitBinary(static_cast<DoubleBit>(static_cast<int>(value)), f, time), idx, mode);
        else if (cmd == "cnt")
            builder.Update(Counter(static_cast<uint32_t>(value), f, time), idx, mode);
        else if (cmd == "ai")
            builder.Update(Analog(value, f, time), idx, mode);
        else if (cmd == "bo")
            builder.Update(BinaryOutputStatus(value != 0, f, time), idx, mode);
        else if (cmd == "ao")
            builder.Update(AnalogOutputStatus(value, f, time), idx, mode);
        else
        {
            Emit("ERR unknown command: " + cmd);
            continue;
        }
        outstation->Apply(builder.Build());
        Emit("OK " + TimeText(time));
    }

    return 0;
}
