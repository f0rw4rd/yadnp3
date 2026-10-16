package yadnp3.conformance

import io.stepfunc.dnp3c.dnp3.PointKind

import java.io.File

/**
 * The dnp3c command line, with the plugin that runs yadnp3's conformance-outstation. The outstation binary comes from
 * the YADNP3_OUTSTATION environment variable.
 */
@main def run(args: String*): Unit = {
  val binary = sys.env.getOrElse("YADNP3_OUTSTATION", "../build/cpp/tests/conformance/conformance-outstation")
  if (!new File(binary).canExecute) {
    System.err.println(s"conformance-outstation not found at $binary: build with -DDNP3_TESTS=ON or set YADNP3_OUTSTATION")
    sys.exit(2)
  }
  val logDir = new File(sys.env.getOrElse("YADNP3_LOG_DIR", "results/outstation-logs"))
  logDir.mkdirs()
  // the address the outstation listens on, which device.ini's tcp.device.address must match; the network tests need it
  // to be other than network.master.address
  val address = sys.env.getOrElse("YADNP3_ADDRESS", "127.0.0.2")
  // kinds of points the outstation is run without, e.g. YADNP3_ABSENT=fcnt for subset level 1, which has no frozen
  // counters; device.ini's supports options must say the same
  val kinds: Map[String, PointKind] = Map(
    "bi" -> PointKind.BinaryInput,
    "dbi" -> PointKind.DoubleBitInput,
    "cnt" -> PointKind.Counter,
    "fcnt" -> PointKind.FrozenCounter,
    "ai" -> PointKind.AnalogInput,
    "bo" -> PointKind.BinaryOutputStatus,
    "ao" -> PointKind.AnalogOutputStatus)
  val absent = sys.env.getOrElse("YADNP3_ABSENT", "").split(",").map(_.trim).filter(_.nonEmpty).map { k =>
    kinds.getOrElse(k, { System.err.println(s"unknown kind in YADNP3_ABSENT: $k"); sys.exit(2) })
  }.toSet[PointKind]
  io.stepfunc.dnp3c.conformance.Main.run(args.toArray, new Yadnp3Plugin(binary, logDir, address, absent))
}
