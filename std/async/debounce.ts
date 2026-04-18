// std/async/debounce.ts — Debounced function wrapper (RFC-0020)

import { Mutex } from '../sync/mutex';
import { AtomicU64, AtomicBool } from '../sync/atomic';

/// A debounced function wrapper. Delays invocation of `f: T -> R` until
/// `delay_ms` has elapsed since the **last** call. Each new `.call(arg)`
/// resets the timer; only the most recent argument is used when the
/// deferred invocation finally fires.
///
/// Implementation uses `task.spawn` to run a background waiter that
/// wakes up after `delay_ms` and checks a generation counter. If a
/// newer call arrived while the waiter was sleeping, the waiter exits
/// without firing, leaving the job to the more recent waiter.
///
/// T = argument type, R = return type. Both must be `Send` so they
/// can cross the spawned task boundary.
struct Debounced<T: Send, R: Send> {
  func: own<(own<T>) -> own<R>>,
  delay_ms: u64,
  /// Monotonically increasing generation; each `.call()` bumps this.
  /// A waiter only fires if its captured generation matches.
  generation: own<AtomicU64>,
  /// The most recent argument; waiter consumes it on fire.
  pending_arg: own<Mutex<Option<own<T>>>>,
  /// True while at least one waiter is scheduled.
  scheduled: own<AtomicBool>,
}

impl<T: Send, R: Send> Debounced<T, R> {
  /// Create a new debounced wrapper with the given delay.
  fn new(f: own<(own<T>) -> own<R>>, delay_ms: u64): own<Debounced<T, R>> {
    return Debounced {
      func: f,
      delay_ms: delay_ms,
      generation: AtomicU64::new(0),
      pending_arg: Mutex::new(Option::None),
      scheduled: AtomicBool::new(false),
    };
  }

  /// Schedule a call. Resets the debounce timer — the inner function
  /// will only actually fire once `delay_ms` has elapsed with no
  /// subsequent call. If more calls arrive within the window, only the
  /// most recent argument survives.
  fn call(ref<Self>, arg: own<T>): void {
    // Stash the new argument (dropping any previously-pending one).
    let guard = self.pending_arg.lock();
    *guard = Option::Some(arg);

    // Bump generation so any in-flight waiters become stale.
    let gen = self.generation.fetch_add(1) + 1;
    self.scheduled.store(true);

    // Spawn a ticker-style waiter. It sleeps for delay_ms then checks
    // whether it is still the newest waiter before firing.
    let delay = self.delay_ms;
    let gen_ref = ref self.generation;
    let arg_ref = ref self.pending_arg;
    let func_ref = ref self.func;
    let scheduled_ref = ref self.scheduled;
    task.spawn(|| {
      sleep_ms(delay);
      // Only fire if our generation is still the newest.
      if gen_ref.load() == gen {
        let g = arg_ref.lock();
        let taken = *g;
        *g = Option::None;
        scheduled_ref.store(false);
        match taken {
          Option::Some(a) => { (*func_ref)(a); },
          Option::None => {},
        }
      }
    });
  }

  /// Cancel any pending invocation without firing it.
  fn cancel(ref<Self>): void {
    // Bumping generation invalidates any sleeping waiters.
    self.generation.fetch_add(1);
    let guard = self.pending_arg.lock();
    *guard = Option::None;
    self.scheduled.store(false);
  }

  /// Immediately invoke with the most-recent pending argument and
  /// cancel the scheduled waiter. Returns `Some(result)` if a pending
  /// argument was available, `None` otherwise.
  fn flush(ref<Self>): Option<own<R>> {
    // Invalidate any sleeping waiter so it can't double-fire.
    self.generation.fetch_add(1);
    self.scheduled.store(false);

    let guard = self.pending_arg.lock();
    let taken = *guard;
    *guard = Option::None;
    match taken {
      Option::Some(arg) => {
        let r = (self.func)(arg);
        return Option::Some(r);
      },
      Option::None => { return Option::None; },
    }
  }

  /// Return true if a call is currently pending (waiter scheduled).
  fn is_pending(ref<Self>): bool {
    return self.scheduled.load();
  }
}

/// Convenience free function: create a debounced wrapper.
function debounce<T: Send, R: Send>(
  f: own<(own<T>) -> own<R>>,
  delay_ms: u64,
): own<Debounced<T, R>> {
  return Debounced::new(f, delay_ms);
}

// --- Runtime-provided timer functions ---

@extern("env", "__asc_sleep_ms")
declare function sleep_ms(ms: u64): void;
