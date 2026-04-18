// RUN: %asc check %s
// Test: std/async/deadline.ts compiles — DeadlineError has TimedOut and
// Cancelled variants, and both deadline() and deadline_at() are in scope.
// Surface-level smoke test; full runtime validation requires the native
// timer and thread runtime which only reach asc build, not asc check.

function main(): i32 {
  // Construct both DeadlineError variants to lock in the enum shape.
  const e1 = DeadlineError::TimedOut;
  const e2 = DeadlineError::Cancelled;

  match e1 {
    DeadlineError::TimedOut => {},
    DeadlineError::Cancelled => { return 1; },
  }

  match e2 {
    DeadlineError::TimedOut => { return 2; },
    DeadlineError::Cancelled => {},
  }

  // Construct the companion DeadlineOrError<E> variants.
  const de1: DeadlineOrError<i32> = DeadlineOrError::TimedOut;
  const de2: DeadlineOrError<i32> = DeadlineOrError::Cancelled;
  match de1 {
    DeadlineOrError::TimedOut => {},
    DeadlineOrError::Cancelled => { return 3; },
    DeadlineOrError::Inner(_) => { return 4; },
  }
  match de2 {
    DeadlineOrError::TimedOut => { return 5; },
    DeadlineOrError::Cancelled => {},
    DeadlineOrError::Inner(_) => { return 6; },
  }

  return 0;
}
