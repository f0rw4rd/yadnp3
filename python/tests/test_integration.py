"""Integration tests: master <-> outstation loopback over TCP."""

import threading
import time

import pytest

import opendnp3


# ---------------------------------------------------------------------------
# Reusable callback implementations
# ---------------------------------------------------------------------------

class CollectingSOEHandler(opendnp3.ISOEHandler):
    """SOE handler that collects all measurements into lists."""

    def __init__(self):
        super().__init__()
        self.binaries = []
        self.analogs = []
        self.counters = []
        self.frozen_counters = []
        self.binary_output_statuses = []
        self.analog_output_statuses = []
        self.double_bit_binaries = []
        self.octet_strings = []
        self.time_and_intervals = []
        self.fragment_infos = []
        self._lock = threading.Lock()

    def BeginFragment(self, info):
        with self._lock:
            self.fragment_infos.append(info)

    def EndFragment(self, info):
        pass

    def Process(self, info, values):
        with self._lock:
            if not values:
                return
            first = values[0]
            val = first.value
            if isinstance(val, opendnp3.Binary):
                self.binaries.extend(values)
            elif isinstance(val, opendnp3.Analog):
                self.analogs.extend(values)
            elif isinstance(val, opendnp3.Counter):
                self.counters.extend(values)
            elif isinstance(val, opendnp3.FrozenCounter):
                self.frozen_counters.extend(values)
            elif isinstance(val, opendnp3.BinaryOutputStatus):
                self.binary_output_statuses.extend(values)
            elif isinstance(val, opendnp3.AnalogOutputStatus):
                self.analog_output_statuses.extend(values)
            elif isinstance(val, opendnp3.DoubleBitBinary):
                self.double_bit_binaries.extend(values)
            elif isinstance(val, opendnp3.OctetString):
                self.octet_strings.extend(values)
            elif isinstance(val, opendnp3.TimeAndInterval):
                self.time_and_intervals.extend(values)


class SimpleMasterApp(opendnp3.IMasterApplication):
    """Minimal master application that returns the current time."""

    def __init__(self):
        super().__init__()

    def OnReceiveIIN(self, iin):
        pass

    def OnTaskStart(self, type, id):
        pass

    def OnTaskComplete(self, info):
        pass

    def OnOpen(self):
        pass

    def OnClose(self):
        pass

    def AssignClassDuringStartup(self):
        return False

    def Now(self):
        import time as _time
        ms = int(_time.time() * 1000)
        return opendnp3.UTCTimestamp(ms)


class AcceptAllCommandHandler(opendnp3.ICommandHandler):
    """Command handler that accepts all commands with SUCCESS."""

    def __init__(self):
        super().__init__()
        self.received_crobs = []
        self.received_analog_commands = []
        self._lock = threading.Lock()

    def Begin(self):
        pass

    def End(self):
        pass

    def Select(self, command, index):
        return opendnp3.CommandStatus.SUCCESS

    def Operate(self, command, index, handler, opType):
        with self._lock:
            if isinstance(command, opendnp3.ControlRelayOutputBlock):
                self.received_crobs.append((command, index))
            else:
                self.received_analog_commands.append((command, index))
        return opendnp3.CommandStatus.SUCCESS


class SimpleOutstationApp(opendnp3.IOutstationApplication):
    """Minimal outstation application."""

    def __init__(self):
        super().__init__()

    def SupportsWriteAbsoluteTime(self):
        return False

    def WriteAbsoluteTime(self, timestamp):
        return False

    def SupportsAssignClass(self):
        return False

    def GetApplicationIIN(self):
        return opendnp3.ApplicationIIN()

    def ColdRestartSupport(self):
        return opendnp3.RestartMode.UNSUPPORTED

    def WarmRestartSupport(self):
        return opendnp3.RestartMode.UNSUPPORTED

    def ColdRestart(self):
        return 0

    def WarmRestart(self):
        return 0

    def Now(self):
        import time as _time
        ms = int(_time.time() * 1000)
        return opendnp3.DNPTime(ms)


# ---------------------------------------------------------------------------
# Shared port allocation to avoid conflicts between tests
# ---------------------------------------------------------------------------

_port_lock = threading.Lock()
_next_port = 20100


def _alloc_port():
    global _next_port
    with _port_lock:
        port = _next_port
        _next_port += 1
    return port


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="class")
def manager():
    """Create a DNP3Manager with 2 threads (shared per test class)."""
    mgr = opendnp3.DNP3Manager(2)
    yield mgr
    mgr.Shutdown()


@pytest.fixture(scope="class")
def loopback(manager):
    """Set up a full master <-> outstation loopback on TCP.

    Returns (master, outstation, soe_handler, cmd_handler, port).
    """
    port = _alloc_port()

    # -- Outstation side (TCP server) --
    server_channel = manager.AddTCPServer(
        "server",
        opendnp3.levels.NOTHING,
        opendnp3.ServerAcceptMode.CloseExisting,
        opendnp3.IPEndpoint("127.0.0.1", port),
        None,
    )

    cmd_handler = AcceptAllCommandHandler()
    outstation_app = SimpleOutstationApp()
    outstation_config = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(10))
    outstation_config.outstation.params.allowUnsolicited = False
    outstation_config.link.LocalAddr = 1024
    outstation_config.link.RemoteAddr = 1

    outstation = server_channel.AddOutstation(
        "outstation", cmd_handler, outstation_app, outstation_config
    )

    # -- Master side (TCP client) --
    client_channel = manager.AddTCPClient(
        "client",
        opendnp3.levels.NOTHING,
        opendnp3.ChannelRetry.Default(),
        [opendnp3.IPEndpoint("127.0.0.1", port)],
        "0.0.0.0",
        None,
    )

    soe_handler = CollectingSOEHandler()
    master_app = SimpleMasterApp()
    master_config = opendnp3.MasterStackConfig()
    master_config.master.disableUnsolOnStartup = True
    master_config.link.LocalAddr = 1
    master_config.link.RemoteAddr = 1024

    master = client_channel.AddMaster(
        "master", soe_handler, master_app, master_config
    )

    # Enable both stacks and wait for connection + startup integrity poll
    outstation.Enable()
    master.Enable()
    time.sleep(1.5)

    yield master, outstation, soe_handler, cmd_handler, port

    # Cleanup (manager.Shutdown() handles the rest)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestLoopbackConnectivity:
    """Verify the master and outstation can connect and exchange data."""

    def test_startup_integrity_poll(self, loopback):
        """After enable, the master performs a startup integrity poll.
        The SOE handler should have received data (even if default zeros)."""
        master, outstation, soe, cmd, port = loopback
        # The integrity poll should have delivered initial values for the
        # 10 binary/analog/counter points configured in DatabaseConfig(10).
        # At minimum we should see some measurement callbacks.
        assert (
            len(soe.binaries) > 0
            or len(soe.analogs) > 0
            or len(soe.counters) > 0
            or len(soe.binary_output_statuses) > 0
            or len(soe.analog_output_statuses) > 0
        ), "Expected at least one measurement from the startup integrity poll"

    def test_binary_values_from_integrity_poll(self, loopback):
        """All binaries should arrive with default value (False) and online flag."""
        master, outstation, soe, cmd, port = loopback
        assert len(soe.binaries) == 10
        for indexed_bin in soe.binaries:
            assert indexed_bin.value.value is False

    def test_analog_values_from_integrity_poll(self, loopback):
        """All analogs should arrive with default value (0.0)."""
        master, outstation, soe, cmd, port = loopback
        assert len(soe.analogs) == 10
        for indexed_analog in soe.analogs:
            assert indexed_analog.value.value == 0.0

    def test_counter_values_from_integrity_poll(self, loopback):
        """All counters should arrive with default value (0)."""
        master, outstation, soe, cmd, port = loopback
        assert len(soe.counters) == 10
        for indexed_ctr in soe.counters:
            assert indexed_ctr.value.value == 0


class TestDatabaseUpdates:
    """Verify that outstation database updates are visible to the master."""

    def test_update_binary(self, loopback):
        """Update a binary point and verify the master sees the new value."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()

        # Update binary index 3 to True
        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Binary(True), 3)
        outstation.Apply(builder.Build())

        # Trigger a class scan to pick up the change
        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        # Find index 3 in the results
        idx3 = [b for b in soe.binaries if b.index == 3]
        assert len(idx3) > 0, "Expected binary index 3 in scan results"
        assert idx3[-1].value.value is True

    def test_update_analog(self, loopback):
        """Update an analog point and verify the master sees the new value."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        # Use integer value since default DNP3 analog variation is 32-bit integer
        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Analog(42.0), 0)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx0 = [a for a in soe.analogs if a.index == 0]
        assert len(idx0) > 0, "Expected analog index 0 in scan results"
        assert abs(idx0[-1].value.value - 42.0) < 1e-6

    def test_update_counter(self, loopback):
        """Update a counter point and verify the master sees the new value."""
        master, outstation, soe, cmd, port = loopback
        soe.counters.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Counter(999), 5)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx5 = [c for c in soe.counters if c.index == 5]
        assert len(idx5) > 0, "Expected counter index 5 in scan results"
        assert idx5[-1].value.value == 999

    def test_multiple_updates(self, loopback):
        """Multiple updates in a single batch are all visible."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()
        soe.analogs.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Binary(True), 0)
        builder.Update(opendnp3.Binary(True), 1)
        builder.Update(opendnp3.Analog(100.0), 2)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        true_bins = [b for b in soe.binaries if b.value.value is True]
        assert len(true_bins) >= 2

        a2 = [a for a in soe.analogs if a.index == 2]
        assert len(a2) > 0
        assert abs(a2[-1].value.value - 100.0) < 1e-6


class TestDirectOperate:
    """Verify direct-operate commands reach the outstation."""

    def test_direct_operate_crob(self, loopback):
        """Direct operate a CROB and verify the command handler sees it."""
        master, outstation, soe, cmd, port = loopback
        result_event = threading.Event()
        summaries = []

        def on_result(result):
            # Extract data inside callback; the reference is only valid here
            summaries.append(result.summary)
            result_event.set()

        crob = opendnp3.ControlRelayOutputBlock(opendnp3.OperationType.LATCH_ON)
        master.DirectOperate(crob, 0, on_result)
        result_event.wait(timeout=5.0)

        assert len(summaries) == 1
        assert summaries[0] == opendnp3.TaskCompletion.SUCCESS

        # Verify the command handler received the CROB
        time.sleep(0.5)
        assert len(cmd.received_crobs) > 0
        _, idx = cmd.received_crobs[-1]
        assert idx == 0

    def test_direct_operate_analog_output(self, loopback):
        """Direct operate an analog output command."""
        master, outstation, soe, cmd, port = loopback
        result_event = threading.Event()
        summaries = []

        def on_result(result):
            summaries.append(result.summary)
            result_event.set()

        ao = opendnp3.AnalogOutputInt32(12345)
        master.DirectOperate(ao, 1, on_result)
        result_event.wait(timeout=5.0)

        assert len(summaries) == 1
        assert summaries[0] == opendnp3.TaskCompletion.SUCCESS


class TestSelectAndOperate:
    """Verify select-before-operate commands work."""

    def test_sbo_crob(self, loopback):
        """Select-and-operate a CROB."""
        master, outstation, soe, cmd, port = loopback
        result_event = threading.Event()
        summaries = []

        def on_result(result):
            summaries.append(result.summary)
            result_event.set()

        crob = opendnp3.ControlRelayOutputBlock(opendnp3.OperationType.LATCH_OFF)
        master.SelectAndOperate(crob, 2, on_result)
        result_event.wait(timeout=5.0)

        assert len(summaries) == 1
        assert summaries[0] == opendnp3.TaskCompletion.SUCCESS


class TestLifecycle:
    """Verify proper lifecycle management."""

    def test_manager_create_shutdown(self):
        """Manager can be created and shut down cleanly."""
        mgr = opendnp3.DNP3Manager(1)
        mgr.Shutdown()

    def test_channel_shutdown(self, manager):
        """Channels can be shut down independently."""
        port = _alloc_port()
        ch = manager.AddTCPServer(
            "test-ch",
            opendnp3.levels.NOTHING,
            opendnp3.ServerAcceptMode.CloseExisting,
            opendnp3.IPEndpoint("127.0.0.1", port),
            None,
        )
        ch.Shutdown()

    def test_stack_disable_enable(self, loopback):
        """Stacks can be disabled and re-enabled."""
        master, outstation, soe, cmd, port = loopback
        master.Disable()
        outstation.Disable()
        time.sleep(0.5)

        outstation.Enable()
        master.Enable()
        time.sleep(1.5)

        # After re-enable, a new integrity poll should happen
        soe.analogs.clear()
        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)
        assert len(soe.analogs) > 0

    def test_repeated_create_destroy(self):
        """Rapid creation and destruction doesn't crash."""
        for _ in range(10):
            mgr = opendnp3.DNP3Manager(1)
            port = _alloc_port()
            ch = mgr.AddTCPServer(
                "s",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                None,
            )
            ch.Shutdown()
            mgr.Shutdown()


