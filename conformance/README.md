# Conformance tests

Runs the outstation tests of the DNP3 IED Certification Procedure against yadnp3, unattended, with
[dnp3c](https://github.com/stepfunc/dnp3c).

- `cpp/tests/conformance` builds `conformance-outstation`, an outstation on 127.0.0.1:20000 configured from its command
  line and driven through stdin (see the comment at the top of its `main.cpp`).
- This directory is a dnp3c integration plugin. For each procedure it starts the outstation, performs the steps dnp3c
  can't do over DNP3 (power cycles, generating events, uninstalling points, checking operated outputs) and checks
  responses against what it told the outstation to hold.
- `device.ini` describes the outstation to dnp3c: its points, the commands each output accepts, and what it supports.

## Running

Needs a JDK 17 or newer and Maven.

```
cmake -DDNP3_TESTS=ON .. && cmake --build . --target conformance-outstation   # in build/
cd conformance
mvn compile scala:run -DlauncherId=conformance                                 # 3.1r1, every procedure
mvn compile scala:run -DlauncherId=conformance -Dconformance.run=8.24          # one section or procedure
```

The outstation binary defaults to `../build/cpp/tests/conformance/conformance-outstation`; set `YADNP3_OUTSTATION` to
use another. The report is `results/3.1r1/L2/report.html`, and the outstation's own logs are in
`results/outstation-logs`. The run stops at the first procedure that fails.

To run revision 2.7, pass `-Dconformance.revision=2.7`.

## What isn't tested

- Section 9 (network) and section 10 (output events) are skipped: the plugin doesn't reconfigure the outstation's
  network, and the outstation doesn't report output events.
- The outstation has no self address, confirmed link-layer frames, or device attributes (g0), so `device.ini` turns
  those off.

## License

dnp3c is GPL-3.0-only. Maven downloads it from Maven Central to build and run the plugin; it isn't part of this
repository.
