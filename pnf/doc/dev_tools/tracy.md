<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Performance analysis with the Tracy profiler

[[_TOC_]]

## Overview

From the Tracy manual:

> Tracy is a real-time, nanosecond resolution hybrid frame and sampling
> profiler that you can use for remote or embedded telemetry of games and other
> applications. It can profile CPU, GPU, memory allocations, locks, context
> switches, [...]

- Sources are on [Github](https://github.com/wolfpld/tracy)
- There is a [web demo](https://tracy.nereid.pl/)
- You can [watch an intro video](https://youtu.be/ghXk3Bk5F2U?t=37)
- You can [read the manual](https://github.com/wolfpld/tracy/releases/download/v0.13.1/tracy.pdf)

## OAI Integration

To enable Tracy, compile `-DTRACY_ENABLE=ON` in cmake. Note that `build_oai`
has no native switch, but you can use `--cmake-opt -DTRACY_ENABLE=ON` instead.

Furthermore, you will need the Tracy profiler:

- Windows hosts: There is a precompiled `tracy-profiler.exe` on Github
- Linux hosts: Compile tracy-profiler from source as described in the manual.

Start the OAI executable you want to profile. Then, open the profiler, and
click on connect to connect to the executable.

It is also possible to collect data from within docker containers:

- Open port 8086.
- To collect CPU data, make sure that you run docker with `--privileged --mount
  "type=bind,source=/sys/kernel/debug,target=/sys/kernel/debug,readonly" --user
  0:0 --pid=host` or provide the corresponding options in docker-compose.

## Instrumentation

Instrumentation is done via the header `common/instrumentation.h`. A couple of
places in OAI have been instrumented already, search for the macros mentioned
in `common/instrumentation.h`.

In short, main features already in use:

- Measure specific code regions by surrounding them with `TracyCZone(ctx, true);`
  and `TracyCZoneEnd(ctx);`.
- Record individual "Tracy frames" (in the OAI context, that's likely one 4G/5G
  slot) with `TracyCFrameMark;`
- Plot values using `TracyCPlot(name, val);`

More information about these macros can be found in the manual.

Make sure to link `utils` into the static library you are modifying to get
tracy header definitions.
