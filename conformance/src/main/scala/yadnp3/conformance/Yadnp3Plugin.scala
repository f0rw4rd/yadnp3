package yadnp3.conformance

import io.stepfunc.dnp3c.dnp3._
import io.stepfunc.dnp3c.dnp3.app._
import io.stepfunc.dnp3c.dnp3.app.objects.AttributeValue

import java.io.File
import java.util.concurrent.ConcurrentLinkedQueue

/** How the outstation process is configured; a change takes a power cycle */
final case class StackSettings(
    remoteAddress: Int = 1,
    unsolicited: Boolean = false,
    unsolicitedRetries: Option[Int] = None,
    unsolicitedConfirmTimeoutMs: Int = 2000,
    fragmentSize: Int = 2048,
    disabled: Set[PointKind] = Set.empty,
    localOutputs: Set[Int] = Set.empty,
    coldRestart: Boolean = true)

/**
 * Runs yadnp3's conformance-outstation (cpp/tests/conformance) for each procedure, generates events through its
 * control protocol, and checks responses against what it was told to hold
 */
class Yadnp3Plugin(binary: String, logDir: File) extends IntegrationPlugin {

  import PointKind._

  private val numInputs = 10
  private val numOutputs = 20

  private var settings = StackSettings()
  private var process: Option[OutstationProcess] = None
  private var mirror = new Mirror(numInputs, numOutputs, Set.empty)
  private var batch = 0
  private var procedure = 0
  private val crobs = new ConcurrentLinkedQueue[Int]()
  private val analogs = new ConcurrentLinkedQueue[Int]()

  private def outstation: OutstationProcess = process.getOrElse(throw new Exception("the outstation isn't running"))

  private def kindArg(kind: PointKind): String = kind match {
    case BinaryInput => "bi"
    case DoubleBitInput => "dbi"
    case Counter => "cnt"
    case FrozenCounter => "fcnt"
    case AnalogInput => "ai"
    case BinaryOutputStatus => "bo"
    case AnalogOutputStatus => "ao"
  }

  // the class each point starts in, as device.ini's class.assign options say; None for no events
  private def defaultClass(kind: PointKind, idx: Int): Option[EventClass] = kind match {
    case BinaryOutputStatus | AnalogOutputStatus => None
    case Counter => Some(EventClass.Class2)
    case AnalogInput => Some(EventClass.Class3)
    case BinaryInput if idx >= 8 => Some(EventClass.Class2)
    case _ => Some(EventClass.Class1)
  }

  // the classes the master assigned points to since the outstation started
  private val assigned = scala.collection.concurrent.TrieMap[(PointKind, Int), Option[EventClass]]()

  private def classOf(kind: PointKind, idx: Int): Option[EventClass] =
    assigned.getOrElse((kind, idx), defaultClass(kind, idx))

  private def onAssign(pointType: Int, pointClass: Int, start: Int, stop: Int): Unit = {
    val kind = List(BinaryInput, DoubleBitInput, Counter, FrozenCounter, AnalogInput, BinaryOutputStatus, AnalogOutputStatus)(pointType)
    val eventClass = pointClass match {
      case 0x02 => Some(EventClass.Class1)
      case 0x04 => Some(EventClass.Class2)
      case 0x08 => Some(EventClass.Class3)
      case _ => None
    }
    (start to stop).foreach(i => assigned((kind, i)) = eventClass)
  }

  private def onNotification(n: Notification): Unit = n match {
    case Notification.Crob(index) => crobs.add(index)
    case Notification.AnalogOutput(index, value) =>
      analogs.add(index)
      synchronized(mirror.set(Value(AnalogOutputStatus, index, value, 0x01, 0)))
    case Notification.Assign(t, c, start, stop) => onAssign(t, c, start, stop)
    case _ => ()
  }