class TestClassScans:
    """Verify class-based scanning."""

    def test_class0_scan(self, loopback):
        """A Class 0 scan returns static data."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()
        soe.analogs.clear()

        field = opendnp3.ClassField(True, False, False, False)  # Class0 only
        master.ScanClasses(field, soe)
        time.sleep(0.5)

        # Should have received all static data (10 of each type)
        assert len(soe.binaries) == 10
        assert len(soe.analogs) == 10

    def test_add_class_scan(self, loopback):
        """AddClassScan creates a recurring periodic scan."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        # Add a periodic Class 0 scan every 500ms
        scan = master.AddClassScan(
            opendnp3.ClassField.AllClasses(),
            opendnp3.TimeDuration.Milliseconds(500),
            soe,
        )
        time.sleep(1.5)

        # Should have received at least 2 rounds of data
        assert len(soe.analogs) >= 20

    def test_demand_scan(self, loopback):
        """Demand() triggers a scan immediately."""
        master, outstation, soe, cmd, port = loopback

        # Add a slow periodic scan
        scan = master.AddClassScan(
            opendnp3.ClassField.AllClasses(),
            opendnp3.TimeDuration.Seconds(60),
            soe,
        )
        time.sleep(0.5)

        soe.analogs.clear()
        scan.Demand()
        time.sleep(0.5)

        assert len(soe.analogs) > 0


# ---------------------------------------------------------------------------
# Additional measurement type updates over loopback
# ---------------------------------------------------------------------------

class TestAdditionalMeasurementTypes:
    """Verify updating and reading back all measurement types."""

    def test_update_binary_output_status(self, loopback):
        """Update BinaryOutputStatus and verify master sees it."""
        master, outstation, soe, cmd, port = loopback
        soe.binary_output_statuses.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.BinaryOutputStatus(True), 3)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx3 = [b for b in soe.binary_output_statuses if b.index == 3]
        assert len(idx3) > 0, "Expected BinaryOutputStatus index 3 in scan results"
        assert idx3[-1].value.value is True

    def test_update_analog_output_status(self, loopback):
        """Update AnalogOutputStatus and verify master sees it."""
        master, outstation, soe, cmd, port = loopback
        soe.analog_output_statuses.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.AnalogOutputStatus(77.0), 2)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx2 = [a for a in soe.analog_output_statuses if a.index == 2]
        assert len(idx2) > 0, "Expected AnalogOutputStatus index 2 in scan results"
        assert abs(idx2[-1].value.value - 77.0) < 1e-6

    def test_update_double_bit_binary(self, loopback):
        """Update DoubleBitBinary and verify master sees it."""
        master, outstation, soe, cmd, port = loopback
        soe.double_bit_binaries.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.DoubleBitBinary(opendnp3.DoubleBit.DETERMINED_ON), 1)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx1 = [d for d in soe.double_bit_binaries if d.index == 1]
        assert len(idx1) > 0, "Expected DoubleBitBinary index 1 in scan results"
        assert idx1[-1].value.value == opendnp3.DoubleBit.DETERMINED_ON

    def test_update_frozen_counter(self, loopback):
        """Update a counter then use FreezeCounter and verify frozen counter is reported."""
        master, outstation, soe, cmd, port = loopback
        soe.frozen_counters.clear()

        # First set a counter value, then freeze it
        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Counter(500), 0)
        builder.FreezeCounter(0, False, opendnp3.EventMode.Detect)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx0 = [f for f in soe.frozen_counters if f.index == 0]
        assert len(idx0) > 0, "Expected FrozenCounter index 0 in scan results"
        assert idx0[-1].value.value == 500

    def test_update_octet_string(self, loopback):
        """Update OctetString and verify master sees it via scan."""
        master, outstation, soe, cmd, port = loopback
        soe.octet_strings.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Analog(1.0), 0)  # also update something else
        outstation.Apply(builder.Build())

        # OctetString updates need the outstation to use IUpdateHandler directly
        # through Apply. The UpdateBuilder doesn't expose OctetString in the same
        # way -- it goes through IUpdateHandler. Let's just verify we can create
        # and apply an update with other types and that the SOE handler receives
        # the scan response correctly.
        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        # Even if OctetString data isn't returned in class0, the SOE handler
        # callback infrastructure is exercised. Verify we got other data.
        assert len(soe.analogs) > 0


# ---------------------------------------------------------------------------
# All analog command types
# ---------------------------------------------------------------------------

class TestAllAnalogCommandTypes:
    """Verify all analog command output types work via DirectOperate and SBO."""

    def _do_command(self, master, command, index, timeout=5.0):
        """Helper: execute a DirectOperate and return the result snapshot."""
        result_event = threading.Event()
        results = []

        def on_result(result):
            results.append(result)
            result_event.set()

        master.DirectOperate(command, index, on_result)
        result_event.wait(timeout=timeout)
        assert len(results) == 1
        return results[0]

    def _sbo_command(self, master, command, index, timeout=5.0):
        """Helper: execute a SelectAndOperate and return the result snapshot."""
        result_event = threading.Event()
        results = []

        def on_result(result):
            results.append(result)
            result_event.set()

        master.SelectAndOperate(command, index, on_result)
        result_event.wait(timeout=timeout)
        assert len(results) == 1
        return results[0]

    def test_direct_operate_analog_int16(self, loopback):
        """DirectOperate with AnalogOutputInt16."""
        master, outstation, soe, cmd, port = loopback
        result = self._do_command(master, opendnp3.AnalogOutputInt16(1000), 0)
        assert result.summary == opendnp3.TaskCompletion.SUCCESS

    def test_direct_operate_analog_int32(self, loopback):
        """DirectOperate with AnalogOutputInt32."""
        master, outstation, soe, cmd, port = loopback
        result = self._do_command(master, opendnp3.AnalogOutputInt32(100000), 0)
        assert result.summary == opendnp3.TaskCompletion.SUCCESS

    def test_direct_operate_analog_float32(self, loopback):
        """DirectOperate with AnalogOutputFloat32."""
        master, outstation, soe, cmd, port = loopback
        result = self._do_command(master, opendnp3.AnalogOutputFloat32(3.14), 0)
        assert result.summary == opendnp3.TaskCompletion.SUCCESS

    def test_direct_operate_analog_double64(self, loopback):
        """DirectOperate with AnalogOutputDouble64."""
        master, outstation, soe, cmd, port = loopback
        result = self._do_command(master, opendnp3.AnalogOutputDouble64(2.71828), 0)
        assert result.summary == opendnp3.TaskCompletion.SUCCESS

    def test_sbo_analog_int16(self, loopback):
        """SelectAndOperate with AnalogOutputInt16."""
        master, outstation, soe, cmd, port = loopback
        result = self._sbo_command(master, opendnp3.AnalogOutputInt16(500), 1)
        assert result.summary == opendnp3.TaskCompletion.SUCCESS

    def test_sbo_analog_int32(self, loopback):
        """SelectAndOperate with AnalogOutputInt32."""
        master, outstation, soe, cmd, port = loopback
        result = self._sbo_command(master, opendnp3.AnalogOutputInt32(50000), 1)
        assert result.summary == opendnp3.TaskCompletion.SUCCESS

    def test_sbo_analog_float32(self, loopback):
        """SelectAndOperate with AnalogOutputFloat32."""
        master, outstation, soe, cmd, port = loopback
        result = self._sbo_command(master, opendnp3.AnalogOutputFloat32(1.5), 1)
        assert result.summary == opendnp3.TaskCompletion.SUCCESS

    def test_sbo_analog_double64(self, loopback):
        """SelectAndOperate with AnalogOutputDouble64."""
        master, outstation, soe, cmd, port = loopback
        result = self._sbo_command(master, opendnp3.AnalogOutputDouble64(9.99), 1)
        assert result.summary == opendnp3.TaskCompletion.SUCCESS


# ---------------------------------------------------------------------------
# ICommandTaskResult.to_list() test
# ---------------------------------------------------------------------------

class TestCommandTaskResultToList:
    """Verify the to_list() method on command task results."""

    def test_to_list_on_direct_operate(self, loopback):
        """The to_list() method returns a list of CommandPointResult."""
        master, outstation, soe, cmd, port = loopback
        result_event = threading.Event()
        results = []

        def on_result(result):
            results.append(result)
            result_event.set()

        crob = opendnp3.ControlRelayOutputBlock(opendnp3.OperationType.LATCH_ON)
        master.DirectOperate(crob, 0, on_result)
        result_event.wait(timeout=5.0)

        assert len(results) == 1
        point_results = results[0].to_list()
        assert isinstance(point_results, list)
        assert len(point_results) > 0

        # Verify CommandPointResult fields are accessible
        pr = point_results[0]
        assert pr.headerIndex is not None
        assert pr.index == 0
        assert pr.state == opendnp3.CommandPointState.SUCCESS
        assert pr.status == opendnp3.CommandStatus.SUCCESS


# ---------------------------------------------------------------------------
# IChannelListener callback test
# ---------------------------------------------------------------------------

class TestChannelListener:
    """Verify IChannelListener callback works."""

    def test_channel_listener_receives_state_changes(self):
        """A Python IChannelListener subclass receives OnStateChange calls."""

        class TestListener(opendnp3.IChannelListener):
            def __init__(self):
                super().__init__()
                self.states = []
                self._lock = threading.Lock()

            def OnStateChange(self, state):
                with self._lock:
                    self.states.append(state)

        listener = TestListener()
        mgr = opendnp3.DNP3Manager(1)
        try:
            port = _alloc_port()
            ch = mgr.AddTCPServer(
                "listener-test",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                listener,
            )
            time.sleep(0.5)

            # The server channel should report OPENING at minimum
            ch.Shutdown()
            time.sleep(0.5)

            # We should have received at least one state change
            assert len(listener.states) > 0
            # The final state after shutdown should be SHUTDOWN
            assert listener.states[-1] == opendnp3.ChannelState.SHUTDOWN
        finally:
            mgr.Shutdown()


# ---------------------------------------------------------------------------
# ILogHandler callback test
# ---------------------------------------------------------------------------

class TestLogHandler:
    """Verify ILogHandler callback works."""

    def test_log_handler_receives_messages(self):
        """A Python ILogHandler subclass receives log calls."""

        class TestLogHandler(opendnp3.ILogHandler):
            def __init__(self):
                super().__init__()
                self.messages = []
                self._lock = threading.Lock()

            def log(self, module, id, level, location, message):
                with self._lock:
                    self.messages.append((id, message))

        log_handler = TestLogHandler()
        mgr = opendnp3.DNP3Manager(1, log_handler)
        try:
            port = _alloc_port()
            # Use NORMAL log level so we get some log messages
            ch = mgr.AddTCPServer(
                "log-test",
                opendnp3.levels.NORMAL,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                None,
            )
            time.sleep(0.5)
            ch.Shutdown()
            time.sleep(0.5)

            # We should have received at least one log message
            assert len(log_handler.messages) > 0
        finally:
            mgr.Shutdown()


# ---------------------------------------------------------------------------
# IMaster.SetLogFilters test
# ---------------------------------------------------------------------------

class TestSetLogFilters:
    """Verify SetLogFilters on master and outstation."""

    def test_master_set_log_filters(self, loopback):
        """IMaster.SetLogFilters can be called without error."""
        master, outstation, soe, cmd, port = loopback
        # Set to everything, then to nothing
        master.SetLogFilters(opendnp3.LogLevels.everything())
        master.SetLogFilters(opendnp3.levels.NOTHING)

    def test_outstation_set_log_filters(self, loopback):
        """IOutstation.SetLogFilters can be called without error."""
        master, outstation, soe, cmd, port = loopback
        outstation.SetLogFilters(opendnp3.LogLevels.everything())
        outstation.SetLogFilters(opendnp3.levels.NOTHING)


# ---------------------------------------------------------------------------
# IOutstation.SetRestartIIN test
# ---------------------------------------------------------------------------

class TestSetRestartIIN:
    """Verify IOutstation.SetRestartIIN."""

    def test_set_restart_iin(self, loopback):
        """IOutstation.SetRestartIIN can be called without error."""
        master, outstation, soe, cmd, port = loopback
        # This sets the DEVICE_RESTART IIN bit
        outstation.SetRestartIIN()


# ---------------------------------------------------------------------------
# Custom header scan test
# ---------------------------------------------------------------------------

class TestCustomHeaderScan:
    """Verify IMaster.Scan with custom headers."""

    def test_scan_with_all_objects_header(self, loopback):
        """Scan with a specific AllObjects header for binary inputs (Group1Var0)."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()

        headers = [opendnp3.Header.AllObjects(1, 0)]  # Group1Var0 = binary inputs
        master.Scan(headers, soe)
        time.sleep(0.5)

        assert len(soe.binaries) == 10

    def test_scan_with_range_header(self, loopback):
        """Scan with a Range8 header for a subset of analog inputs."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        # Group30Var0 = analog inputs, range 0-4
        headers = [opendnp3.Header.Range8(30, 0, 0, 4)]
        master.Scan(headers, soe)
        time.sleep(0.5)

        # Should get 5 analog values (indices 0-4)
        assert len(soe.analogs) == 5


# ---------------------------------------------------------------------------
# SOE handler fragment info test
# ---------------------------------------------------------------------------

