# TODO

## Library Improvements
- Implement channel closing semantics so producers can signal completion without sentinels.
- Add non-blocking `trySend` / `tryReceive` APIs for polling scenarios.
- Provide timed send/receive operations to support cancellation and timeouts.
- Evaluate fairness and performance; consider replacing atomics with scoped counters guarded by mutex to prevent priority inversion.

## Testing & Tooling
- Add stress tests covering high contention, slow consumers, and buffer edge cases.
- Introduce benchmarks to measure throughput and latency versus reference implementations.
- Wire the tests into CI and run under thread sanitizers and sanitizers for data races.

## Documentation
- Document channel lifecycle, blocking guarantees, and example usage in `README.md`.
- Outline contribution guidelines and coding conventions once they solidify.
