package yadnp3.conformance

import io.stepfunc.dnp3c.dnp3.PointKind
import io.stepfunc.dnp3c.dnp3.app._

import scala.collection.mutable
import scala.collection.mutable.ArrayBuffer

/**
 * A point as the outstation holds it. For binary and double-bit points the flags include the state bits, as they're
 * sent. The value of a double-bit point is its DoubleBit code (0 to 3).
 */
final case class Value(kind: PointKind, idx: Int, value: Double, flags: Int, time: Long)

/** An event the plugin generated, in the batch it belongs to */
final class Generated(val value: Value, val batch: Int, val eventClass: EventClass) {
  var handled = false
}

/** What the plugin made the outstation hold, and the events it generated, to check responses against */
final class Mirror(val numInputs: Int, val numOutputs: Int, disabled: Set[PointKind]) {

  import PointKind._

  val kinds: List[PointKind] =
    List(BinaryInput, DoubleBitInput, Counter, FrozenCounter, AnalogInput, BinaryOutputStatus, AnalogOutputStatus)

  // the points as the outstation starts them: ONLINE, and off or zero but for the counters, at 1
  private val points: mutable.Map[PointKind, mutable.SortedMap[Int, Value]] = mutable.Map.from(kinds.map { kind =>
    val count = if (kind == BinaryOutputStatus || kind == AnalogOutputStatus) numOutputs else numInputs
    val flags = if (kind == DoubleBitInput) 0x41 else 0x01
    val value = kind match {
      case DoubleBitInput | Counter => 1
      case _ => 0
    }
    kind -> mutable.SortedMap.from((0 until count).map(i => i -> Value(kind, i, value, flags, 0)))
  })

  val events: ArrayBuffer[Generated] = ArrayBuffer()
  private var recordedCounters: Seq[Value] = Nil

  def installed(kind: PointKind): Boolean = !disabled.contains(kind) && !(kind == FrozenCounter && disabled.contains(Counter))

  def get(kind: PointKind, idx: Int): Value = points(kind)(idx)
  def all(kind: PointKind): Seq[Value] = if (installed(kind)) points(kind).values.toSeq else Nil
  def set(value: Value): Unit = points(value.kind)(value.idx) = value

  def addEvent(value: Value, batch: Int, eventClass: EventClass): Unit = events += new Generated(value, batch, eventClass)

  def recordCounters(): Unit = recordedCounters = all(Counter)
  def recorded: Seq[Value] = recordedCounters

  def resetHandled(): Unit = events.foreach(_.handled = false)

  def inBatch(batch: Option[Int]): Seq[Generated] = events.filter(e => batch.forall(_ == e.batch)).toSeq

  /** The oldest event of the kind not yet matched, in the batch if given, and of the class unless it's All */
  def pop(kind: PointKind, batch: Option[Int], eventClass: EventClass): Option[Generated] = {
    val next = inBatch(batch).find(e =>
      !e.handled && e.value.kind == kind && (eventClass == EventClass.All || e.eventClass == eventClass))
    next.foreach(_.handled = true)
    next
  }

  def remaining(batch: Option[Int], eventClass: EventClass): Seq[Generated] =
    inBatch(batch).filter(e => !e.handled && (eventClass == EventClass.All || e.eventClass == eventClass))
}

object Mirror {

  import PointKind._

  /** The kind of point a received point is */
  def kindOf(point: MeasurementPoint): PointKind = point match {
    case _: BinaryPoint => BinaryInput
    case _: DoubleBitPoint => DoubleBitInput
    case c: CounterPoint => if (c.isFrozen) FrozenCounter else Counter
    case _: AnalogPoint => AnalogInput
    case _: BinaryOutputStatusPoint => BinaryOutputStatus
    case _: AnalogOutputStatusPoint => AnalogOutputStatus
  }

  private def numeric(point: MeasurementPoint): Double = point match {
    case p: BinaryPoint => if (p.value) 1 else 0
    case p: DoubleBitPoint => p.value.value.toDouble
    case p: CounterPoint => p.value.toDouble
    case p: AnalogPoint => p.value
    case p: BinaryOutputStatusPoint => if (p.value) 1 else 0
    case p: AnalogOutputStatusPoint => p.value
  }

  /** Why the received point doesn't match the expected value, if it doesn't; flags and time only where it has them */
  def mismatch(expected: Value, received: GenIndexedPoint[MeasurementPoint], checkTime: Boolean = true): Option[String] = {
    val kind = kindOf(received.point)
    val actual = numeric(received.point)
    if (kind != expected.kind) Some(s"expected a ${expected.kind.name}, received a ${kind.name}")
    else if (received.idx != expected.idx) Some(s"expected ${kind.name} ${expected.idx}, received index ${received.idx}")
    else if (actual != expected.value) Some(s"${kind.name} ${received.idx}: expected value ${expected.value}, received $actual")
    else
      received.point.flags.map(_ & 0xff) match {
        case Some(flags) if flags != expected.flags =>
          Some(f"${kind.name} ${received.idx}: expected flags 0x${expected.flags}%02X, received 0x$flags%02X")
        case _ =>
          received.point.timestamp match {
            case Some(t) if checkTime && t.value != expected.time =>
              Some(s"${kind.name} ${received.idx}: expected time ${expected.time}, received ${t.value}")
            case _ => None
          }
      }
  }

  def matches(expected: Value, received: GenIndexedPoint[MeasurementPoint]): Boolean = mismatch(expected, received).isEmpty

  def describe(v: Value): String = f"${v.kind.name} ${v.idx}: value=${v.value}, flags=0x${v.flags}%02X, time=${v.time}"
}
