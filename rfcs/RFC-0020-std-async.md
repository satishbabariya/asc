# RFC-0020 — Std: Async Utilities

| Field       | Value                                              |
|-------------|----------------------------------------------------|
| Status      | Accepted                                           |
| Depends on  | RFC-0007, RFC-0011, RFC-0014                       |
| Module path | `std::async`                                       |
| Inspired by | Deno `@std/async` (retry, debounce, deadline, MuxAsyncIterator, Semaphore) |

## Summary

`std::async` provides higher-level concurrency utilities built on top of the `task`/`chan`
primitives from RFC-0007 and `std::sync`. These are patterns that appear in every
production codebase — retry with backoff, debouncing, rate limiting, deadlines, task
pools — abstracted into composable, ownership-safe APIs.

Note: this module uses the word "async" in the module name for familiarity, but the
underlying model is always synchronous ownership + channels. There is no `async/await`
executor or hidden runtime. Every "async" operation in this module is either a blocking
call or a `task.spawn` under the hood.

---

## 1. `Semaphore` — concurrency limiter

A `Semaphore` limits the number of tasks that can proceed concurrently through a section.
It is the natural companion to `Mutex` for rate-limiting (Mutex = 1 permit, Semaphore = N).

```typescript
class Semaphore {
  static new(permits: usize): own<Semaphore>;

  // Acquire a permit. Blocks until one is available.
  // Returns a SemaphoreGuard — dropping the guard releases the permit.
  fn acquire(ref<Semaphore>): SemaphoreGuard;

  // Try to acquire without blocking. Returns None if no permits available.
  fn try_acquire(ref<Semaphore>): Option<SemaphoreGuard>;

  // Acquire a permit with a timeout.
  fn acquire_timeout(ref<Semaphore>, timeout: Duration): Option<SemaphoreGuard>;

  fn available_permits(ref<Semaphore>): usize;
  fn total_permits(ref<Semaphore>): usize;
}

struct SemaphoreGuard {
  // Drop: releases one permit back to the semaphore
  // NOT Clone, NOT Copy — exactly one permit per guard
}

// Usage: limit to 4 concurrent HTTP requests
const sem = Semaphore::new(4);
for (const url of urls) {
  const guard = sem.acquire();  // blocks if 4 already in flight
  task.spawn(move || {
    const _g = guard;           // hold guard for lifetime of task
    fetch(url)
  });
}
```

---

## 2. `retry` — automatic retry with backoff

```typescript
@copy
struct RetryConfig {
  max_attempts:  u32,       // total attempts including first. Default: 3
  initial_delay: Duration,  // delay before second attempt. Default: 100ms
  multiplier:    f64,       // backoff multiplier. Default: 2.0
  max_delay:     Duration,  // cap on delay. Default: 30s
  jitter:        f64,       // random fraction of delay added: [0.0, 1.0]. Default: 0.1
}

const DEFAULT_RETRY: RetryConfig = RetryConfig {
  max_attempts: 3,
  initial_delay: Duration::from_millis(100),
  multiplier: 2.0,
  max_delay: Duration::from_secs(30),
  jitter: 0.1,
};

// Retry a fallible operation with exponential backoff.
// f is called up to max_attempts times.
// Returns Ok(result) on first success, or Err(last_error) after all attempts.
function retry<T, E>(
  config: RetryConfig,
  f: own<impl FnMut() -> Result<own<T>, own<E>>>
): Result<own<T>, own<E>>;

// Retry with a custom should_retry predicate.
// If should_retry returns false, stop immediately and return the error.
function retry_if<T, E>(
  config: RetryConfig,
  f: own<impl FnMut() -> Result<own<T>, own<E>>>,
  should_retry: ref<E> -> bool
): Result<own<T>, own<E>>;

// Usage
const result = retry(DEFAULT_RETRY, || {
  http_get("https://api.example.com/data")
})?;
```

---

## 3. `deadline` — timeout enforcement

```typescript
// Run a function in a spawned task; cancel and return Err if it exceeds duration.
// The task is killed (not cooperatively cancelled) when the deadline expires.
// Note: the spawned task must be Send.
function deadline<T: Send>(
  duration: Duration,
  f: own<impl FnOnce() -> own<T> + Send>
): Result<own<T>, DeadlineError>;

// Same but with a specific instant rather than duration from now.
function deadline_at<T: Send>(
  instant: Instant,
  f: own<impl FnOnce() -> own<T> + Send>
): Result<own<T>, DeadlineError>;

enum DeadlineError {
  Exceeded { duration: Duration },
  TaskPanicked(PanicInfo),
}

// Usage
const result = deadline(Duration::from_secs(5), || {
  slow_computation()
})?;
```