  private def start(reporter: PluginReporter): Unit = {
    val s = settings
    val args = Seq(
      "--remote", s.remoteAddress.toString,
      "--unsol", if (s.unsolicited) "1" else "0",
      "--unsol-retries", s.unsolicitedRetries.getOrElse(-1).toString,
      "--unsol-confirm-timeout", s.unsolicitedConfirmTimeoutMs.toString,
      "--frag", s.fragmentSize.toString,
      "--cold-restart", if (s.coldRestart) "1" else "0",
      "--log", "comms") ++
      (if (s.disabled.nonEmpty) Seq("--disable", s.disabled.map(kindArg).mkString(",")) else Nil)
    val log = new File(logDir, f"outstation-$procedure%03d.log")
    reporter.log(s"Starting the outstation: ${args.mkString(" ")} (log: ${log.getName})")
    crobs.clear()
    analogs.clear()
    assigned.clear()
    mirror = new Mirror(numInputs, numOutputs, s.disabled)
    process = Some(new OutstationProcess(binary, args, log, onNotification))
    if (s.localOutputs.nonEmpty) applyLocal(s.localOutputs)
  }

  private def stop(): Unit = {
    process.foreach(_.stop())
    process = None
  }

  private def restart(reporter: PluginReporter): Unit = {
    stop()
    start(reporter)
  }

  private def update(kind: PointKind, idx: Int, value: Double, flags: Int, event: Boolean): Value = {
    val t = outstation.stamped(s"${kindArg(kind)} $idx $value $flags ${if (event) 1 else 0}")
    val v = Value(kind, idx, value, flags, t.ms)
    mirror.set(v)
    if (event && mirror.installed(kind)) classOf(kind, idx).foreach(c => mirror.addEvent(v, batch, c))
    v
  }

  // ONLINE cleared for the outputs in local mode, the state and every other flag kept (3.1r1's 8.6.4)
  private def applyLocal(indices: Set[Int]): Unit = {
    outstation.command(("local" +: indices.toSeq.sorted.map(_.toString)).mkString(" "))
    (0 until numOutputs).foreach { i =>
      val p = mirror.get(BinaryOutputStatus, i)
      val flags = if (indices.contains(i)) p.flags & ~0x01 else p.flags | 0x01
      if (flags != p.flags) update(BinaryOutputStatus, i, p.value, flags, event = false)
    }
  }

  // the next value of an input, and its flags, which make it change
  private def change(kind: PointKind, idx: Int): Value = {
    val p = mirror.get(kind, idx)
    kind match {
      case BinaryInput =>
        val on = p.value == 0
        update(kind, idx, if (on) 1 else 0, if (on) 0x81 else 0x01, event = true)
      case DoubleBitInput =>
        val on = p.value == 1
        update(kind, idx, if (on) 2 else 1, if (on) 0x81 else 0x41, event = true)
      case Counter => update(kind, idx, p.value + 1, 0x01, event = true)
      case AnalogInput => update(kind, idx, p.value + 100, 0x01, event = true)
      case other => throw new Exception(s"Can't change a ${other.name}")
    }
  }

  private def log(reporter: PluginReporter, v: Value): Unit = reporter.log(s"Updated ${Mirror.describe(v)}")

  // =================
  // Meta and actions
  // =================

  override def startProcedure(reporter: PluginReporter): Unit = {
    procedure += 1
    settings = StackSettings()
    batch = 0
    start(reporter)
  }

  override def endProcedure(reporter: PluginReporter): Unit = stop()

  override def cyclePower(reporter: PluginReporter): Unit = {
    reporter.log("Cycling power")
    restart(reporter)
  }

  override def setLinkLayerConfirm(reporter: PluginReporter, value: Boolean): Unit =
    if (value) throw new Exception("the outstation doesn't send confirmed link-layer frames")

  override def setSelfAddressSupport(reporter: PluginReporter, value: Boolean): Unit =
    if (value) throw new Exception("the outstation doesn't support the self address")

  override def checkOperate(reporter: CheckReporter, kind: OutputKind, index: Int): Option[DeferredCheck] = {
    val queue = if (kind == BinaryOutputStatus) crobs else analogs
    val deadline = System.currentTimeMillis() + 1000
    var found: Option[Int] = None
    while (found.isEmpty && System.currentTimeMillis() < deadline) {
      found = Option(queue.poll())
      if (found.isEmpty) Thread.sleep(20)
    }
    found match {
      case Some(i) if i == index => reporter.log(s"${kind.name} $index operated")
      case Some(i) => throw new Exception(s"${kind.name} $i operated, expected $index")
      case None => throw new Exception(s"${kind.name} $index never operated")
    }
    None
  }

