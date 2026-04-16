// std/async/ticker.ts — Periodic ticker with drift compensation (RFC-0020)

/// Periodic ticker. Calls to tick() block until the next scheduled time.
/// Compensates for processing time drift.
struct Ticker {
  period_ms: u64,
  next_tick_ns: u64,
  stopped: bool,
}

impl Ticker {
  fn new(period_ms: u64): own<Ticker> {
    @extern("__asc_clock_monotonic")
    const now = clock_monotonic();
    return Ticker {
      period_ms: period_ms,
      next_tick_ns: now + period_ms * 1_000_000,
      stopped: false,
    };
  }

  fn tick(refmut<Self>): u64 {
    if self.stopped { panic!("Ticker: tick() called after stop()"); }

    loop {
      @extern("__asc_clock_monotonic")
      const now = clock_monotonic();
      if now >= self.next_tick_ns { break; }
    }

    const tick_ns = self.next_tick_ns;
    self.next_tick_ns = self.next_tick_ns + self.period_ms * 1_000_000;

    @extern("__asc_clock_monotonic")
    const now2 = clock_monotonic();
    if self.next_tick_ns < now2 {
      self.next_tick_ns = now2 + self.period_ms * 1_000_000;
    }

    return tick_ns;
  }

  fn reset(refmut<Self>): void {
    @extern("__asc_clock_monotonic")
    const now = clock_monotonic();
    self.next_tick_ns = now + self.period_ms * 1_000_000;
  }

  fn stop(refmut<Self>): void {
    self.stopped = true;
  }
}
