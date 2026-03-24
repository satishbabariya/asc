# Decision — No Garbage Collector

| Field | Value |
|---|---|
| Date | 2025 |
| Status | Accepted |
| Related RFC | RFC-0001, RFC-0005, RFC-0008 |

## Decision

**The compiler produces no GC heap, no safepoints, no write barriers, and no GC roots.
Memory is managed entirely by compile-time ownership enforcement.**

## Reasons

1. **Deterministic performance.** GC pauses are fundamentally incompatible with real-time
   Wasm workloads (audio processing, game loops, simulation). Ownership gives deterministic
   drop timing — a value is freed exactly when it goes out of scope.

2. **Wasm GC proposal immaturity.** The Wasm GC proposal (as of 2024) does not support
   custom GC algorithms, finalization ordering, or integration with host GC cycles. Using it
   would lock the compiler to a specific GC strategy with no escape hatch.

3. **No runtime dependency.** A GC requires a runtime thread (or stop-the-world pauses) and
   a heap implementation. Ownership requires neither — the compiler inserts all memory
   management at compile time, resulting in a zero-runtime-overhead model.

4. **Fearless concurrency.** GC and concurrency interact badly — either you need a
   concurrent GC (complex, high overhead) or stop-the-world with all threads (kills
   parallelism). Ownership-based Send/Sync makes data-race freedom a compile-time property,
   requiring no runtime bookkeeping.

## Consequences

- Types must be designed with ownership in mind. Cycles of owned values are not possible
  without explicit weak references (a future RFC).
- Users coming from TypeScript/JavaScript will need to learn ownership concepts at function
  boundaries. The hybrid inference model (RFC-0002) minimises annotation burden.
- Self-referential data structures (linked lists, trees with parent pointers) require
  careful design. This is a known limitation; the standard library provides ownership-safe
  alternatives.