  override def checkNotOperate(reporter: CheckReporter, kind: OutputKind): Option[DeferredCheck] = {
    val queue = if (kind == BinaryOutputStatus) crobs else analogs
    Thread.sleep(500)
    Option(queue.poll()).foreach(i => throw new Exception(s"${kind.name} $i operated, expected none"))
    reporter.log(s"No ${kind.name} operated")
    None
  }

  override def generateInputChanges(reporter: PluginReporter, kind: InputKind[?]): Unit = {
    batch += 1
    (0 until numInputs).foreach(i => log(reporter, change(kind, i)))
  }

  override def generateExactInputEvents(reporter: PluginReporter, kind: InputKind[?], numEvents: Int): Unit = {
    batch += 1
    (1 to numEvents).foreach(_ => log(reporter, change(kind, 0)))
  }

  private def kindOfClass(eventClass: EventClass): InputKind[?] = eventClass match {
    case EventClass.Class1 => BinaryInput
    case EventClass.Class2 => Counter
    case EventClass.Class3 => AnalogInput
    case other => throw new Exception(s"No single kind of input for $other")
  }

  override def generateClassEvents(reporter: PluginReporter, eventClass: EventClass): Unit = eventClass match {
    case EventClass.All =>
      batch += 1
      List(BinaryInput, Counter, AnalogInput).foreach(k => (0 until numInputs).foreach(i => log(reporter, change(k, i))))
    case c =>
      // only the points of the kind in the class
      batch += 1
      val kind = kindOfClass(c)
      (0 until numInputs).filter(i => classOf(kind, i).contains(c)).foreach(i => log(reporter, change(kind, i)))
  }

  override def generateExactClassEvents(reporter: PluginReporter, eventClass: EventClass, numEvents: Int): Unit =
    generateExactInputEvents(reporter, kindOfClass(eventClass), numEvents)

  override def generatePattern(reporter: PluginReporter, kind: InputKind[?]): Unit = generateInputChanges(reporter, kind)

  override def uninstall(reporter: PluginReporter, kind: PointKind): Unit = {
    settings = settings.copy(disabled = settings.disabled + kind)
    reporter.log(s"Uninstalling every ${kind.name}")
    restart(reporter)
  }

  override def reinstallPoints(reporter: PluginReporter): Unit = {
    settings = settings.copy(disabled = Set.empty)
    reporter.log("Reinstalling every point")
    restart(reporter)
  }

  override def enableRemoteSupervisoryControl(reporter: PluginReporter): Unit = {
    settings = settings.copy(localOutputs = Set.empty)
    applyLocal(Set.empty)
    reporter.log("Every binary output is under remote control")
  }

  override def setGlobalRemoteSupervisoryControl(reporter: PluginReporter): Unit = {
    settings = settings.copy(localOutputs = (0 until numOutputs).toSet)
    applyLocal(settings.localOutputs)
    reporter.log("Every binary output is under local control")
  }

  override def setIndividualRemoteSupervisoryControl(reporter: PluginReporter, index: Int): Unit = {
    settings = settings.copy(localOutputs = Set(index))
    applyLocal(settings.localOutputs)
    reporter.log(s"Binary output $index is under local control")
  }

  override def enableUnsolicitedResponse(reporter: PluginReporter, enabled: Boolean): Unit = {
    settings = settings.copy(unsolicited = enabled)
    reporter.log(s"Unsolicited responses enabled: $enabled")
    restart(reporter)
  }

  override def setUnsolicitedResponseTimeout(reporter: PluginReporter, timeoutMs: Int): Unit = {
    settings = settings.copy(unsolicitedConfirmTimeoutMs = timeoutMs)
    reporter.log(s"Unsolicited confirm timeout set to $timeoutMs ms")
    restart(reporter)
  }

  override def setMaxUnsolicitedRetries(reporter: PluginReporter, numRetries: Option[Int]): Unit = {
    settings = settings.copy(unsolicitedRetries = numRetries)
    reporter.log(s"Unsolicited retries set to ${numRetries.fold("infinite")(_.toString)}")
    restart(reporter)
  }

