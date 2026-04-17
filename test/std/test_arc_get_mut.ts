// RUN: %asc check %s
// Test: Arc::get_mut returns Some only when uniquely owned.
function main(): i32 {
  let a = Arc::new(10);

  // Unique — should be Some.
  match a.get_mut() {
    Option::Some(_) => {},
    Option::None => { return 1; },
  }

  // After cloning, no longer unique — should be None.
  const a2 = a.clone();
  match a.get_mut() {
    Option::Some(_) => { return 2; },
    Option::None => {},
  }

  return 0;
}