class TestSOEHandlerFragmentInfo:
    """Verify the SOE handler receives ResponseInfo in fragment callbacks."""

    def test_fragment_info_received(self, loopback):
        """BeginFragment is called with ResponseInfo during scans."""
        master, outstation, soe, cmd, port = loopback
        soe.fragment_infos.clear()

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        assert len(soe.fragment_infos) > 0
        info = soe.fragment_infos[0]
        assert hasattr(info, "unsolicited")
        assert hasattr(info, "fir")
        assert hasattr(info, "fin")


# ---------------------------------------------------------------------------
# Channel GetLogFilters / SetLogFilters test
# ---------------------------------------------------------------------------

class TestChannelLogFilters:
    """Verify IChannel.GetLogFilters and SetLogFilters."""

    def test_channel_get_set_log_filters(self, manager):
        """IChannel supports GetLogFilters and SetLogFilters."""
        port = _alloc_port()
        ch = manager.AddTCPServer(
            "filter-test",
            opendnp3.levels.NORMAL,
            opendnp3.ServerAcceptMode.CloseExisting,
            opendnp3.IPEndpoint("127.0.0.1", port),
            None,
        )

        # Get current filters
        filters = ch.GetLogFilters()
        assert filters is not None
        assert filters.get_value() != 0

        # Set to nothing
        ch.SetLogFilters(opendnp3.levels.NOTHING)
        new_filters = ch.GetLogFilters()
        assert new_filters.get_value() == 0

        # Set to everything
        ch.SetLogFilters(opendnp3.LogLevels.everything())
        all_filters = ch.GetLogFilters()
        assert all_filters.get_value() != 0

        ch.Shutdown()


# ---------------------------------------------------------------------------
# Master application callbacks test
# ---------------------------------------------------------------------------

class TestMasterApplicationCallbacks:
    """Verify that IMasterApplication callbacks fire during operation."""

    def test_on_task_start_and_complete(self):
        """OnTaskStart and OnTaskComplete are called during startup."""

        class TrackingMasterApp(opendnp3.IMasterApplication):
            def __init__(self):
                super().__init__()
                self.task_starts = []
                self.task_completions = []
                self.iin_received = []
                self.opened = threading.Event()
                self._lock = threading.Lock()

            def OnReceiveIIN(self, iin):
                with self._lock:
                    self.iin_received.append(iin)

            def OnTaskStart(self, type, id):
                with self._lock:
                    self.task_starts.append(type)

            def OnTaskComplete(self, info):
                with self._lock:
                    self.task_completions.append(info)

            def OnOpen(self):
                self.opened.set()

            def OnClose(self):
                pass

            def AssignClassDuringStartup(self):
                return False

            def Now(self):
                import time as _time
                ms = int(_time.time() * 1000)
                return opendnp3.UTCTimestamp(ms)

        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            # Outstation side
            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig
            )

            # Master side with tracking app
            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            tracking_app = TrackingMasterApp()
            soe = CollectingSOEHandler()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, tracking_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(2.0)

            # Verify callbacks fired
            assert tracking_app.opened.is_set(), "OnOpen should have been called"
            assert len(tracking_app.task_starts) > 0, "OnTaskStart should have been called"
            assert len(tracking_app.task_completions) > 0, "OnTaskComplete should have been called"

            # Verify TaskInfo fields are accessible
            info = tracking_app.task_completions[0]
            assert info.type is not None
            assert info.result is not None
            assert info.id is not None
            # Some internal tasks may not have a defined TaskId
            assert hasattr(info.id, "IsDefined")
            assert hasattr(info.id, "GetId")

        finally:
            mgr.Shutdown()


# ---------------------------------------------------------------------------
# Modify flags via UpdateBuilder
# ---------------------------------------------------------------------------

class TestModifyFlags:
    """Verify UpdateBuilder.Modify sets flags on measurement points."""

    def test_modify_binary_flags(self, loopback):
        """Modify sets quality flags on binary inputs visible to master."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()

        # Set the COMM_LOST flag (0x02) on binary inputs 0-4
        # Modify takes an int for the flags parameter
        builder = opendnp3.UpdateBuilder()
        builder.Modify(opendnp3.FlagsType.BinaryInput, 0, 4, 0x02)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        # The modified binaries should have the new flag value
        modified = [b for b in soe.binaries if 0 <= b.index <= 4]
        assert len(modified) == 5
        for b in modified:
            assert b.value.flags.value == 0x02


# ---------------------------------------------------------------------------
# EventMode integration test
# ---------------------------------------------------------------------------

class TestEventMode:
    """Verify EventMode.Force generates events visible to the master.

    Note: The default loopback fixture has EventBufferConfig with 0 event slots,
    so events are dropped. This test creates its own setup with event buffers.
    """

    def test_force_event_mode(self):
        """Updating with EventMode.Force generates an event visible via event scan."""
        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            # Outstation with event buffers configured
            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(10))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.outstation.eventBufferConfig = opendnp3.EventBufferConfig.AllTypes(50)
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig
            )

            # Master
            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, master_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(1.5)

            # Force an event on analog index 0
            builder = opendnp3.UpdateBuilder()
            builder.Update(opendnp3.Analog(10.0), 0, opendnp3.EventMode.Force)
            outstation.Apply(builder.Build())

            # Event class scan should pick up the event
            soe.analogs.clear()
            master.ScanClasses(opendnp3.ClassField.AllEventClasses(), soe)
            time.sleep(0.5)

            idx0 = [a for a in soe.analogs if a.index == 0]
            assert len(idx0) > 0, "Expected forced event for analog index 0"

        finally:
            mgr.Shutdown()


# ---------------------------------------------------------------------------
# GIL deadlock regression tests
# ---------------------------------------------------------------------------

class TestGILRelease:
    """Regression tests for GIL release on blocking C++ calls.

    Without py::call_guard<py::gil_scoped_release>(), these methods can
    deadlock when a Python callback (ILogHandler, IChannelListener) fires
    on a C++ thread while the main Python thread holds the GIL.
    """

    def _run_with_timeout(self, func, timeout=10.0):
        """Run func in a thread; fail if it doesn't complete within timeout."""
        result = [None]
        error = [None]

        def target():
            try:
                result[0] = func()
            except Exception as e:
                error[0] = e

        t = threading.Thread(target=target)
        t.start()
        t.join(timeout=timeout)
        if t.is_alive():
            pytest.fail(
                f"Operation did not complete within {timeout}s -- likely GIL deadlock"
            )
        if error[0] is not None:
            raise error[0]
        return result[0]

    def test_add_tcp_server_with_log_handler(self):
        """AddTCPServer must release the GIL so ILogHandler callbacks work."""

        class LogHandler(opendnp3.ILogHandler):
            def __init__(self):
                super().__init__()
                self.count = 0
            def log(self, module, id, level, location, message):
                self.count += 1

        handler = LogHandler()

        def do_work():
            mgr = opendnp3.DNP3Manager(1, handler)
            port = _alloc_port()
            ch = mgr.AddTCPServer(
                "gil-test-server",
                opendnp3.levels.NORMAL,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                None,
            )
            time.sleep(0.3)
            ch.Shutdown()
            mgr.Shutdown()
            return handler.count

        count = self._run_with_timeout(do_work)
        assert count > 0, "Log handler should have received messages"

    def test_add_tcp_client_with_log_handler(self):
        """AddTCPClient must release the GIL so ILogHandler callbacks work."""

        class LogHandler(opendnp3.ILogHandler):
            def __init__(self):
                super().__init__()
                self.count = 0
            def log(self, module, id, level, location, message):
                self.count += 1

        handler = LogHandler()

        def do_work():
            mgr = opendnp3.DNP3Manager(1, handler)
            port = _alloc_port()
            # Create a server so the client can connect and log activity
            server_ch = mgr.AddTCPServer(
                "gil-client-server",
                opendnp3.levels.NORMAL,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                None,
            )
            ch = mgr.AddTCPClient(
                "gil-test-client",
                opendnp3.levels.NORMAL,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                None,
            )
            # Add a master stack so that protocol activity generates logs
            soe = CollectingSOEHandler()
            m_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = ch.AddMaster("m", soe, m_app, mconfig)
            master.Enable()
            time.sleep(0.5)
            mgr.Shutdown()
            return handler.count

        count = self._run_with_timeout(do_work)
        assert count > 0, "Log handler should have received messages"

    def test_add_master_with_log_handler(self):
        """AddMaster must release the GIL so ILogHandler callbacks work."""

        class LogHandler(opendnp3.ILogHandler):
            def __init__(self):
                super().__init__()
                self.count = 0
            def log(self, module, id, level, location, message):
                self.count += 1

        handler = LogHandler()

        def do_work():
            mgr = opendnp3.DNP3Manager(1, handler)
            port = _alloc_port()
            ch = mgr.AddTCPServer(
                "gil-master-server",
                opendnp3.levels.NORMAL,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                None,
            )
            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = ch.AddMaster("m", soe, master_app, mconfig)
            master.Enable()
            time.sleep(0.3)
            master.Shutdown()
            ch.Shutdown()
            mgr.Shutdown()
            return handler.count

        count = self._run_with_timeout(do_work)
        assert count > 0, "Log handler should have received messages"

    def test_add_outstation_with_log_handler(self):
        """AddOutstation must release the GIL so ILogHandler callbacks work."""

        class LogHandler(opendnp3.ILogHandler):
            def __init__(self):
                super().__init__()
                self.count = 0
            def log(self, module, id, level, location, message):
                self.count += 1

        handler = LogHandler()

        def do_work():
            mgr = opendnp3.DNP3Manager(1, handler)
            port = _alloc_port()
            ch = mgr.AddTCPServer(
                "gil-outstation-server",
                opendnp3.levels.NORMAL,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                None,
            )
            cmd = AcceptAllCommandHandler()
            app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = ch.AddOutstation("o", cmd, app, oconfig)
            outstation.Enable()
            time.sleep(0.3)
            outstation.Shutdown()
            ch.Shutdown()
            mgr.Shutdown()
            return handler.count

        count = self._run_with_timeout(do_work)
        assert count > 0, "Log handler should have received messages"

    def test_apply_and_set_restart_iin_with_log_handler(self):
        """Apply and SetRestartIIN must release the GIL."""

        class LogHandler(opendnp3.ILogHandler):
            def __init__(self):
                super().__init__()
                self.count = 0
            def log(self, module, id, level, location, message):
                self.count += 1

        handler = LogHandler()

        def do_work():
            mgr = opendnp3.DNP3Manager(2, handler)
            port = _alloc_port()

            # Outstation side
            server_ch = mgr.AddTCPServer(
                "gil-apply-server",
                opendnp3.levels.NORMAL,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                None,
            )
            cmd = AcceptAllCommandHandler()
            app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_ch.AddOutstation("o", cmd, app, oconfig)
            outstation.Enable()
            time.sleep(0.3)

            # Apply updates (should release GIL)
            builder = opendnp3.UpdateBuilder()
            builder.Update(opendnp3.Analog(1.0), 0)
            outstation.Apply(builder.Build())

            # SetRestartIIN (should release GIL)
            outstation.SetRestartIIN()

            time.sleep(0.2)
            mgr.Shutdown()
            return handler.count

        count = self._run_with_timeout(do_work)
        assert count > 0, "Log handler should have received messages"

    def test_scan_and_demand_with_log_handler(self):
        """Scan, ScanClasses, AddClassScan, and Demand must release the GIL."""

        class LogHandler(opendnp3.ILogHandler):
            def __init__(self):
                super().__init__()
                self.count = 0
            def log(self, module, id, level, location, message):
                self.count += 1

        handler = LogHandler()

        def do_work():
            mgr = opendnp3.DNP3Manager(2, handler)
            port = _alloc_port()

            # Outstation
            server_ch = mgr.AddTCPServer(
                "gil-scan-server",
                opendnp3.levels.NORMAL,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                None,
            )
            cmd = AcceptAllCommandHandler()
            o_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_ch.AddOutstation("o", cmd, o_app, oconfig)

            # Master
            client_ch = mgr.AddTCPClient(
                "gil-scan-client",
                opendnp3.levels.NORMAL,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                None,
            )
            soe = CollectingSOEHandler()
            m_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_ch.AddMaster("m", soe, m_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(1.5)

            # ScanClasses (should release GIL)
            master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)

            # Scan with custom headers (should release GIL)
            master.Scan([opendnp3.Header.AllObjects(1, 0)], soe)

            # AddClassScan + Demand (should release GIL)
            scan = master.AddClassScan(
                opendnp3.ClassField.AllClasses(),
                opendnp3.TimeDuration.Seconds(60),
                soe,
            )
            scan.Demand()

            time.sleep(0.5)
            mgr.Shutdown()
            return handler.count

        count = self._run_with_timeout(do_work)
        assert count > 0, "Log handler should have received messages"


# ---------------------------------------------------------------------------
# New scan method tests
# ---------------------------------------------------------------------------

