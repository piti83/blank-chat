# BT-06 methodology — end-to-end latency of CBR and Poisson

## Research question

How does the selected traffic-shaping mechanism (CBR vs Poisson) affect user-observed end-to-end message latency in Blank Chat when messages are not intentionally queued behind other benchmark messages?

## Separation from BT-05

BT-05 remains the volumetric-overhead benchmark. It intentionally queues 1000 user messages quickly and measures total Tor OR traffic for the resulting loaded system.

BT-06 is a separate experiment because latency measured on the saturated BT-05 backlog would primarily describe benchmark-induced queue growth rather than isolated user-message delivery latency.

## Workload

For each mode:

- 1000 messages by default,
- exactly 150 B of user payload per message,
- periodic PFS rotation disabled during the measured series,
- CBR: `cbr_interval_ms = 10000`,
- Poisson: `poisson_lambda = 0.1`.

Only one benchmark user message may be outstanding at a time. The next message is queued only after the sender observes the end-to-end ACK for the previous message. The analyzer verifies the invariant

`QUEUE(i+1) >= ACK(i)`

using sender-VM timestamps.

## Phase randomization

Before each `QUEUE` event, the orchestrator waits a deterministic pseudorandom interval drawn uniformly from `[0, mean scheduler interval)`. The seed is stored in the result metadata.

This delay is applied before `QUEUE`, therefore it is excluded from the measured latency. Its purpose is to prevent all samples from being created at a fixed phase relative to the scheduler tick.

## Timestamps and primary metric

Diagnostics retain four events per benchmark message:

- `QUEUE`: application accepted the user message into the sender outbox,
- `TX`: the corresponding serialized PUSH frame is submitted to `async_write`,
- `RX`: the receiver has received, decrypted and processed the message,
- `ACK`: the sender has received the end-to-end ACK.

The primary thesis metric is user-observed end-to-end latency:

`L = corrected(RX) - corrected(QUEUE)`

It is decomposed into:

- scheduler delay: `QUEUE -> TX`,
- post-transmission delivery delay: `TX -> RX`.

Thus:

`QUEUE -> RX = (QUEUE -> TX) + (TX -> RX)`.

## Clock correction

Both client VM clocks are calibrated against the same host clock immediately before and after the measured series. Each calibration uses multiple samples and selects the minimum-RTT midpoint estimate. VM-minus-host offset is linearly interpolated across the measured interval.

The reported clock uncertainty is retained with every sample and is derived from half of the measured calibration RTT at each endpoint.

## Integrity checks

The run is considered methodologically valid only when all of the following hold:

1. exactly N `QUEUE`, `TX`, `RX` and `ACK` events are present,
2. all four sets contain the same unique message IDs,
3. every benchmark PUSH frame maps unambiguously to one message ID,
4. every benchmark frame has exactly one matched TX event,
5. sequence numbers are contiguous,
6. `QUEUE(i+1) >= ACK(i)` for every consecutive pair,
7. all measured timestamps lie inside the pre/post clock-calibration window,
8. latency decomposition is numerically consistent.

`PASS` therefore refers to experimental integrity and correct execution, not to a predetermined maximum latency.

## Reported statistics

For each mode the analyzer reports at least:

- sample count,
- mean,
- median,
- P95,
- standard deviation,
- minimum,
- maximum.

The same descriptive statistics are also reported for the `QUEUE -> TX` and `TX -> RX` components. Histogram, ECDF and boxplot use the primary `QUEUE -> RX` metric.