---

## 4. `debounce` — collapse rapid calls

```typescript
// Debounced function wrapper. Calls to the inner function are collapsed:
// only the LAST call within `delay` of inactivity is actually executed.
// T = argument type, R = return type.
class Debounced<T: Send, R: Send> {
  static new(
    delay: Duration,
    f: own<impl Fn(own<T>) -> own<R> + Send>
  ): own<Debounced<T, R>>;

  // Schedule a call. If another call arrives within `delay`, this one is cancelled.
  // Returns a channel receiver that yields the result when it eventually runs.
  fn call(ref<Debounced<T, R>>, arg: own<T>): Receiver<R>;

  // Cancel any pending call.
  fn cancel(ref<Debounced<T, R>>): void;

  // Flush: execute immediately regardless of delay.
  fn flush(ref<Debounced<T, R>>): void;
}

// Simpler free-function form for one-off debouncing:
// Call f at most once per `delay`, always executing the most recent call.
function debounce<T: Send>(
  delay: Duration,
  f: own<impl Fn(own<T>) + Send>
): own<impl Fn(own<T>)>;
```

---

## 5. `throttle` — rate limiting

```typescript
// Throttled function: executes at most once per `interval`.
// Unlike debounce (which delays to the trailing edge), throttle fires on
// the LEADING edge and ignores calls during the cooldown window.
function throttle<T: Send>(
  interval: Duration,
  f: own<impl Fn(own<T>) + Send>
): own<impl Fn(own<T>)>;

// Token bucket rate limiter: allows `capacity` calls total,
// replenishing `refill_rate` tokens per second.
class RateLimiter {
  static new(capacity: u32, refill_rate: f64): own<RateLimiter>;

  // Try to consume one token. Returns true if allowed.
  fn try_acquire(ref<RateLimiter>): bool;

  // Block until a token is available.
  fn acquire(ref<RateLimiter>): void;

  // Consume n tokens at once. Returns false if not enough tokens.
  fn try_acquire_n(ref<RateLimiter>, n: u32): bool;
}
```

---

## 6. `pool` — worker task pool

```typescript
// A fixed pool of worker tasks that process jobs from a shared queue.
class TaskPool<Job: Send, Result: Send> {
  // Create a pool of `workers` tasks, each running `worker_fn`.
  static new(
    workers: usize,
    worker_fn: own<impl Fn(own<Job>) -> own<Result> + Send + Clone>
  ): own<TaskPool<Job, Result>>;

  // Submit a job. Returns a receiver for the result.
  fn submit(ref<TaskPool<Job, Result>>, job: own<Job>): Receiver<Result>;

  // Submit and block until result is ready.
  fn submit_sync(ref<TaskPool<Job, Result>>, job: own<Job>): own<Result>;

  // Submit many jobs, collect all results in order.
  fn map<I: IntoIterator<Item=own<Job>>>(
    ref<TaskPool<Job, Result>>,
    jobs: own<I>
  ): own<Vec<Result>>;

  // Drain the pool: wait for all in-flight jobs to complete.
  fn drain(ref<TaskPool<Job, Result>>): void;

  // Current queue depth.
  fn queued(ref<TaskPool<Job, Result>>): usize;

  // Number of idle workers.
  fn idle_workers(ref<TaskPool<Job, Result>>): usize;
}

// Usage: parallel image processing with 4 workers
const pool = TaskPool::new(4, |img: own<ImageBuffer>| {
  compress(img)
});
const results = pool.map(images);
```

---

## 7. `once` — lazy one-time initialization