  override def setMasterAddress(reporter: PluginReporter, address: Int): Unit = {
    settings = settings.copy(remoteAddress = address)
    reporter.log(s"Master address set to $address")
    restart(reporter)
  }

  override def generateUnsolicitedEvents(reporter: PluginReporter, timing: ActionTiming): ActionResult = {
    generateExactInputEvents(reporter, BinaryInput, 1)
    ActionResult.Performed
  }

  override def generateNotEnoughUnsolicitedEvents(reporter: PluginReporter): Unit =
    throw new Exception("the outstation sends an unsolicited response for any event")

  override def recordCurrentCounterValues(reporter: PluginReporter): Unit = {
    mirror.recordCounters()
    mirror.recorded.foreach(v => reporter.log(s"Recorded ${Mirror.describe(v)}"))
  }

  override def generateTimeEvent(reporter: PluginReporter): Unit = {
    batch += 1
    log(reporter, change(BinaryInput, 0))
  }

  override def setMaxFragmentSize(reporter: PluginReporter, maxFragmentSize: Int): Unit = {
    settings = settings.copy(fragmentSize = maxFragmentSize)
    reporter.log(s"Max fragment size set to $maxFragmentSize")
    restart(reporter)
  }

  override def generateMultiFragmentResponse(reporter: PluginReporter): Unit = {
    List(BinaryInput, DoubleBitInput, Counter, AnalogInput).foreach(k => generateExactInputEvents(reporter, k, 150))
    reporter.log("Generated 150 events of each kind of input")
  }

  override def setColdRestartSupport(reporter: PluginReporter, enabled: Boolean): Unit = {
    settings = settings.copy(coldRestart = enabled)
    outstation.command(s"coldrestart ${if (enabled) 1 else 0}")
    reporter.log(s"Cold restart support set to $enabled")
  }

  override def setAnalogInput(reporter: PluginReporter, index: Int, value: Double): Unit = {
    batch += 1
    log(reporter, update(AnalogInput, index, value, 0x01, event = true))
  }

  override def generateExactFrozenCounterEvents(reporter: PluginReporter, numEvents: Int): Unit = {
    batch += 1
    (1 to numEvents).foreach { _ =>
      val c = mirror.get(Counter, 0)
      update(Counter, 0, c.value + 1, c.flags, event = false)
      val t = outstation.stamped("freeze 0 0")
      val v = Value(FrozenCounter, 0, c.value + 1, c.flags, t.ms)
      mirror.set(v)
      classOf(FrozenCounter, 0).foreach(c => mirror.addEvent(v, batch, c))
      log(reporter, v)
    }
  }

  override def generateExactSmallAnalogInputEvents(reporter: PluginReporter, numEvents: Int): Unit =
    generateExactInputEvents(reporter, AnalogInput, numEvents)

  override def generateEventOn(reporter: PluginReporter, kind: PointKind, index: Int): Unit = {
    batch += 1
    log(reporter, change(kind, index))
  }

  // the state bits of a binary or double-bit point, kept when its flags change
  private def stateBits(v: Value): Int = v.kind match {
    case BinaryInput | BinaryOutputStatus => v.flags & 0x80
    case DoubleBitInput => v.flags & 0xc0
    case _ => 0
  }

  private def setFlags(kind: PointKind, indices: Seq[Int], bits: Int): Unit =
    indices.foreach { i =>
      val p = mirror.get(kind, i)
      if (kind == FrozenCounter) {
        outstation.command(s"fcntflags $i $bits")
        mirror.set(p.copy(flags = bits))
      } else update(kind, i, p.value, bits | stateBits(p), event = false)
    }

  override def setFlagsNormal(reporter: PluginReporter, kind: PointKind): Unit = {
    val count = if (kind == BinaryOutputStatus || kind == AnalogOutputStatus) numOutputs else numInputs
    setFlags(kind, 0 until count, 0x01)
    reporter.log(s"Every ${kind.name} has its flags normal")
  }

