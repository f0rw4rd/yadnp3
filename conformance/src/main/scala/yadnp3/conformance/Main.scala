package yadnp3.conformance

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
  io.stepfunc.dnp3c.conformance.Main.run(args.toArray, new Yadnp3Plugin(binary, logDir))
}