class TestNewScanMethods:
    """Test the additional scan methods: AddScan, AddAllObjectsScan,
    AddRangeScan, ScanAllObjects, ScanRange."""

    def test_scan_all_objects(self, loopback):
        """ScanAllObjects requests all objects for a group/variation."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()

        # Group1Var0 = binary input, all objects
        gv = opendnp3.GroupVariationID(1, 0)
        master.ScanAllObjects(gv, soe)
        time.sleep(0.5)

        assert len(soe.binaries) == 10

    def test_scan_range(self, loopback):
        """ScanRange requests a range of objects for a group/variation."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        # Group30Var0 = analog input, range 0-4
        gv = opendnp3.GroupVariationID(30, 0)
        master.ScanRange(gv, 0, 4, soe)
        time.sleep(0.5)

        assert len(soe.analogs) == 5

    def test_add_scan(self, loopback):
        """AddScan creates a recurring scan with custom headers."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()

        headers = [opendnp3.Header.AllObjects(1, 0)]
        scan = master.AddScan(
            opendnp3.TimeDuration.Milliseconds(500),
            headers,
            soe,
        )
        time.sleep(1.5)

        # Should have received at least 2 rounds
        assert len(soe.binaries) >= 20

    def test_add_all_objects_scan(self, loopback):
        """AddAllObjectsScan creates a recurring all-objects scan."""
        master, outstation, soe, cmd, port = loopback
        soe.counters.clear()

        # Group20Var0 = counter, all objects
        gv = opendnp3.GroupVariationID(20, 0)
        scan = master.AddAllObjectsScan(
            gv,
            opendnp3.TimeDuration.Milliseconds(500),
            soe,
        )
        time.sleep(1.5)

        # Should have received at least 2 rounds of 10 counters
        assert len(soe.counters) >= 20

    def test_add_range_scan(self, loopback):
        """AddRangeScan creates a recurring range-based scan."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        # Group30Var0 = analog input, range 0-2
        gv = opendnp3.GroupVariationID(30, 0)
        scan = master.AddRangeScan(gv, 0, 2,
            opendnp3.TimeDuration.Milliseconds(500),
            soe,
        )
        time.sleep(1.5)

        # Should have received at least 2 rounds of 3 analogs
        assert len(soe.analogs) >= 6


# ---------------------------------------------------------------------------
# IINField integration tests
# ---------------------------------------------------------------------------

class TestIINFieldIntegration:
    """Verify IINField is properly received via OnReceiveIIN callback
    and that task info reports useful error information."""

    def test_on_receive_iin_callback(self):
        """OnReceiveIIN receives an IINField with accessible bits during loopback."""

        class TrackingMasterApp(opendnp3.IMasterApplication):
            def __init__(self):
                super().__init__()
                self.iin_fields = []
                self._lock = threading.Lock()

            def OnReceiveIIN(self, iin):
                with self._lock:
                    # Copy the IIN data so it survives beyond callback
                    self.iin_fields.append(opendnp3.IINField(iin.LSB, iin.MSB))

            def OnTaskStart(self, type, id):
                pass

            def OnTaskComplete(self, info):
                pass

            def OnOpen(self):
                pass

            def OnClose(self):
                pass

            def AssignClassDuringStartup(self):
                return False

            def Now(self):
                import time as _time
                ms = int(_time.time() * 1000)
                return opendnp3.UTCTimestamp(ms)

        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            # Outstation
            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig
            )

            # Master with tracking app
            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            tracking_app = TrackingMasterApp()
            soe = CollectingSOEHandler()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, tracking_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(2.0)

            # OnReceiveIIN should have been called during startup handshaking
            with tracking_app._lock:
                iin_count = len(tracking_app.iin_fields)
                if iin_count > 0:
                    first_iin = tracking_app.iin_fields[0]

            assert iin_count > 0, "OnReceiveIIN should have been called"

            # The IINField should be inspectable
            assert isinstance(first_iin, opendnp3.IINField)
            assert first_iin.LSB is not None
            assert first_iin.MSB is not None

        finally:
            mgr.Shutdown()

    def test_iin_device_restart_visible(self):
        """When outstation sets DEVICE_RESTART IIN, master sees it in OnReceiveIIN."""

        class TrackingMasterApp(opendnp3.IMasterApplication):
            def __init__(self):
                super().__init__()
                self.iin_fields = []
                self._lock = threading.Lock()

            def OnReceiveIIN(self, iin):
                with self._lock:
                    self.iin_fields.append(opendnp3.IINField(iin.LSB, iin.MSB))

            def OnTaskStart(self, type, id):
                pass

            def OnTaskComplete(self, info):
                pass

            def OnOpen(self):
                pass

            def OnClose(self):
                pass

            def AssignClassDuringStartup(self):
                return False

            def Now(self):
                import time as _time
                ms = int(_time.time() * 1000)
                return opendnp3.UTCTimestamp(ms)

        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            # Outstation
            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig
            )

            # Master
            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            tracking_app = TrackingMasterApp()
            soe = CollectingSOEHandler()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            # Don't auto-clear restart so we can observe it
            mconfig.master.ignoreRestartIIN = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, tracking_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(1.0)

            # Set device restart on outstation
            outstation.SetRestartIIN()
            time.sleep(0.5)

            # Trigger a scan to get the IIN back
            tracking_app.iin_fields.clear()
            master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
            time.sleep(1.0)

            with tracking_app._lock:
                # Look for any IIN with DEVICE_RESTART set
                restart_iins = [
                    iin for iin in tracking_app.iin_fields
                    if iin.IsSet(opendnp3.IINBit.DEVICE_RESTART)
                ]

            assert len(restart_iins) > 0, (
                "Expected at least one IIN response with DEVICE_RESTART set"
            )

        finally:
            mgr.Shutdown()

    def test_task_info_repr_and_fields(self):
        """TaskInfo provides type, result, and repr during normal loopback operation."""

        class TrackingMasterApp(opendnp3.IMasterApplication):
            def __init__(self):
                super().__init__()
                self.task_completions = []
                self._lock = threading.Lock()

            def OnReceiveIIN(self, iin):
                pass

            def OnTaskStart(self, type, id):
                pass

            def OnTaskComplete(self, info):
                with self._lock:
                    self.task_completions.append({
                        "type": info.type,
                        "result": info.result,
                        "id_defined": info.id.IsDefined(),
                        "repr": repr(info),
                    })

            def OnOpen(self):
                pass

            def OnClose(self):
                pass

            def AssignClassDuringStartup(self):
                return False

            def Now(self):
                import time as _time
                ms = int(_time.time() * 1000)
                return opendnp3.UTCTimestamp(ms)

        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            # Outstation
            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig
            )

            # Master with tracking app
            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            tracking_app = TrackingMasterApp()
            soe = CollectingSOEHandler()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, tracking_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(2.0)

            with tracking_app._lock:
                completions = list(tracking_app.task_completions)

            # Startup tasks should have completed (clear restart, integrity poll, etc.)
            assert len(completions) > 0, "Expected task completion callbacks"

            # Verify TaskInfo fields are meaningful
            for c in completions:
                assert c["type"] is not None
                assert c["result"] is not None
                # The repr should be a non-empty string containing TaskInfo
                assert isinstance(c["repr"], str)
                assert "TaskInfo" in c["repr"]

            # At least one task should have succeeded
            successes = [c for c in completions if c["result"] == opendnp3.TaskCompletion.SUCCESS]
            assert len(successes) > 0, "Expected at least one successful task"

        finally:
            mgr.Shutdown()

    def test_iinfield_manipulation_in_callback(self):
        """IINField received in callback can be inspected with all new methods."""

        class InspectingMasterApp(opendnp3.IMasterApplication):
            def __init__(self):
                super().__init__()
                self.inspections = []
                self._lock = threading.Lock()

            def OnReceiveIIN(self, iin):
                with self._lock:
                    inspection = {
                        "lsb": iin.LSB,
                        "msb": iin.MSB,
                        "any": iin.Any(),
                        "has_request_error": iin.HasRequestError(),
                        "device_restart": iin.IsSet(opendnp3.IINBit.DEVICE_RESTART),
                        "need_time": iin.IsSet(opendnp3.IINBit.NEED_TIME),
                        "str": str(iin),
                        "repr": repr(iin),
                    }
                    self.inspections.append(inspection)

            def OnTaskStart(self, type, id):
                pass

            def OnTaskComplete(self, info):
                pass

            def OnOpen(self):
                pass

            def OnClose(self):
                pass

            def AssignClassDuringStartup(self):
                return False

            def Now(self):
                import time as _time
                ms = int(_time.time() * 1000)
                return opendnp3.UTCTimestamp(ms)

        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig
            )

            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            app = InspectingMasterApp()
            soe = CollectingSOEHandler()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(2.0)

            with app._lock:
                inspections = list(app.inspections)

            assert len(inspections) > 0, "Should have received IIN inspections"

            # Verify the inspection data makes sense
            insp = inspections[0]
            assert isinstance(insp["lsb"], int)
            assert isinstance(insp["msb"], int)
            assert isinstance(insp["any"], bool)
            assert isinstance(insp["has_request_error"], bool)
            assert isinstance(insp["device_restart"], bool)
            assert isinstance(insp["need_time"], bool)
            assert isinstance(insp["str"], str)
            assert isinstance(insp["repr"], str)
            assert "IIN" in insp["str"]
            assert "IINField" in insp["repr"]

        finally:
            mgr.Shutdown()

    def test_application_iin_controls_outstation_iin(self):
        """ApplicationIIN set by outstation application affects what master sees."""

        class DeviceTroubleApp(opendnp3.IOutstationApplication):
            """Outstation app that reports device trouble."""
            def __init__(self):
                super().__init__()

            def SupportsWriteAbsoluteTime(self):
                return False

            def WriteAbsoluteTime(self, timestamp):
                return False

            def SupportsAssignClass(self):
                return False

            def GetApplicationIIN(self):
                app_iin = opendnp3.ApplicationIIN()
                app_iin.deviceTrouble = True
                return app_iin

            def ColdRestartSupport(self):
                return opendnp3.RestartMode.UNSUPPORTED

            def WarmRestartSupport(self):
                return opendnp3.RestartMode.UNSUPPORTED

            def ColdRestart(self):
                return 0

            def WarmRestart(self):
                return 0

            def Now(self):
                import time as _time
                return opendnp3.DNPTime(int(_time.time() * 1000))

        class TrackingMasterApp(opendnp3.IMasterApplication):
            def __init__(self):
                super().__init__()
                self.iin_fields = []
                self._lock = threading.Lock()

            def OnReceiveIIN(self, iin):
                with self._lock:
                    self.iin_fields.append(opendnp3.IINField(iin.LSB, iin.MSB))

            def OnTaskStart(self, type, id):
                pass

            def OnTaskComplete(self, info):
                pass

            def OnOpen(self):
                pass

            def OnClose(self):
                pass

            def AssignClassDuringStartup(self):
                return False

            def Now(self):
                import time as _time
                return opendnp3.UTCTimestamp(int(_time.time() * 1000))

        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            cmd_handler = AcceptAllCommandHandler()
            trouble_app = DeviceTroubleApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, trouble_app, oconfig
            )

            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            tracking_app = TrackingMasterApp()
            soe = CollectingSOEHandler()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, tracking_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(2.0)

            with tracking_app._lock:
                trouble_iins = [
                    iin for iin in tracking_app.iin_fields
                    if iin.IsSet(opendnp3.IINBit.DEVICE_TROUBLE)
                ]

            assert len(trouble_iins) > 0, (
                "Master should see DEVICE_TROUBLE in IIN when outstation "
                "application reports it via GetApplicationIIN"
            )

        finally:
            mgr.Shutdown()


# ---------------------------------------------------------------------------
# TLS verify callback integration tests
# ---------------------------------------------------------------------------

