| # | Chapter | Theme |
|---|---|---|
| 1 | [The toolchain, first light, and printf over the ST-LINK](chapters/01-the-toolchain.md) | Cross toolchain, startup, linker script, flashing |
| 2 | [A single producer, single consumer ring buffer](chapters/02-a-single-producer.md) | Lock-free buffering, memory ordering on the M7 |
| 3 | [Receiving on interrupt without losing bytes](chapters/03-receiving-on-interrupt-without-losing-bytes.md) | Interrupt-driven receive, overrun, and two documented library failure modes |
| 4 | [Circular DMA and the idle line](chapters/04-circular-dma-and-the-idle-line.md) | Transfer-driven receive, variable length frames, the transfer counter |
| 5 | [Framing and the hardware CRC unit](chapters/05-framing-and-the-hardware-crc-unit.md) | Byte stuffing against a length prefix, CRC-16, and the peripheral's reversal settings |
| 6 | [Sampling on a timer at exactly 1 kHz](chapters/06-sampling-on-a-timer-at-exactly-1-khz.md) | Timer-triggered acquisition, proving the rate externally |
| 7 | [Stop mode, RTC wake, and a battery number](chapters/07-stop-mode.md) | Low-power modes, clock restore after wake, charge per cycle |
| 8 | [The node's state machine: sense, feature, and a transmit that is a stub](chapters/08-the-nodes-state-machine.md) | Application state machine, bare metal |
| 9 | [The payload codec and its Python twin](chapters/09-the-payload-codec-and-its-python-twin.md) | Bit packing, CBOR, round-trip property tests |
| 10 | [Where the energy goes: wake, sense, compute, send](chapters/10-where-the-energy-goes.md) | Marker pins, per-phase charge accounting |
| 11 | [An AT engine that never blocks](chapters/11-an-at-engine-that-never-blocks.md) | Asynchronous command queue, unsolicited results, timeouts, backoff |
| 12 | [Energy as a regression test, and the rig that runs it](chapters/12-energy-as-a-regression-test.md) | Host unit tests, size gate, hardware in the loop, watchdogs |
| 13 | [The IKS4A1: FIFO, watermark, interrupt](chapters/13-the-iks4a1.md) | Sensor FIFO, watermark interrupt, the three bus topologies |
| 14 | [The IKS5A1: low and high g at once, wide pressure](chapters/14-the-iks5a1.md) | Simultaneous acceleration ranges, dual full-scale barometer |
| 15 | [Eight by eight time of flight, decided on the MCU](chapters/15-eight-by-eight-time-of-flight.md) | Sensor firmware upload, zone reduction, hysteresis |
| 16 | [Where the classifier runs: sensor, MCU or host](chapters/16-where-the-classifier-runs.md) | In-sensor engine against on-MCU model against host, measured |
| 17 | [The same driver twice: vendor layer against registers](chapters/17-the-same-driver-twice.md) | Abstraction layers, code size and cycles, and the Rust variant |
| 18 | [Transforms and filters with a numerical acceptance test](chapters/18-transforms-and-filters-with-a-numerical-acceptance-test.md) | CMSIS-DSP, float32 against Q15, cycles, memory placement |
| 19 | [Caches, the MPU, and why DMA reads stale bytes](chapters/19-caches.md) | D-cache maintenance, MPU regions, which engine reaches which memory |
| 20 | [Three jobs at once: the node as an RTOS application](chapters/20-three-jobs-at-once.md) | Static allocation, synchronisation, measured latency distribution |