```typescript
// Run a potentially-expensive initialization exactly once, even under concurrent access.
// On second and subsequent calls, returns a borrow to the already-computed value.
// This is the lazy-static pattern.
class LazyCell<T> {
  static new(init: own<impl FnOnce() -> own<T>>): own<LazyCell<T>>;

  // Initialize if not yet done; return borrow to value.
  fn get(ref<LazyCell<T>>): ref<T>;

  // Has initialization completed?
  fn is_initialized(ref<LazyCell<T>>): bool;

  // Consume the LazyCell, returning the inner value (initializes if needed).
  fn into_inner(own<LazyCell<T>>): own<T>;
}

// Thread-safe version backed by Once from std::sync.
class LazyLock<T: Send + Sync> {
  static new(init: own<impl FnOnce() -> own<T> + Send>): own<LazyLock<T>>;
  fn get(ref<LazyLock<T>>): ref<T>;
  fn into_inner(own<LazyLock<T>>): own<T>;
}

// Usage: expensive config parsed once on first access
static CONFIG: LazyLock<AppConfig> = LazyLock::new(|| {
  AppConfig::from_env().unwrap()
});

fn get_timeout(): Duration {
  CONFIG.get().timeout
}
```

---

## 8. `interval` and `ticker` — time-based iteration

```typescript
// Tick every `period`, returning successive Instants.
// The ticker tries to maintain the period even if processing takes time
// (it compensates for drift).
class Ticker {
  static new(period: Duration): own<Ticker>;

  // Block until the next tick. Returns the tick Instant.
  fn tick(ref<Ticker>): Instant;

  // Reset the ticker (next tick is `period` from now).
  fn reset(ref<Ticker>): void;

  // Stop the ticker.
  fn stop(own<Ticker>): void;
}

// Run a function at a fixed interval. Blocks the current thread.
// Stops when f returns false.
function interval<F: FnMut(Instant) -> bool>(period: Duration, f: own<F>): void;

// Run in a spawned task (non-blocking).
function interval_task(
  period: Duration,
  f: own<impl FnMut(Instant) + Send>
): Thread<void>;
```

---

## 9. `Duration` and `Instant` types

These are needed by the async utilities and belong in `std::time`, but are re-exported
from `std::async` for convenience.

```typescript
// In std::time (auto-imported):
@copy
struct Duration {
  static fn from_secs(secs: u64): Duration;
  static fn from_millis(ms: u64): Duration;
  static fn from_micros(us: u64): Duration;
  static fn from_nanos(ns: u64): Duration;

  fn as_secs(Duration): u64;
  fn as_millis(Duration): u128;
  fn as_micros(Duration): u128;
  fn as_nanos(Duration): u128;
  fn subsec_nanos(Duration): u32;

  // Arithmetic (all @copy — no allocation)
  // + - * / operators defined
  // saturating_add, saturating_sub, checked_add, checked_sub
}

@copy
struct Instant {
  static fn now(): Instant;
  fn elapsed(Instant): Duration;
  fn duration_since(Instant, earlier: Instant): Duration;
  fn checked_add(Instant, d: Duration): Option<Instant>;
  fn checked_sub(Instant, d: Duration): Option<Instant>;
  // + Duration, - Duration, - Instant operators defined
}

const ZERO: Duration = Duration::from_nanos(0);
const MAX: Duration;
```

---

## 10. Module layout

```
std::async
├── Semaphore, SemaphoreGuard
├── retry, retry_if, RetryConfig, DEFAULT_RETRY
├── deadline, deadline_at, DeadlineError
├── debounce (free fn), Debounced
├── throttle, RateLimiter
├── TaskPool
├── LazyCell, LazyLock
├── Ticker, interval, interval_task
└── re-exports: Duration, Instant (from std::time)

std::time
├── Duration
├── Instant
└── SystemTime (wall clock, not monotonic — for timestamps only)
```

Import pattern:

```typescript
import { retry, DEFAULT_RETRY } from 'std/async';
import { Semaphore } from 'std/async';
import { TaskPool } from 'std/async';
import { LazyLock } from 'std/async';
import { Duration, Instant } from 'std/time';
```

---

## 11. Design decisions

| Decision | Choice | Rationale |
|---|---|---|
| No `async/await` | All blocking or `task.spawn` | Consistent with no-executor model from RFC-0007 |
| `Debounced` returns `Receiver<R>` | Yes | Caller can optionally wait for result |
| `deadline` kills the task | Yes | No cooperative cancellation in our model; deterministic termination |
| `LazyLock` uses `Once` from sync | Yes | Thread-safety without re-implementing the state machine |
| `TaskPool` fixed size | Yes | Static stack sizing requires known thread count (RFC-0007 §stack analysis) |
| `Duration`/`Instant` in `std::time` | Yes | Separate module; re-exported from `std::async` for convenience |
| Jitter on retry | Default 10% | Prevents thundering herd; common production default |
