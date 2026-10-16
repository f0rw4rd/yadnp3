package yadnp3.conformance

import java.io.{BufferedReader, File, InputStreamReader, PrintWriter}
import java.util.concurrent.{LinkedBlockingQueue, TimeUnit}

/** What the outstation reported on its own: what the master operated, wrote or assigned */
enum Notification {
  case Crob(index: Int)
  case AnalogOutput(index: Int, value: Double)
  case Time(ms: Long)
  case ColdRestart
  // the master assigned the points start to stop of the type (AssignClassType) to the class (PointClass bits)
  case Assign(pointType: Int, pointClass: Int, start: Int, stop: Int)
}

/** A time the outstation stamped an update with */
final case class StampedTime(ms: Long, quality: Int)

/**
 * The conformance-outstation process (cpp/tests/conformance), one per power cycle: it's configured on the command
 * line, and controlled with one command per line on stdin
 */
final class OutstationProcess(binary: String, args: Seq[String], logFile: File, onNotification: Notification => Unit) {

  private val process = new ProcessBuilder((binary +: args)*)
    .redirectError(ProcessBuilder.Redirect.appendTo(logFile))
    .start()
  private val input = new PrintWriter(process.getOutputStream, true)
  private val responses = new LinkedBlockingQueue[String]()

  private val reader = new Thread(() => {
    val out = new BufferedReader(new InputStreamReader(process.getInputStream))
    Iterator.continually(out.readLine()).takeWhile(_ != null).foreach { line =>
      if (line.startsWith("EV ")) notification(line.drop(3)).foreach(onNotification)
      else responses.put(line)
    }
    responses.put("EOF")
  })
  reader.setDaemon(true)
  reader.start()

  expect("READY")

  private def notification(text: String): Option[Notification] = text.split(" ").toList match {
    case "CROB" :: index :: Nil => Some(Notification.Crob(index.toInt))
    case "AO" :: index :: value :: Nil => Some(Notification.AnalogOutput(index.toInt, value.toDouble))
    case "TIME" :: ms :: Nil => Some(Notification.Time(ms.toLong))
    case "COLDRESTART" :: Nil => Some(Notification.ColdRestart)
    case "ASSIGN" :: t :: c :: start :: stop :: Nil => Some(Notification.Assign(t.toInt, c.toInt, start.toInt, stop.toInt))
    case _ => None
  }

  private def expect(expected: String): Unit = {
    val line = responses.poll(10, TimeUnit.SECONDS)
    if (line != expected) throw new Exception(s"outstation process: expected $expected, got $line (log: $logFile)")
  }

  /** Sends a command and returns the rest of its OK line */
  def command(cmd: String): String = {
    input.println(cmd)
    val line = responses.poll(10, TimeUnit.SECONDS)
    if (line == null) throw new Exception(s"outstation process didn't answer '$cmd'")
    if (line == "OK") ""
    else if (line.startsWith("OK ")) line.drop(3)
    else throw new Exception(s"outstation process failed '$cmd': $line")
  }

  def stamped(cmd: String): StampedTime = command(cmd).split(" ") match {
    case Array(ms, quality) => StampedTime(ms.toLong, quality.toInt)
    case other => throw new Exception(s"outstation process gave no time for '$cmd': ${other.mkString(" ")}")
  }

  def stop(): Unit = {
    if (process.isAlive) {
      try command("quit")
      catch { case _: Exception => () }
      if (!process.waitFor(5, TimeUnit.SECONDS)) {
        process.destroyForcibly()
        process.waitFor(5, TimeUnit.SECONDS)
      }
    }
  }
}