  override def setFlagAbnormal(reporter: PluginReporter, kind: PointKind, indices: Seq[Int]): AbnormalFlag = {
    val index = indices.headOption.getOrElse(throw new Exception(s"No ${kind.name} to set a flag of"))
    setFlags(kind, Seq(index), 0x05)
    reporter.log(s"${kind.name} $index has COMM_LOST set")
    AbnormalFlag(index, Some(0x05))
  }

  // ===========
  // Validations
  // ===========

  private def fail(msg: String): Nothing = throw new Exception(msg)

  // every point of the kinds is reported once, with the value the outstation holds
  private def verifyCurrent(reporter: PluginReporter, expected: Seq[Value], points: Seq[GenIndexedPoint[MeasurementPoint]], what: String): Unit = {
    val remaining = scala.collection.mutable.Map.from(expected.map(v => (v.kind, v.idx) -> v))
    points.foreach { p =>
      val key = (Mirror.kindOf(p.point), p.idx)
      val v = remaining.remove(key).getOrElse(fail(s"Unexpected ${key._1.name} ${p.idx}, or reported twice"))
      // a static value's time is when it last changed, which is what the mirror holds; the master can freeze
      // counters without the plugin seeing it, which gives a frozen counter the counter's value
      val frozenByMaster = v.kind == FrozenCounter && {
        val c = mirror.get(Counter, v.idx)
        Mirror.mismatch(Value(FrozenCounter, v.idx, c.value, c.flags, 0), p, checkTime = false).isEmpty
      }
      if (!frozenByMaster) Mirror.mismatch(v, p).foreach(fail)
    }
    if (remaining.nonEmpty) fail(s"Missing ${remaining.keys.toList.sortBy(_._2).map((k, i) => s"${k.name} $i").mkString(", ")}")
    reporter.log(s"Verified ${expected.size} $what")
  }

  override def verifyAllStaticCurrentValues(reporter: CheckReporter, isClass0: Boolean, points: Seq[GenIndexedPoint[MeasurementPoint]]): Option[DeferredCheck] = {
    verifyCurrent(reporter, mirror.kinds.flatMap(mirror.all), points, "static points")
    None
  }

  override def verifyInputCurrentValues[P <: MeasurementPoint](reporter: CheckReporter, kind: InputKind[P], points: Seq[GenIndexedPoint[P]]): Option[DeferredCheck] = {
    val received = points.map(_.asInstanceOf[GenIndexedPoint[MeasurementPoint]])
    // the counters' values can come as frozen counters, after a freeze (8.16.2.2.3 step 15)
    val frozen = received.exists(p => Mirror.kindOf(p.point) == FrozenCounter)
    val expected = if (kind == Counter && frozen) mirror.all(Counter).map(_.copy(kind = FrozenCounter)) else mirror.all(kind)
    verifyCurrent(reporter, expected, received, s"${kind.name} points")
    None
  }

  override def verifyRecordedCounterValues(reporter: CheckReporter, points: Seq[GenIndexedPoint[CounterPoint]]): Option[DeferredCheck] = {
    // the procedures compare the recorded values with frozen counters (8.16.2.2.3 step 11), or with counters
    val kind = if (points.exists(_.point.isFrozen)) FrozenCounter else Counter
    verifyCurrent(reporter, mirror.recorded.map(_.copy(kind = kind)), points.map(_.asInstanceOf[GenIndexedPoint[MeasurementPoint]]), "recorded counters")
    None
  }

  // each received event is the oldest unmatched one of its kind, so events are in order for each kind
  private def verifyEvents(reporter: PluginReporter, kinds: Set[PointKind], eventClass: EventClass, batchOnly: Option[Int], reset: Boolean, points: Seq[GenIndexedPoint[MeasurementPoint]]): Unit = {
    if (reset) mirror.resetHandled()
    points.foreach { p =>
      val kind = Mirror.kindOf(p.point)
      if (!kinds.contains(kind)) fail(s"Unexpected ${kind.name} event at index ${p.idx}")
      val expected = mirror.pop(kind, batchOnly, eventClass).getOrElse(fail(s"Unexpected ${kind.name} event at index ${p.idx}"))
      Mirror.mismatch(expected.value, p).foreach(m => fail(s"Event out of order or wrong: $m"))
    }
    val missing = mirror.remaining(batchOnly, eventClass).filter(e => kinds.contains(e.value.kind))
    if (missing.nonEmpty) fail(s"Missing ${missing.size} event(s), the first ${Mirror.describe(missing.head.value)}")
    reporter.log(s"Verified ${points.size} event(s)")
  }

