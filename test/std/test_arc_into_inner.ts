// RUN: %asc check %s
// Test: Arc::into_inner returns Some only when uniquely owned; drops the Arc otherwise.
function main(): i32 {
  const a = Arc::new(42);
  match a.into_inner() {
    Option::Some(v) => { if v != 42 { return 1; } },
    Option::None => { return 2; },
  }

  const b = Arc::new(99);
  const b2 = b.clone();
  match b.into_inner() {
    Option::Some(_) => { return 3; },
    Option::None => {},
  }

  return 0;
}