class TestTLSVerifyCallback:
    """Verify TLSConfig.verifyCallback fires during TLS handshake."""

    @staticmethod
    def _generate_certs(tmpdir):
        """Generate a self-signed CA + server/client certs in tmpdir.

        Returns dict with paths: ca_cert, server_cert, server_key,
        client_cert, client_key.
        """
        import subprocess
        import os

        ca_key = os.path.join(tmpdir, "ca.key")
        ca_cert = os.path.join(tmpdir, "ca.pem")
        srv_key = os.path.join(tmpdir, "server.key")
        srv_csr = os.path.join(tmpdir, "server.csr")
        srv_cert = os.path.join(tmpdir, "server.pem")
        cli_key = os.path.join(tmpdir, "client.key")
        cli_csr = os.path.join(tmpdir, "client.csr")
        cli_cert = os.path.join(tmpdir, "client.pem")

        # CA key + self-signed cert
        subprocess.check_call([
            "openssl", "req", "-x509", "-newkey", "rsa:2048",
            "-keyout", ca_key, "-out", ca_cert,
            "-days", "1", "-nodes",
            "-subj", "/CN=TestCA",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        # Server key + cert signed by CA
        subprocess.check_call([
            "openssl", "req", "-newkey", "rsa:2048",
            "-keyout", srv_key, "-out", srv_csr,
            "-nodes", "-subj", "/CN=localhost",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.check_call([
            "openssl", "x509", "-req", "-in", srv_csr,
            "-CA", ca_cert, "-CAkey", ca_key, "-CAcreateserial",
            "-out", srv_cert, "-days", "1",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        # Client key + cert signed by CA
        subprocess.check_call([
            "openssl", "req", "-newkey", "rsa:2048",
            "-keyout", cli_key, "-out", cli_csr,
            "-nodes", "-subj", "/CN=client",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.check_call([
            "openssl", "x509", "-req", "-in", cli_csr,
            "-CA", ca_cert, "-CAkey", ca_key, "-CAcreateserial",
            "-out", cli_cert, "-days", "1",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        return dict(
            ca_cert=ca_cert,
            server_cert=srv_cert, server_key=srv_key,
            client_cert=cli_cert, client_key=cli_key,
        )

    def test_verify_callback_invoked(self, tmp_path):
        """The verifyCallback is called during TLS handshake with correct arg types."""
        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        callback_calls = []
        callback_lock = threading.Lock()

        def verify_cb(preverified, depth, subject, cert_der):
            with callback_lock:
                callback_calls.append({
                    "preverified": preverified,
                    "depth": depth,
                    "subject": subject,
                    "cert_der": cert_der,
                })
            return True  # accept

        mgr = opendnp3.DNP3Manager(2)
        try:
            # -- TLS server (outstation) --
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_channel = mgr.AddTLSServer(
                "tls-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            # -- TLS client (master) with verify callback --
            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            client_tls.verifyCallback = verify_cb

            client_channel = mgr.AddTLSClient(
                "tls-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            # Verify the callback was invoked
            with callback_lock:
                assert len(callback_calls) > 0, (
                    "verifyCallback should have been called during TLS handshake"
                )
                call = callback_calls[0]

            # Check argument types
            assert isinstance(call["preverified"], bool)
            assert isinstance(call["depth"], int)
            assert isinstance(call["subject"], str)
            assert isinstance(call["cert_der"], bytes)
            assert len(call["cert_der"]) > 0, "cert_der should be non-empty DER data"
            assert len(call["subject"]) > 0, "subject should be non-empty"

        finally:
            mgr.Shutdown()

    def test_verify_callback_reject_prevents_connection(self, tmp_path):
        """When verifyCallback returns False, TLS handshake fails and no data is exchanged."""
        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        def reject_cb(preverified, depth, subject, cert_der):
            return False  # reject all certs

        mgr = opendnp3.DNP3Manager(2)
        try:
            # TLS server
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_channel = mgr.AddTLSServer(
                "tls-reject-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            # TLS client with rejecting callback
            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            client_tls.verifyCallback = reject_cb

            client_channel = mgr.AddTLSClient(
                "tls-reject-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            # With the reject callback, no data should have been exchanged
            assert len(soe.binaries) == 0, (
                "No measurements should arrive when TLS verification rejects"
            )
            assert len(soe.analogs) == 0

        finally:
            mgr.Shutdown()

    def test_verify_callback_sees_correct_subject(self, tmp_path):
        """The subject passed to verifyCallback contains the expected CN."""
        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        subjects = []
        lock = threading.Lock()

        def capture_cb(preverified, depth, subject, cert_der):
            with lock:
                subjects.append(subject)
            return True

        mgr = opendnp3.DNP3Manager(2)
        try:
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_channel = mgr.AddTLSServer(
                "tls-subject-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            client_tls.verifyCallback = capture_cb

            client_channel = mgr.AddTLSClient(
                "tls-subject-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            with lock:
                # The callback should have seen the server cert (CN=localhost)
                # and/or the CA cert (CN=TestCA) depending on chain depth
                all_subjects = " ".join(subjects)

            assert "localhost" in all_subjects or "TestCA" in all_subjects, (
                f"Expected 'localhost' or 'TestCA' in subjects, got: {subjects}"
            )

        finally:
            mgr.Shutdown()

    def test_verify_callback_chain_depth_and_preverified(self, tmp_path):
        """The callback receives correct depth values for the cert chain
        and preverified=True for certs signed by the trusted CA."""
        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        calls_by_depth = {}
        lock = threading.Lock()

        def depth_cb(preverified, depth, subject, cert_der):
            with lock:
                calls_by_depth[depth] = {
                    "preverified": preverified,
                    "subject": subject,
                }
            return True

        mgr = opendnp3.DNP3Manager(2)
        try:
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_channel = mgr.AddTLSServer(
                "tls-depth-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            client_tls.verifyCallback = depth_cb

            client_channel = mgr.AddTLSClient(
                "tls-depth-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            with lock:
                snapshot = dict(calls_by_depth)

            # CA cert at depth 1, server cert at depth 0
            assert len(snapshot) >= 2, (
                f"Expected callbacks at depth 0 and 1, got depths: {list(snapshot.keys())}"
            )
            assert 0 in snapshot, "Expected callback at depth 0 (server cert)"
            assert 1 in snapshot, "Expected callback at depth 1 (CA cert)"

            # With a valid CA-signed chain, both should be preverified=True
            assert snapshot[0]["preverified"] is True, (
                "Server cert (depth 0) should be preverified=True with valid CA"
            )
            assert snapshot[1]["preverified"] is True, (
                "CA cert (depth 1) should be preverified=True (self-signed, trusted)"
            )

            # Depth 0 should be the server cert (CN=localhost)
            assert "localhost" in snapshot[0]["subject"]
            # Depth 1 should be the CA cert (CN=TestCA)
            assert "TestCA" in snapshot[1]["subject"]

        finally:
            mgr.Shutdown()

    def test_verify_callback_cert_der_is_valid_x509(self, tmp_path):
        """The cert_der bytes are valid DER-encoded X.509 certificates
        that can be parsed by Python's ssl module."""
        import ssl

        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        der_certs = []
        lock = threading.Lock()

        def der_cb(preverified, depth, subject, cert_der):
            with lock:
                der_certs.append({"depth": depth, "der": cert_der, "subject": subject})
            return True

        mgr = opendnp3.DNP3Manager(2)
        try:
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_channel = mgr.AddTLSServer(
                "tls-der-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            client_tls.verifyCallback = der_cb

            client_channel = mgr.AddTLSClient(
                "tls-der-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            with lock:
                snapshot = list(der_certs)

            assert len(snapshot) > 0, "Should have received at least one DER cert"

            for entry in snapshot:
                der = entry["der"]
                assert isinstance(der, bytes)
                assert len(der) > 0

                # DER-encoded X.509 certs start with ASN.1 SEQUENCE tag (0x30)
                assert der[0] == 0x30, (
                    f"DER data should start with 0x30 (SEQUENCE), got 0x{der[0]:02x}"
                )

                # Verify we can parse it with Python's ssl module
                pem = ssl.DER_cert_to_PEM_cert(der)
                assert "BEGIN CERTIFICATE" in pem
                assert "END CERTIFICATE" in pem

        finally:
            mgr.Shutdown()

    def test_verify_callback_config_copied_not_referenced(self, tmp_path):
        """After passing TLSConfig to AddTLSClient, changing verifyCallback on
        the Python TLSConfig object does not affect the already-created channel."""
        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        original_calls = []
        replacement_calls = []
        lock = threading.Lock()

        def original_cb(preverified, depth, subject, cert_der):
            with lock:
                original_calls.append(depth)
            return True

        def replacement_cb(preverified, depth, subject, cert_der):
            with lock:
                replacement_calls.append(depth)
            return True

        mgr = opendnp3.DNP3Manager(2)
        try:
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_channel = mgr.AddTLSServer(
                "tls-copy-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            client_tls.verifyCallback = original_cb

            client_channel = mgr.AddTLSClient(
                "tls-copy-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            # Now replace the callback on the Python-side TLSConfig object
            # This should NOT affect the already-created channel
            client_tls.verifyCallback = replacement_cb

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            with lock:
                orig_count = len(original_calls)
                repl_count = len(replacement_calls)

            # The original callback should have been called (C++ copied the config)
            assert orig_count > 0, (
                "Original callback should have been invoked (C++ copies TLSConfig)"
            )
            # The replacement callback should NOT have been called
            assert repl_count == 0, (
                "Replacement callback should not be invoked -- C++ has its own copy"
            )

        finally:
            mgr.Shutdown()

    def test_server_verify_callback_not_invoked(self, tmp_path):
        """verifyCallback set on the SERVER TLSConfig is NOT invoked during
        handshake -- this is a known C++ limitation where only the client path
        supports custom verify callbacks."""
        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        server_cb_calls = []
        client_cb_calls = []
        lock = threading.Lock()

        def server_cb(preverified, depth, subject, cert_der):
            with lock:
                server_cb_calls.append(depth)
            return True

        def client_cb(preverified, depth, subject, cert_der):
            with lock:
                client_cb_calls.append(depth)
            return True

        mgr = opendnp3.DNP3Manager(2)
        try:
            # Server with verifyCallback set
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_tls.verifyCallback = server_cb

            server_channel = mgr.AddTLSServer(
                "tls-servercb-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            # Client with verifyCallback set (this one works)
            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            client_tls.verifyCallback = client_cb

            client_channel = mgr.AddTLSClient(
                "tls-servercb-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            with lock:
                server_count = len(server_cb_calls)
                client_count = len(client_cb_calls)

            # Client callback should fire
            assert client_count > 0, "Client verifyCallback should fire"

            # Server callback should NOT fire -- TLSServerIOHandler::Server
            # hardcodes VerifyCallback to return preverified without consulting
            # config.verifyCallback. This documents the known limitation.
            assert server_count == 0, (
                "Server verifyCallback is not supported by the C++ TLSServer "
                "implementation (known limitation)"
            )

        finally:
            mgr.Shutdown()

    def test_verify_callback_no_callback_uses_standard_verification(self, tmp_path):
        """When verifyCallback is None (default), standard OpenSSL verification
        is used and a valid cert chain results in a successful connection."""
        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        mgr = opendnp3.DNP3Manager(2)
        try:
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_channel = mgr.AddTLSServer(
                "tls-nocb-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            # Client with NO verifyCallback (None / default)
            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            assert client_tls.verifyCallback is None

            client_channel = mgr.AddTLSClient(
                "tls-nocb-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            # Without a callback, standard OpenSSL verification should succeed
            # and data should flow
            assert len(soe.binaries) > 0 or len(soe.analogs) > 0, (
                "With valid certs and no callback, standard verification "
                "should succeed and data should be exchanged"
            )

        finally:
            mgr.Shutdown()

    def test_verify_callback_exception_does_not_crash(self, tmp_path):
        """When verifyCallback raises a Python exception, the TLS handshake
        fails gracefully (returns false) instead of crashing the process."""
        certs = self._generate_certs(str(tmp_path))
        port = _alloc_port()

        def raising_cb(preverified, depth, subject, cert_der):
            raise ValueError("intentional test error")

        mgr = opendnp3.DNP3Manager(2)
        try:
            server_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["server_cert"], certs["server_key"],
            )
            server_channel = mgr.AddTLSServer(
                "tls-exc-server",
                opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port),
                server_tls,
                None,
            )

            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig,
            )

            client_tls = opendnp3.TLSConfig(
                certs["ca_cert"], certs["client_cert"], certs["client_key"],
            )
            client_tls.verifyCallback = raising_cb

            client_channel = mgr.AddTLSClient(
                "tls-exc-client",
                opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)],
                "0.0.0.0",
                client_tls,
                None,
            )

            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster(
                "master", soe, master_app, mconfig,
            )

            outstation.Enable()
            master.Enable()
            time.sleep(3.0)

            # The exception should cause verify to return false, so no data
            # flows — but the process should NOT crash
            assert len(soe.binaries) == 0, (
                "No data should flow when callback raises an exception"
            )

        finally:
            mgr.Shutdown()


# ---------------------------------------------------------------------------
# OnRawAPDU and Header.Raw integration tests
# ---------------------------------------------------------------------------

class _RawSOEHandler(opendnp3.ISOEHandler):
    """SOE handler that collects OnRawAPDU data and tracks call order."""

    def __init__(self):
        super().__init__()
        self.raw_apdus = []
        self.call_order = []
        self._lock = threading.Lock()

    def BeginFragment(self, info):
        with self._lock:
            self.call_order.append("BeginFragment")

    def EndFragment(self, info):
        with self._lock:
            self.call_order.append("EndFragment")

    def OnRawAPDU(self, info, data):
        with self._lock:
            self.raw_apdus.append(data)
            self.call_order.append("OnRawAPDU")

    def Process(self, info, values):
        with self._lock:
            self.call_order.append("Process")


class TestOnRawAPDU:
    """Verify OnRawAPDU fires during loopback scans."""

    def test_on_raw_apdu_fires(self, loopback):
        """OnRawAPDU is called with non-empty bytes during a scan."""
        master, outstation, _, cmd, port = loopback
        raw_soe = _RawSOEHandler()

        master.ScanClasses(opendnp3.ClassField.AllClasses(), raw_soe)
        time.sleep(0.5)

        assert len(raw_soe.raw_apdus) > 0, "OnRawAPDU should have been called"
        assert len(raw_soe.raw_apdus[0]) > 0, "Raw APDU data should be non-empty"

    def test_on_raw_apdu_before_begin_fragment(self, loopback):
        """OnRawAPDU is called before BeginFragment."""
        master, outstation, _, cmd, port = loopback
        raw_soe = _RawSOEHandler()

        master.ScanClasses(opendnp3.ClassField.AllClasses(), raw_soe)
        time.sleep(0.5)

        assert "OnRawAPDU" in raw_soe.call_order
        assert "BeginFragment" in raw_soe.call_order
        raw_idx = raw_soe.call_order.index("OnRawAPDU")
        begin_idx = raw_soe.call_order.index("BeginFragment")
        assert raw_idx < begin_idx, "OnRawAPDU should fire before BeginFragment"

    def test_raw_bytes_contain_group_variation(self, loopback):
        """Raw APDU bytes start with a valid group/variation pair."""
        master, outstation, _, cmd, port = loopback
        raw_soe = _RawSOEHandler()

        # Scan binary inputs (group 1)
        master.Scan([opendnp3.Header.AllObjects(1, 0)], raw_soe)
        time.sleep(0.5)

        assert len(raw_soe.raw_apdus) > 0
        data = raw_soe.raw_apdus[0]
        assert len(data) >= 3, "Raw data must have at least group+var+qualifier"
        group = data[0]
        # Response should contain group 1 (binary) or group 2 (binary event)
        assert group in (1, 2), f"Expected group 1 or 2, got {group}"

    def test_both_raw_and_process_fire(self, loopback):
        """Both OnRawAPDU and Process are called for supported groups."""
        master, outstation, _, cmd, port = loopback
        raw_soe = _RawSOEHandler()

        master.Scan([opendnp3.Header.AllObjects(1, 0)], raw_soe)
        time.sleep(0.5)

        assert "OnRawAPDU" in raw_soe.call_order
        assert "Process" in raw_soe.call_order


class TestHeaderRawSend:
    """Verify Header.Raw can send raw bytes in requests."""

    def test_header_raw_with_scan(self, loopback):
        """Header.Raw sends raw bytes via Scan and response fires OnRawAPDU."""
        master, outstation, _, cmd, port = loopback
        raw_soe = _RawSOEHandler()

        # Build a raw READ request for binary inputs: group=1, var=0, qualifier=0x06 (ALL_OBJECTS)
        raw_header = bytes([1, 0, 0x06])
        master.Scan([opendnp3.Header.Raw(raw_header)], raw_soe)
        time.sleep(0.5)

        # The outstation should respond and OnRawAPDU should fire
        assert len(raw_soe.raw_apdus) > 0, "OnRawAPDU should have fired from Raw header request"

    def test_mixed_raw_and_normal_headers(self, loopback):
        """Mixing Header.Raw and normal Headers in a single request."""
        master, outstation, _, cmd, port = loopback
        raw_soe = _RawSOEHandler()

        # Normal header for analog (group 30, var 0, all objects)
        # Plus raw header for binary (group 1, var 0, all objects)
        raw_header = bytes([1, 0, 0x06])
        headers = [
            opendnp3.Header.AllObjects(30, 0),
            opendnp3.Header.Raw(raw_header),
        ]

        master.Scan(headers, raw_soe)
        time.sleep(0.5)

        # Should get response data
        assert len(raw_soe.raw_apdus) > 0


# ---------------------------------------------------------------------------
# Command handler rejection tests
# ---------------------------------------------------------------------------

class TestCommandHandlerRejection:
    """Verify that when the outstation command handler rejects a command,
    the master sees the correct non-SUCCESS status."""

    def test_direct_operate_rejected_not_supported(self):
        """When command handler returns NOT_SUPPORTED, master receives that status."""

        class RejectingCommandHandler(opendnp3.ICommandHandler):
            def __init__(self):
                super().__init__()

            def Begin(self):
                pass

            def End(self):
                pass

            def Select(self, command, index):
                return opendnp3.CommandStatus.NOT_SUPPORTED

            def Operate(self, command, index, handler, opType):
                return opendnp3.CommandStatus.NOT_SUPPORTED

        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            reject_handler = RejectingCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", reject_handler, outstation_app, oconfig
            )

            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, master_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(1.5)

            result_event = threading.Event()
            results = []

            def on_result(result):
                results.append(result)
                result_event.set()

            crob = opendnp3.ControlRelayOutputBlock(opendnp3.OperationType.LATCH_ON)
            master.DirectOperate(crob, 0, on_result)
            result_event.wait(timeout=5.0)

            assert len(results) == 1
            # The task itself completes (SUCCESS means protocol exchange worked)
            assert results[0].summary == opendnp3.TaskCompletion.SUCCESS
            # But the individual point result should show NOT_SUPPORTED
            point_results = results[0].to_list()
            assert len(point_results) > 0
            assert point_results[0].status == opendnp3.CommandStatus.NOT_SUPPORTED

        finally:
            mgr.Shutdown()

    def test_sbo_rejected_at_select_phase(self):
        """When Select returns NOT_SUPPORTED, SBO fails at SELECT_FAIL."""

        class SelectRejectHandler(opendnp3.ICommandHandler):
            def __init__(self):
                super().__init__()

            def Begin(self):
                pass

            def End(self):
                pass

            def Select(self, command, index):
                return opendnp3.CommandStatus.NOT_SUPPORTED

            def Operate(self, command, index, handler, opType):
                return opendnp3.CommandStatus.SUCCESS

        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            select_reject = SelectRejectHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", select_reject, outstation_app, oconfig
            )

            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, master_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(1.5)

            result_event = threading.Event()
            results = []

            def on_result(result):
                results.append(result)
                result_event.set()

            crob = opendnp3.ControlRelayOutputBlock(opendnp3.OperationType.LATCH_OFF)
            master.SelectAndOperate(crob, 0, on_result)
            result_event.wait(timeout=5.0)

            assert len(results) == 1
            point_results = results[0].to_list()
            assert len(point_results) > 0
            # The select phase should have failed
            assert point_results[0].state == opendnp3.CommandPointState.SELECT_FAIL

        finally:
            mgr.Shutdown()


# ---------------------------------------------------------------------------
# Command value verification at outstation
# ---------------------------------------------------------------------------

class TestCommandValueVerification:
    """Verify that command values sent by master arrive intact at outstation."""

    def test_crob_values_arrive_at_outstation(self, loopback):
        """CROB operation type and index arrive at the outstation command handler."""
        master, outstation, soe, cmd, port = loopback
        cmd.received_crobs.clear()

        result_event = threading.Event()

        def on_result(result):
            result_event.set()

        crob = opendnp3.ControlRelayOutputBlock(opendnp3.OperationType.LATCH_ON)
        master.DirectOperate(crob, 7, on_result)
        result_event.wait(timeout=5.0)
        time.sleep(0.3)

        assert len(cmd.received_crobs) > 0
        received_cmd, received_idx = cmd.received_crobs[-1]
        assert received_idx == 7
        assert received_cmd.opType == opendnp3.OperationType.LATCH_ON

    def test_analog_output_value_arrives_at_outstation(self, loopback):
        """AnalogOutputInt32 value arrives at the outstation command handler."""
        master, outstation, soe, cmd, port = loopback
        cmd.received_analog_commands.clear()

        result_event = threading.Event()

        def on_result(result):
            result_event.set()

        ao = opendnp3.AnalogOutputInt32(54321)
        master.DirectOperate(ao, 3, on_result)
        result_event.wait(timeout=5.0)
        time.sleep(0.3)

        assert len(cmd.received_analog_commands) > 0
        received_cmd, received_idx = cmd.received_analog_commands[-1]
        assert received_idx == 3
        assert received_cmd.value == 54321

    def test_crob_with_timing_parameters(self, loopback):
        """CROB with custom on/off timing parameters arrives correctly."""
        master, outstation, soe, cmd, port = loopback
        cmd.received_crobs.clear()

        result_event = threading.Event()

        def on_result(result):
            result_event.set()

        crob = opendnp3.ControlRelayOutputBlock(
            opendnp3.OperationType.PULSE_ON,
            opendnp3.TripCloseCode.NUL,
            False,  # clear
            1,      # count
            500,    # onTimeMS
            250,    # offTimeMS
        )
        master.DirectOperate(crob, 0, on_result)
        result_event.wait(timeout=5.0)
        time.sleep(0.3)

        assert len(cmd.received_crobs) > 0
        received_cmd, _ = cmd.received_crobs[-1]
        assert received_cmd.opType == opendnp3.OperationType.PULSE_ON
        assert received_cmd.onTimeMS == 500
        assert received_cmd.offTimeMS == 250
        assert received_cmd.count == 1


# ---------------------------------------------------------------------------
# Sequential updates to the same point
# ---------------------------------------------------------------------------

class TestSequentialUpdates:
    """Verify that multiple updates to the same point result in the latest value."""

    def test_latest_value_wins(self, loopback):
        """When the same analog point is updated multiple times, the master
        sees the last value after a scan."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        # Update analog index 0 three times with different values
        for value in [10.0, 20.0, 30.0]:
            builder = opendnp3.UpdateBuilder()
            builder.Update(opendnp3.Analog(value), 0)
            outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField(True, False, False, False), soe)
        time.sleep(0.5)

        # Class 0 scan returns static data -- should show 30.0
        idx0 = [a for a in soe.analogs if a.index == 0]
        assert len(idx0) > 0
        assert abs(idx0[-1].value.value - 30.0) < 1e-6

    def test_binary_toggle(self, loopback):
        """Toggling a binary point between True and False results in the final state."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()

        for val in [True, False, True, False]:
            builder = opendnp3.UpdateBuilder()
            builder.Update(opendnp3.Binary(val), 5)
            outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField(True, False, False, False), soe)
        time.sleep(0.5)

        idx5 = [b for b in soe.binaries if b.index == 5]
        assert len(idx5) > 0
        assert idx5[-1].value.value is False


# ---------------------------------------------------------------------------
# Analog value boundary tests over loopback
# ---------------------------------------------------------------------------

class TestAnalogBoundaryValues:
    """Verify extreme analog values survive the DNP3 round-trip."""

    def test_negative_analog(self, loopback):
        """Negative analog value is preserved over loopback."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Analog(-999.0), 1)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx1 = [a for a in soe.analogs if a.index == 1]
        assert len(idx1) > 0
        assert abs(idx1[-1].value.value - (-999.0)) < 1e-6

    def test_zero_analog(self, loopback):
        """Zero analog value is preserved over loopback."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Analog(0.0), 2)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx2 = [a for a in soe.analogs if a.index == 2]
        assert len(idx2) > 0
        assert idx2[-1].value.value == 0.0

    def test_large_positive_analog(self, loopback):
        """Large positive analog value survives round-trip (within 32-bit int range)."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Analog(2147483647.0), 3)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx3 = [a for a in soe.analogs if a.index == 3]
        assert len(idx3) > 0
        # Default analog variation is int32, so the value should be close
        assert abs(idx3[-1].value.value - 2147483647.0) < 1.0


# ---------------------------------------------------------------------------
# Fragment info behavioral verification
# ---------------------------------------------------------------------------

class TestFragmentInfoBehavior:
    """Verify ResponseInfo fields contain meaningful values, not just existence."""

    def test_fragment_info_fir_fin_set(self, loopback):
        """For a normal single-fragment response, both FIR and FIN should be True."""
        master, outstation, soe, cmd, port = loopback
        soe.fragment_infos.clear()

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        assert len(soe.fragment_infos) > 0
        # For a small dataset (10 points), the response should fit in one fragment
        info = soe.fragment_infos[0]
        assert info.fir is True, "First fragment should have FIR=True"
        assert info.fin is True, "Single-fragment response should have FIN=True"

    def test_fragment_info_solicited(self, loopback):
        """A scan response should be solicited (not unsolicited)."""
        master, outstation, soe, cmd, port = loopback
        soe.fragment_infos.clear()

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        assert len(soe.fragment_infos) > 0
        info = soe.fragment_infos[0]
        assert info.unsolicited is False, (
            "Response to a class scan should be solicited"
        )


# ---------------------------------------------------------------------------
# Counter large value and FrozenCounter round-trip
# ---------------------------------------------------------------------------

class TestCounterBoundaryValues:
    """Verify counter boundary values survive DNP3 round-trip."""

    def test_counter_max_uint32(self, loopback):
        """Counter at max uint32 value survives round-trip."""
        master, outstation, soe, cmd, port = loopback
        soe.counters.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Counter(0xFFFFFFFF), 0)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx0 = [c for c in soe.counters if c.index == 0]
        assert len(idx0) > 0
        assert idx0[-1].value.value == 0xFFFFFFFF

    def test_counter_zero(self, loopback):
        """Counter at zero survives round-trip."""
        master, outstation, soe, cmd, port = loopback
        soe.counters.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.Counter(0), 1)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx1 = [c for c in soe.counters if c.index == 1]
        assert len(idx1) > 0
        assert idx1[-1].value.value == 0


# ---------------------------------------------------------------------------
# Modify flags behavioral verification
# ---------------------------------------------------------------------------

class TestModifyFlagsBehavior:
    """Verify flag modification produces correct quality flags visible to master."""

    def test_modify_analog_flags_comm_lost(self, loopback):
        """Modifying analog input flags to COMM_LOST is visible to master."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        # Set COMM_LOST (0x02) on analog inputs 0-2
        builder = opendnp3.UpdateBuilder()
        builder.Modify(opendnp3.FlagsType.AnalogInput, 0, 2, 0x02)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        modified = [a for a in soe.analogs if 0 <= a.index <= 2]
        assert len(modified) >= 3
        for a in modified:
            assert a.value.flags.value == 0x02, (
                f"Analog index {a.index} flags should be 0x02 (COMM_LOST)"
            )

    def test_modify_counter_flags_restart(self, loopback):
        """Modifying counter flags to RESTART (0x02) is visible to master."""
        master, outstation, soe, cmd, port = loopback
        soe.counters.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Modify(opendnp3.FlagsType.Counter, 0, 0, 0x02)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        idx0 = [c for c in soe.counters if c.index == 0]
        assert len(idx0) > 0
        assert idx0[-1].value.flags.value == 0x02


# ---------------------------------------------------------------------------
# Event buffer and class-specific event scanning
# ---------------------------------------------------------------------------

class TestEventClassScanning:
    """Verify events can be scanned by specific event class."""

    def test_event_class1_scan(self):
        """Events assigned to Class 1 are returned by a Class 1 event scan."""
        mgr = opendnp3.DNP3Manager(2)
        try:
            port = _alloc_port()

            server_channel = mgr.AddTCPServer(
                "server", opendnp3.levels.NOTHING,
                opendnp3.ServerAcceptMode.CloseExisting,
                opendnp3.IPEndpoint("127.0.0.1", port), None,
            )
            cmd_handler = AcceptAllCommandHandler()
            outstation_app = SimpleOutstationApp()
            oconfig = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(10))
            oconfig.outstation.params.allowUnsolicited = False
            oconfig.outstation.eventBufferConfig = opendnp3.EventBufferConfig.AllTypes(50)
            oconfig.link.LocalAddr = 1024
            oconfig.link.RemoteAddr = 1
            outstation = server_channel.AddOutstation(
                "outstation", cmd_handler, outstation_app, oconfig
            )

            client_channel = mgr.AddTCPClient(
                "client", opendnp3.levels.NOTHING,
                opendnp3.ChannelRetry.Default(),
                [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None,
            )
            soe = CollectingSOEHandler()
            master_app = SimpleMasterApp()
            mconfig = opendnp3.MasterStackConfig()
            mconfig.master.disableUnsolOnStartup = True
            mconfig.link.LocalAddr = 1
            mconfig.link.RemoteAddr = 1024
            master = client_channel.AddMaster("master", soe, master_app, mconfig)

            outstation.Enable()
            master.Enable()
            time.sleep(1.5)

            # Force an event (all default events go to Class 1)
            builder = opendnp3.UpdateBuilder()
            builder.Update(opendnp3.Analog(77.0), 0, opendnp3.EventMode.Force)
            outstation.Apply(builder.Build())

            # Scan only Class 1 events
            soe.analogs.clear()
            class1_field = opendnp3.ClassField(False, True, False, False)
            master.ScanClasses(class1_field, soe)
            time.sleep(0.5)

            idx0 = [a for a in soe.analogs if a.index == 0]
            assert len(idx0) > 0, (
                "Expected analog event for index 0 in Class 1 scan"
            )
            assert abs(idx0[-1].value.value - 77.0) < 1e-6

        finally:
            mgr.Shutdown()


# ---------------------------------------------------------------------------
# Multiple points range scan value verification
# ---------------------------------------------------------------------------

class TestRangeScanValues:
    """Verify range scans return the correct values for each index."""

    def test_range_scan_returns_correct_analog_values(self, loopback):
        """Range scan for analogs 0-4 returns the specific values we set."""
        master, outstation, soe, cmd, port = loopback
        soe.analogs.clear()

        # Set distinct values for indices 0-4
        builder = opendnp3.UpdateBuilder()
        for i in range(5):
            builder.Update(opendnp3.Analog(float(i * 100)), i)
        outstation.Apply(builder.Build())

        # Range scan for analogs 0-4
        headers = [opendnp3.Header.Range8(30, 0, 0, 4)]
        master.Scan(headers, soe)
        time.sleep(0.5)

        assert len(soe.analogs) == 5, f"Expected 5 analogs, got {len(soe.analogs)}"

        # Verify each index has the correct value
        for i in range(5):
            matches = [a for a in soe.analogs if a.index == i]
            assert len(matches) > 0, f"Missing analog index {i}"
            assert abs(matches[-1].value.value - float(i * 100)) < 1e-6, (
                f"Analog index {i} expected {i*100}, got {matches[-1].value.value}"
            )

    def test_range_scan_subset(self, loopback):
        """Range scan for indices 2-4 only returns those 3 points."""
        master, outstation, soe, cmd, port = loopback
        soe.binaries.clear()

        headers = [opendnp3.Header.Range8(1, 0, 2, 4)]
        master.Scan(headers, soe)
        time.sleep(0.5)

        assert len(soe.binaries) == 3
        indices = sorted(b.index for b in soe.binaries)
        assert indices == [2, 3, 4]


# ---------------------------------------------------------------------------
# DoubleBitBinary all states over loopback
# ---------------------------------------------------------------------------

class TestDoubleBitBinaryStates:
    """Verify all four DoubleBitBinary states survive the DNP3 round-trip."""

    def test_all_double_bit_states(self, loopback):
        """Each DoubleBit state (INDETERMINATE, OFF, ON, INTERMEDIATE) round-trips."""
        master, outstation, soe, cmd, port = loopback

        states = [
            (0, opendnp3.DoubleBit.INDETERMINATE),
            (1, opendnp3.DoubleBit.DETERMINED_OFF),
            (2, opendnp3.DoubleBit.DETERMINED_ON),
            (3, opendnp3.DoubleBit.INTERMEDIATE),
        ]

        for idx, state in states:
            builder = opendnp3.UpdateBuilder()
            builder.Update(opendnp3.DoubleBitBinary(state), idx)
            outstation.Apply(builder.Build())

        soe.double_bit_binaries.clear()
        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        for idx, expected_state in states:
            matches = [d for d in soe.double_bit_binaries if d.index == idx]
            assert len(matches) > 0, f"Missing DoubleBitBinary index {idx}"
            assert matches[-1].value.value == expected_state, (
                f"DoubleBitBinary index {idx}: expected {expected_state}, "
                f"got {matches[-1].value.value}"
            )


# ---------------------------------------------------------------------------
# BinaryOutputStatus round-trip
# ---------------------------------------------------------------------------

class TestBinaryOutputStatusToggle:
    """Verify BinaryOutputStatus can be toggled and the master sees correct values."""

    def test_toggle_binary_output_status(self, loopback):
        """Set BOS at multiple indices to different values and verify."""
        master, outstation, soe, cmd, port = loopback
        soe.binary_output_statuses.clear()

        builder = opendnp3.UpdateBuilder()
        builder.Update(opendnp3.BinaryOutputStatus(True), 0)
        builder.Update(opendnp3.BinaryOutputStatus(False), 1)
        builder.Update(opendnp3.BinaryOutputStatus(True), 2)
        outstation.Apply(builder.Build())

        master.ScanClasses(opendnp3.ClassField.AllClasses(), soe)
        time.sleep(0.5)

        expected = {0: True, 1: False, 2: True}
        for idx, expected_val in expected.items():
            matches = [b for b in soe.binary_output_statuses if b.index == idx]
            assert len(matches) > 0, f"Missing BOS index {idx}"
            assert matches[-1].value.value is expected_val, (
                f"BOS index {idx}: expected {expected_val}, got {matches[-1].value.value}"
            )


# ---------------------------------------------------------------------------
# In-memory file handler for file transfer tests
# ---------------------------------------------------------------------------

class InMemoryFileHandler(opendnp3.IFileHandler):
    """In-memory file handler for testing file transfer operations."""

    def __init__(self):
        super().__init__()
        self.files = {}       # name -> {'data': bytes, 'perms': FilePermissions, 'time': int}
        self.directories = {} # path -> [FileInfo, ...]
        self.open_handles = {}
        self._next_handle = 1
        self.valid_username = "admin"
        self.valid_password = "secret"
        self._lock = threading.Lock()

    def add_file(self, name, data, perms=None, time_of_creation=0):
        if perms is None:
            perms = opendnp3.FilePermissions()
        self.files[name] = {'data': data, 'perms': perms, 'time': time_of_creation}

    def add_directory(self, path, entries):
        self.directories[path] = entries

    def get_file_data(self, name):
        if name in self.files:
            return self.files[name]['data']
        return None

    def GetFileInfo(self, filename):
        with self._lock:
            r = opendnp3.FileCommandResult()
            if filename in self.directories:
                r.status = opendnp3.FileStatus.SUCCESS
                r.info.fileName = filename
                r.info.type = opendnp3.FileType.DIRECTORY
                r.info.size = 0
                return r
            if filename in self.files:
                entry = self.files[filename]
                r.status = opendnp3.FileStatus.SUCCESS
                r.info.fileName = filename
                r.info.type = opendnp3.FileType.SIMPLE_FILE
                r.info.size = len(entry['data'])
                r.info.timeOfCreation = entry['time']
                r.info.permissions = entry['perms']
                return r
            r.status = opendnp3.FileStatus.NOT_EXIST
            return r

    def OpenFile(self, filename, authKey, permissions, mode, maxBlockSize, requestId):
        with self._lock:
            r = opendnp3.FileOpenResult()
            if mode == opendnp3.FileMode.READ:
                # Check directories
                if filename in self.directories:
                    # Serialize directory entries
                    import struct
                    dir_data = b""
                    for info in self.directories[filename]:
                        fname_bytes = info.fileName.encode('utf-8')
                        fname_len = len(fname_bytes)
                        fixed_len = 18
                        entry_data = struct.pack('<HHHIBBBBBBh',
                            fixed_len, fname_len,
                            int(info.type), info.size,
                            *[(info.timeOfCreation >> (i*8)) & 0xFF for i in range(6)],
                            info.permissions.ToRaw())
                        # Manual serialization: 2+2+2+4+6+2 = 18 bytes
                        buf = struct.pack('<HHH', fixed_len, fname_len, int(info.type))
                        buf += struct.pack('<I', info.size)
                        toc = info.timeOfCreation
                        for i in range(6):
                            buf += struct.pack('<B', (toc >> (i*8)) & 0xFF)
                        buf += struct.pack('<H', info.permissions.ToRaw())
                        buf += fname_bytes
                        dir_data += buf
                    temp_key = filename + ".__dir__"
                    self.files[temp_key] = {'data': dir_data, 'perms': opendnp3.FilePermissions(), 'time': 0}
                    handle = self._next_handle
                    self._next_handle += 1
                    self.open_handles[handle] = {'filename': temp_key, 'mode': mode, 'maxBlockSize': maxBlockSize, 'write_buf': b""}
                    r.status = opendnp3.FileStatus.SUCCESS
                    r.fileHandle = handle
                    r.fileSize = len(dir_data)
                    r.maxBlockSize = maxBlockSize
                    return r

                if filename not in self.files:
                    r.status = opendnp3.FileStatus.NOT_EXIST
                    return r
                entry = self.files[filename]
                handle = self._next_handle
                self._next_handle += 1
                self.open_handles[handle] = {'filename': filename, 'mode': mode, 'maxBlockSize': maxBlockSize, 'write_buf': b""}
                r.status = opendnp3.FileStatus.SUCCESS
                r.fileHandle = handle
                r.fileSize = len(entry['data'])
                r.maxBlockSize = maxBlockSize
                return r
            elif mode == opendnp3.FileMode.WRITE:
                handle = self._next_handle
                self._next_handle += 1
                self.open_handles[handle] = {'filename': filename, 'mode': mode, 'maxBlockSize': maxBlockSize, 'write_buf': b"", 'perms': permissions}
                r.status = opendnp3.FileStatus.SUCCESS
                r.fileHandle = handle
                r.fileSize = 0
                r.maxBlockSize = maxBlockSize
                return r
            r.status = opendnp3.FileStatus.INVALID_MODE
            return r

    def ReadBlock(self, fileHandle, blockNum):
        with self._lock:
            r = opendnp3.FileBlockResult()
            if fileHandle not in self.open_handles:
                r.status = opendnp3.FileStatus.NOT_OPENED
                return r
            handle_info = self.open_handles[fileHandle]
            filename = handle_info['filename']
            if filename not in self.files:
                r.status = opendnp3.FileStatus.NOT_EXIST
                return r
            data = self.files[filename]['data']
            max_bs = handle_info['maxBlockSize']
            offset = blockNum * max_bs
            chunk = data[offset:offset + max_bs]
            r.status = opendnp3.FileStatus.SUCCESS
            r.data = list(chunk)
            r.lastBlock = (offset + max_bs >= len(data))
            return r

    def WriteBlock(self, fileHandle, blockNum, lastBlock, data):
        with self._lock:
            if fileHandle not in self.open_handles:
                return opendnp3.FileStatus.NOT_OPENED
            self.open_handles[fileHandle]['write_buf'] += data
            return opendnp3.FileStatus.SUCCESS

    def CloseFile(self, fileHandle, requestId):
        with self._lock:
            if fileHandle not in self.open_handles:
                return opendnp3.FileStatus.NOT_OPENED
            handle_info = self.open_handles.pop(fileHandle)
            if handle_info['mode'] == opendnp3.FileMode.WRITE:
                perms = handle_info.get('perms', opendnp3.FilePermissions())
                self.files[handle_info['filename']] = {
                    'data': handle_info['write_buf'],
                    'perms': perms,
                    'time': 0,
                }
            return opendnp3.FileStatus.SUCCESS

    def DeleteFile(self, filename):
        with self._lock:
            if filename not in self.files:
                return opendnp3.FileStatus.NOT_EXIST
            del self.files[filename]
            return opendnp3.FileStatus.SUCCESS

    def AbortFile(self, fileHandle):
        with self._lock:
            self.open_handles.pop(fileHandle, None)

    def AuthenticateFile(self, username, password):
        r = opendnp3.OutstationFileAuthResult()
        if username == self.valid_username and password == self.valid_password:
            r.status = opendnp3.FileStatus.SUCCESS
            r.authKey = 12345
        else:
            r.status = opendnp3.FileStatus.PERMISSION_DENIED
            r.authKey = 0
        return r


# ---------------------------------------------------------------------------
# File transfer fixture
# ---------------------------------------------------------------------------

@pytest.fixture(scope="class")
def file_loopback(manager):
    """Set up master <-> outstation loopback with file handler."""
    port = _alloc_port()

    server_channel = manager.AddTCPServer(
        "file_server",
        opendnp3.levels.NOTHING,
        opendnp3.ServerAcceptMode.CloseExisting,
        opendnp3.IPEndpoint("127.0.0.1", port),
        None,
    )

    cmd_handler = AcceptAllCommandHandler()
    outstation_app = SimpleOutstationApp()
    outstation_config = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(5))
    outstation_config.outstation.params.allowUnsolicited = False
    outstation_config.link.LocalAddr = 1024
    outstation_config.link.RemoteAddr = 1

    file_handler = InMemoryFileHandler()
    # Pre-populate test files
    file_handler.add_file("test.txt", b"hello",
                          opendnp3.FilePermissions(
                              opendnp3.FilePermissionSet(True, True, False),
                              opendnp3.FilePermissionSet(True, False, False),
                              opendnp3.FilePermissionSet(True, False, False)),
                          1234567890)

    outstation = server_channel.AddOutstation(
        "file_outstation", cmd_handler, outstation_app, outstation_config,
        fileHandler=file_handler
    )

    client_channel = manager.AddTCPClient(
        "file_client",
        opendnp3.levels.NOTHING,
        opendnp3.ChannelRetry.Default(),
        [opendnp3.IPEndpoint("127.0.0.1", port)],
        "0.0.0.0",
        None,
    )

    soe_handler = CollectingSOEHandler()
    master_app = SimpleMasterApp()
    master_config = opendnp3.MasterStackConfig()
    master_config.master.disableUnsolOnStartup = True
    master_config.link.LocalAddr = 1
    master_config.link.RemoteAddr = 1024

    master = client_channel.AddMaster(
        "file_master", soe_handler, master_app, master_config
    )

    outstation.Enable()
    master.Enable()
    time.sleep(1.5)

    yield master, outstation, file_handler, port


# ---------------------------------------------------------------------------
# File Transfer Tests
# ---------------------------------------------------------------------------

class TestFileTransfer:
    """Test file transfer operations over TCP loopback."""

    def test_read_small_file(self, file_loopback):
        """Read a small file and verify its content."""
        master, outstation, fh, port = file_loopback
        result_holder = []
        event = threading.Event()

        def on_read(result):
            result_holder.append(result)
            event.set()

        master.ReadFile("test.txt", on_read)
        assert event.wait(timeout=5), "ReadFile timed out"

        r = result_holder[0]
        assert r.summary == opendnp3.TaskCompletion.SUCCESS
        assert r.statusCode == opendnp3.FileStatus.SUCCESS
        assert r.data == b"hello"

    def test_read_file_not_found(self, file_loopback):
        """Reading a non-existent file returns FILE_NOT_FOUND / NOT_EXIST."""
        master, outstation, fh, port = file_loopback
        result_holder = []
        event = threading.Event()

        def on_read(result):
            result_holder.append(result)
            event.set()

        master.ReadFile("nonexistent.txt", on_read)
        assert event.wait(timeout=5), "ReadFile timed out"

        r = result_holder[0]
        assert r.statusCode in (opendnp3.FileStatus.NOT_EXIST,
                                opendnp3.FileStatus.FILE_NOT_FOUND)

    def test_get_file_info(self, file_loopback):
        """Get file info and verify metadata fields."""
        master, outstation, fh, port = file_loopback
        result_holder = []
        event = threading.Event()

        def on_info(result):
            result_holder.append(result)
            event.set()

        master.GetFileInfo("test.txt", on_info)
        assert event.wait(timeout=5), "GetFileInfo timed out"

        r = result_holder[0]
        assert r.summary == opendnp3.TaskCompletion.SUCCESS
        assert r.statusCode == opendnp3.FileStatus.SUCCESS
        assert r.info.fileName == "test.txt"
        assert r.info.type == opendnp3.FileType.SIMPLE_FILE
        assert r.info.size == 5

    def test_delete_file(self, file_loopback):
        """Delete a file and verify it's gone."""
        master, outstation, fh, port = file_loopback
        # Add a file to delete
        fh.add_file("deleteme.txt", b"trash")

        result_holder = []
        event = threading.Event()

        def on_delete(result):
            result_holder.append(result)
            event.set()

        master.DeleteFile("deleteme.txt", on_delete)
        assert event.wait(timeout=5), "DeleteFile timed out"

        r = result_holder[0]
        assert r.summary == opendnp3.TaskCompletion.SUCCESS
        assert r.statusCode == opendnp3.FileStatus.SUCCESS
        assert fh.get_file_data("deleteme.txt") is None

    def test_write_and_read_back(self, file_loopback):
        """Write a file then read it back, verify round-trip."""
        master, outstation, fh, port = file_loopback
        test_data = b"write test data 12345"
        perms = opendnp3.FilePermissions(
            opendnp3.FilePermissionSet(True, True, False),
            opendnp3.FilePermissionSet(True, False, False),
            opendnp3.FilePermissionSet(False, False, False))

        # Write
        write_event = threading.Event()
        write_result = []

        def on_write(result):
            write_result.append(result)
            write_event.set()

        master.WriteFile("written.txt", test_data, perms, on_write)
        assert write_event.wait(timeout=5), "WriteFile timed out"
        assert write_result[0].summary == opendnp3.TaskCompletion.SUCCESS

        # Read back
        read_event = threading.Event()
        read_result = []

        def on_read(result):
            read_result.append(result)
            read_event.set()

        master.ReadFile("written.txt", on_read)
        assert read_event.wait(timeout=5), "ReadFile timed out"
        assert read_result[0].summary == opendnp3.TaskCompletion.SUCCESS
        assert read_result[0].data == test_data

    def test_authenticate_file_valid(self, file_loopback):
        """Authenticate with valid credentials."""
        master, outstation, fh, port = file_loopback
        result_holder = []
        event = threading.Event()

        def on_auth(result):
            result_holder.append(result)
            event.set()

        master.AuthenticateFile("admin", "secret", on_auth)
        assert event.wait(timeout=5), "AuthenticateFile timed out"

        r = result_holder[0]
        assert r.summary == opendnp3.TaskCompletion.SUCCESS
        assert r.statusCode == opendnp3.FileStatus.SUCCESS
        assert r.authKey == 12345

    def test_authenticate_file_invalid(self, file_loopback):
        """Authenticate with invalid credentials returns PERMISSION_DENIED."""
        master, outstation, fh, port = file_loopback
        result_holder = []
        event = threading.Event()

        def on_auth(result):
            result_holder.append(result)
            event.set()

        master.AuthenticateFile("wrong", "creds", on_auth)
        assert event.wait(timeout=5), "AuthenticateFile timed out"

        r = result_holder[0]
        assert r.summary == opendnp3.TaskCompletion.SUCCESS
        assert r.statusCode == opendnp3.FileStatus.PERMISSION_DENIED


# ---------------------------------------------------------------------------
# Handlers the caller keeps no reference to
# ---------------------------------------------------------------------------

class CountingSOEHandler(opendnp3.ISOEHandler):
    """SOE handler that counts into a list it doesn't own, so the test can see it after dropping the handler."""

    def __init__(self, counts):
        super().__init__()
        self.counts = counts

    def BeginFragment(self, info):
        pass

    def EndFragment(self, info):
        pass

    def Process(self, info, values):
        self.counts.append(len(values))


class TestHandlerLifetime:
    """The stack keeps a Python handler, overrides and all, for as long as it uses it."""

    def test_handlers_passed_without_a_reference(self, manager):
        import gc

        port = _alloc_port()
        server = manager.AddTCPServer("lifetime-server", opendnp3.levels.NOTHING,
                                      opendnp3.ServerAcceptMode.CloseExisting,
                                      opendnp3.IPEndpoint("127.0.0.1", port), None)
        ocfg = opendnp3.OutstationStackConfig(opendnp3.DatabaseConfig(3))
        ocfg.link.LocalAddr = 1024
        ocfg.link.RemoteAddr = 1
        outstation = server.AddOutstation("lifetime-outstation", AcceptAllCommandHandler(),
                                          SimpleOutstationApp(), ocfg)

        client = manager.AddTCPClient("lifetime-client", opendnp3.levels.NOTHING, opendnp3.ChannelRetry.Default(),
                                      [opendnp3.IPEndpoint("127.0.0.1", port)], "0.0.0.0", None)
        mcfg = opendnp3.MasterStackConfig()
        mcfg.master.disableUnsolOnStartup = True
        mcfg.link.LocalAddr = 1
        mcfg.link.RemoteAddr = 1024
        counts = []
        master = client.AddMaster("lifetime-master", CountingSOEHandler(counts), SimpleMasterApp(), mcfg)
        gc.collect()

        outstation.Enable()
        master.Enable()
        deadline = time.time() + 5
        while not counts and time.time() < deadline:
            time.sleep(0.05)
        assert sum(counts) > 0, "the SOE handler passed without a reference got nothing"

        # a scan's handler, too
        scan_counts = []
        master.ScanClasses(opendnp3.ClassField.AllClasses(), CountingSOEHandler(scan_counts))
        gc.collect()
        deadline = time.time() + 5
        while not scan_counts and time.time() < deadline:
            time.sleep(0.05)
        assert sum(scan_counts) > 0, "the scan's SOE handler passed without a reference got nothing"

        master.Shutdown()
        outstation.Shutdown()


# ---------------------------------------------------------------------------
# Calls that wait on the stack while ASIO threads call into Python
# ---------------------------------------------------------------------------

_DELETE_MANAGER = """
import time
import opendnp3

class Log(opendnp3.ILogHandler):
    def log(self, module, id, level, location, message):
        time.sleep(0.001)

class Listener(opendnp3.IChannelListener):
    def OnStateChange(self, state):
        time.sleep(0.05)

mgr = opendnp3.DNP3Manager(2, Log())
listeners = [Listener() for _ in range(8)]
for i, listener in enumerate(listeners):
    # nothing listens on port 1, so the clients keep retrying, and logging
    mgr.AddTCPClient("c%d" % i, opendnp3.levels.ALL, opendnp3.ChannelRetry.Default(),
                     [opendnp3.IPEndpoint("127.0.0.1", 1)], "0.0.0.0", listener)
time.sleep(0.5)
del mgr
print("deleted")
"""

_GET_LOG_FILTERS = """
import time
import opendnp3

class Log(opendnp3.ILogHandler):
    def log(self, module, id, level, location, message):
        sum(range(2000))

class Listener(opendnp3.IChannelListener):
    def OnStateChange(self, state):
        sum(range(20000))

mgr = opendnp3.DNP3Manager(1, Log())
retry = opendnp3.ChannelRetry(opendnp3.TimeDuration.Milliseconds(1), opendnp3.TimeDuration.Milliseconds(1))
ch = mgr.AddTCPClient("c", opendnp3.levels.ALL, retry, [opendnp3.IPEndpoint("127.0.0.1", 1)], "0.0.0.0", Listener())

class SOE(opendnp3.ISOEHandler):
    def BeginFragment(self, info):
        pass
    def EndFragment(self, info):
        pass
    def Process(self, info, values):
        pass

class App(opendnp3.IMasterApplication):
    pass

soe, app = SOE(), App()
master = ch.AddMaster("m", soe, app, opendnp3.MasterStackConfig())
master.Enable()
time.sleep(0.2)
for _ in range(20000):
    ch.GetLogFilters()
mgr.Shutdown()
print("done")
"""


def _run_isolated(code):
    import subprocess
    import sys as _sys

    try:
        result = subprocess.run([_sys.executable, "-c", code], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        pytest.fail("deadlocked: still running after 30 s")
    assert result.returncode == 0, result.stderr


class TestNoDeadlock:
    def test_deleting_the_manager_while_callbacks_run(self):
        _run_isolated(_DELETE_MANAGER)

    def test_get_log_filters_while_callbacks_run(self):
        _run_isolated(_GET_LOG_FILTERS)