  // the kinds with points in the class, or in any class for All
  private def kindsOf(eventClass: EventClass): Set[PointKind] =
    mirror.kinds.filter(k => (0 until numOutputs).exists(i => classOf(k, i).exists(c => eventClass == EventClass.All || c == eventClass))).toSet

  override def verifyInputEvents[P <: MeasurementPoint](reporter: CheckReporter, kind: InputKind[P], expectation: EventExpectation, points: Seq[GenIndexedPoint[P]]): Option[DeferredCheck] = {
    val received = points.map(_.asInstanceOf[GenIndexedPoint[MeasurementPoint]])
    expectation match {
      case EventExpectation.LatestBatch => verifyEvents(reporter, Set(kind), EventClass.All, Some(batch), reset = true, received)
      case EventExpectation.RemainingLatestBatch => verifyEvents(reporter, Set(kind), EventClass.All, Some(batch), reset = false, received)
      case EventExpectation.AllGeneratedBatches => verifyEvents(reporter, Set(kind), EventClass.All, None, reset = true, received)
      case EventExpectation.CurrentValuesWithOptionalEarlier => verifyCurrentValues(reporter, kind, received)
      case other => fail(s"Unsupported check of ${kind.name} events: $other")
    }
    None
  }

  // for each point changed in the latest batch, its last event has its current value; any before it, earlier values
  private def verifyCurrentValues(reporter: PluginReporter, kind: PointKind, points: Seq[GenIndexedPoint[MeasurementPoint]]): Unit = {
    val history = mirror.events.filter(_.value.kind == kind).toSeq
    val current = history.filter(_.batch == batch).groupBy(_.value.idx).view.mapValues(_.last.value).toMap
    val received = points.groupBy(_.idx)
    if (received.keySet != current.keySet)
      fail(s"Expected ${kind.name} events for ${current.keys.toList.sorted.mkString(", ")}, received ${received.keys.toList.sorted.mkString(", ")}")
    current.foreach { (idx, expected) =>
      Mirror.mismatch(expected, received(idx).last).foreach(fail)
      received(idx).init.foreach(p =>
        if (!history.exists(e => e.value.idx == idx && Mirror.matches(e.value, p))) fail(s"${kind.name} $idx reported a value it never had"))
    }
    reporter.log(s"Verified the current values of ${current.size} ${kind.name}(s)")
  }

  override def verifyFirstInputEvent[P <: MeasurementPoint](reporter: CheckReporter, kind: InputKind[P], point: GenIndexedPoint[P]): Option[DeferredCheck] = {
    mirror.resetHandled()
    val expected = mirror.pop(kind, Some(batch), EventClass.All).getOrElse(fail(s"No ${kind.name} event was generated"))
    Mirror.mismatch(expected.value, point.asInstanceOf[GenIndexedPoint[MeasurementPoint]]).foreach(fail)
    reporter.log(s"Verified the first ${kind.name} event")
    None
  }

  override def verifyClassEvents(reporter: CheckReporter, eventClass: EventClass, expectation: EventExpectation, points: Seq[GenIndexedPoint[MeasurementPoint]]): Option[DeferredCheck] = {
    val kinds = kindsOf(eventClass)
    expectation match {
      case EventExpectation.LatestBatch => verifyEvents(reporter, kinds, eventClass, Some(batch), reset = true, points)
      case EventExpectation.RemainingLatestBatch => verifyEvents(reporter, kinds, eventClass, Some(batch), reset = false, points)
      case EventExpectation.AllGeneratedBatches => verifyEvents(reporter, kinds, eventClass, None, reset = true, points)
      case EventExpectation.ReturnedEventsOnly =>
        points.foreach(p =>
          if (!mirror.events.exists(e => Mirror.matches(e.value, p))) fail(s"Event at index ${p.idx} matches no event generated"))
        reporter.log(s"Verified ${points.size} returned event(s)")
      case other => fail(s"Unsupported check of $eventClass events: $other")
    }
    None
  }

