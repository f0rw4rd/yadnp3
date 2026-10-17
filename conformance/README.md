# Conformance tests

Runs the outstation tests of the DNP3 IED Certification Procedure against yadnp3, unattended, with
[dnp3c](https://github.com/stepfunc/dnp3c).

- `cpp/tests/conformance` builds `conformance-outstation`, an outstation configured from its command line and driven
  through stdin (see the comment at the top of its `main.cpp`).
- This directory is a dnp3c integration plugin. For each procedure it starts the outstation, performs the steps dnp3c
  can't do over DNP3 (power cycles, generating events, uninstalling points, checking operated outputs, setting up the
  network) and checks responses against what it told the outstation to hold.
- `device.ini` describes the outstation to dnp3c: its points, the commands each output accepts, and what it supports.

## Running

Needs a JDK 17 or newer, Maven, and Linux: the outstation listens on 127.0.0.2, so the network tests (section 9) can
connect from 127.0.0.1's port 20000.

```
cmake -DDNP3_TESTS=ON .. && cmake --build . --target conformance-outstation   # in build/
cd conformance
mvn compile scala:run -DlauncherId=conformance                                 # 3.1r1, every procedure
mvn compile scala:run -DlauncherId=conformance -Dconformance.run=8.24          # one section or procedure
mvn compile scala:run -DlauncherId=conformance -Dconformance.revision=2.7      # revision 2.7
```

The report is `results/<revision>/L<level>/report.html`, and the outstation's own logs are in
`results/outstation-logs`. The run stops at the first procedure that fails.

Environment variables:

- `YADNP3_OUTSTATION`: the outstation binary, by default `../build/cpp/tests/conformance/conformance-outstation`.
- `YADNP3_ADDRESS`: the address it listens on, by default 127.0.0.2; `tcp.device.address` must match.
- `YADNP3_ABSENT`: kinds of points the outstation has none of, e.g. `fcnt` (frozen counters) for subset level 1,
  which doesn't have them. The `supports.*` options must say the same.

`device.ini` is for subset level 2. For level 1, set `outstation.subset.level = 1`, turn off `supports.frozenCounter`,
`supports.disable.frozenCounter` and `supports.frozenCounterChangeEvents`, and run with `YADNP3_ABSENT=fcnt`. For level
3, set `outstation.subset.level = 3`.

## What isn't tested

- Section 10 (output events) is skipped: the outstation doesn't report output events.
- Of section 9, only the TCP tests that need no address filtering run (9.5.1.1.2, 9.5.1.2.2): the outstation doesn't
  filter masters by address, and doesn't use UDP here.
- The outstation has no self address, confirmed link-layer frames, or device attributes (g0), so `device.ini` turns
  those off.

## License

dnp3c is GPL-3.0-only. Maven downloads it from Maven Central to build and run the plugin; it isn't part of this
repository.
