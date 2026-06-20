# Probe — SCPI telemetry & trigger-capture

The **probe** is a FreeRTOS task that exposes TARS internals over a serial line
using a minimal SCPI dialect. It does two things:

1. **Telemetry** — periodically pushes a set of metrics (CPU/RTOS/peripheral)
   defined at compile time.
2. **Trigger-capture** — on command, acquires a high-rate buffer into external
   SDRAM and streams it back as a SCPI definite-length binary block.

## Transport

| Setting | Value |
|---------|-------|
| Peripheral | USART1 (PA9 = TX, PA10 = RX) |
| Baud | 115200, 8N1 |
| TX | DMA (DMA2 Stream7) |
| RX | interrupt, byte-by-byte ring buffer |

> **Hardware note.** USART1 PA9/PA10 are routed to the on-board ST-LINK. Only an
> ST-LINK with VCP (USB PID `0x374B`) exposes this as a host COM port. Plain
> ST-LINK/V2 (PID `0x3748`, as on some F429I-DISC1 units) does **not** — in that
> case attach an external USB-TTL adapter to **PA9 / PA10 / GND**.

This channel is independent of the USB CDC shell (`tars>`), which stays on USB.

## Bandwidth

115200 baud ≈ **11.5 KB/s**. A 64 KB capture therefore takes ~6 s to stream
back. Acquisition is effectively instant; the UART is the bottleneck. Raise the
baud rate or change transport if you need faster readback.

## SCPI command set (minimal)

| Command | Reply | Meaning |
|---------|-------|---------|
| `*IDN?` | `TARS,PROBE,<board>,1.0` | Identify |
| `*RST` | `OK` | Reset capture + telemetry to defaults |
| `*TRG` / `CAP:TRIG` | binary block | Acquire one capture and stream it |
| `CAP:FETC?` | binary block | Re-stream the last capture |
| `CAP:STAT?` | `IDLE` / `READY` | Capture state |
| `CAP:DEPT <n>` / `CAP:DEPT?` | `OK` / `<n>` | Capture depth in bytes (default 65536) |
| `CAP:RATE <hz>` / `CAP:RATE?` | `OK` / `<hz>` | Sample rate (default 1000000) |
| `MEAS:ALL?` | `name=val,...` | All metrics in one line |
| `MEAS? <name>` | `<val>` | One metric |
| `METR:LIST?` | `name,unit` per line | List metrics |
| `TELE:STAT ON\|OFF` / `TELE:STAT?` | `OK` / `ON`\|`OFF` | Telemetry on/off (default ON) |
| `TELE:PER <ms>` / `TELE:PER?` | `OK` / `<ms>` | Telemetry period (default 1000, min 50) |

Commands are line-based (`\r\n` or `\n`) and case-insensitive.

### Telemetry format

When enabled, the probe emits one line per period, prefixed so the host can tell
it apart from command replies:

```
TELM:uptime_ms=12345.000,heap_free=40960.000,motor_pos=12.300,...
```

Silence it with `TELE:STAT OFF` before issuing queries if you want clean
request/response framing. The capture stream and telemetry run in the same task,
so a binary block is never interleaved with a telemetry line.

### Capture block format

SCPI definite-length block:

```
#<ndigits><length><raw bytes><CRLF>
```

e.g. `#565536` + 65536 raw bytes. Samples are little-endian `uint16`
(12-bit-range values in the MVP synthetic source).

## Capture pipeline

```
source --> SDRAM capture buffer (0xD0100000, 256 KB) --> UART DMA --> host
```

**MVP:** the source is *synthesized* into the buffer (a sine + dither), so the
full path (fill → frame → DMA out) is exercised end-to-end. To use a real
signal, replace the fill loop in `TarsProbeCapture_Trigger()`
(`App/probe/tars_probe_capture.c`) with ADC + DMA at the configured rate; the
streaming half stays unchanged.

## Metrics are compile-time configured

Metrics are declared in a CSV and code-generated, mirroring the pin map flow:

| Path | Role |
|------|------|
| `tools/probe/metrics.csv` | **Source of truth** — edit this |
| `generated/probe/metrics.c` | Auto-generated at build (gitignored) |
| `tools/probe/probe-gen.py` | CSV → C generator |

CSV row: `name,unit,provider,arg`. The `provider` token maps to
`TARS_PROBE_SRC_<provider>` and is read by `TarsProbeMetrics_Read()`.

**To add a metric:**

1. Add a row in `tools/probe/metrics.csv`.
2. Add the enum value in `App/probe/tars_probe_metrics.h`.
3. Add the `case` in `App/probe/tars_probe_metrics.c`.
4. Rebuild.

## PC client

`tools/probe/probe-client.py` (needs `pyserial`):

```bash
# Identify
python3 tools/probe/probe-client.py -p /dev/cu.usbserial-XXXX --idn

# Read all metrics once
python3 tools/probe/probe-client.py -p /dev/cu.usbserial-XXXX --meas

# List metric names/units
python3 tools/probe/probe-client.py -p /dev/cu.usbserial-XXXX --list

# Trigger a capture and save the raw block
python3 tools/probe/probe-client.py -p /dev/cu.usbserial-XXXX \
    --capture --depth 65536 --rate 1000000 -o cap.bin
```

The client silences telemetry, sets depth/rate, sends `*TRG`, parses the
`#<n><len>` block, prints the first samples, and optionally saves the payload.

## Source layout

| File | Role |
|------|------|
| `App/probe/tars_probe.c` | Task: telemetry + SCPI dispatch |
| `App/probe/tars_probe_uart.c` | USART1 DMA TX + IRQ RX |
| `App/probe/tars_probe_capture.c` | Capture engine (source → SDRAM → UART) |
| `App/probe/tars_probe_metrics.c` | Metric provider readings |
| `tools/probe/metrics.csv` | Metric definitions |
| `tools/probe/probe-gen.py` | CSV → C codegen |
| `tools/probe/probe-client.py` | PC test client |