  override def verifyFirstClassEvent(reporter: CheckReporter, eventClass: EventClass, point: GenIndexedPoint[MeasurementPoint]): Option[DeferredCheck] = {
    mirror.resetHandled()
    val expected = mirror.pop(Mirror.kindOf(point.point), Some(batch), eventClass).getOrElse(fail(s"Unexpected $eventClass event"))
    Mirror.mismatch(expected.value, point).foreach(fail)
    reporter.log(s"Verified the first $eventClass event")
    None
  }

  override def checkAnalogOutputTolerance(reporter: CheckReporter, expectedValue: Double, actualValue: Double): Option[DeferredCheck] = {
    if (Math.abs(expectedValue - actualValue) > 0.001) fail(s"Analog output status $actualValue, expected $expectedValue")
    reporter.log("Analog output status within 0.001 of the value commanded")
    None
  }

  private def sameEvent(a: GenIndexedPoint[MeasurementPoint], b: GenIndexedPoint[MeasurementPoint]): Boolean = {
    def resolved(p: MeasurementPoint) = p.copy_with_timestamp(p.timestamp.map(_.copy(isRelative = false)))
    a.idx == b.idx && resolved(a.point) == resolved(b.point)
  }

  override def verifyPolledDataDoNotRepeatUnsolicitedData(reporter: CheckReporter, unsolicitedResponsePoints: Seq[GenIndexedPoint[MeasurementPoint]], polledResponsePoints: Seq[GenIndexedPoint[MeasurementPoint]]): Option[DeferredCheck] = {
    verifyEvents(reporter, kindsOf(EventClass.All), EventClass.All, Some(batch), reset = true, unsolicitedResponsePoints)
    polledResponsePoints.find(p => unsolicitedResponsePoints.exists(sameEvent(_, p))).foreach(p =>
      fail(s"The polled response repeats the event at index ${p.idx}"))
    None
  }

  override def verifyResponseRetainsEvents(reporter: CheckReporter, previous: Seq[GenIndexedPoint[MeasurementPoint]], current: Seq[GenIndexedPoint[MeasurementPoint]]): Option[DeferredCheck] = {
    current.foreach(c =>
      if (!mirror.events.exists(e => Mirror.matches(e.value, c))) fail(s"The event at index ${c.idx} matches no event generated"))
    previous.foreach { p =>
      val kind = Mirror.kindOf(p.point)
      val latest = mirror.events.filter(e => e.value.kind == kind && e.value.idx == p.idx).lastOption
      val retained = current.exists(sameEvent(p, _)) || current.exists(c => latest.exists(l => Mirror.matches(l.value, c)))
      if (!retained) fail(s"The response no longer reports the event at index ${p.idx}, or its current value")
    }
    reporter.log(s"Verified that the response retains the ${previous.size} earlier event(s)")
    None
  }

  override def verifyFrozenCounterEvents(reporter: CheckReporter, points: Seq[GenIndexedPoint[CounterPoint]]): Option[DeferredCheck] = {
    verifyEvents(reporter, Set(FrozenCounter), EventClass.All, Some(batch), reset = true, points.map(_.asInstanceOf[GenIndexedPoint[MeasurementPoint]]))
    None
  }

  override def verifyDelayMeasurementAccuracy(reporter: CheckReporter, delayMs: Long): Option[DeferredCheck] = {
    if (delayMs < 0 || delayMs > 50) fail(s"Delay measurement of $delayMs ms, expected 0 to 50 ms")
    reporter.log(s"Delay measurement of $delayMs ms is within 0 to 50 ms")
    None
  }

  override def verifyTimestamp(reporter: CheckReporter, points: Seq[GenIndexedPoint[MeasurementPoint]]): Option[DeferredCheck] = {
    verifyEvents(reporter, kindsOf(EventClass.All), EventClass.All, None, reset = true, points)
    None
  }

  override def verifyDeviceAttributes(reporter: CheckReporter, attributes: Seq[(Int, AttributeValue)]): Option[DeferredCheck] =
    fail("the outstation doesn't support device attributes")
}
