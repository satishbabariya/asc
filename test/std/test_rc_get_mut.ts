// RUN: %asc check %s
// Test: Rc::get_mut returns Some only when uniquely owned.
function main(): i32 {
  let a = Rc::new(10);

  match a.get_mut() {
    Option::Some(_) => {},
    Option::None => { return 1; },
  }

  const a2 = a.clone();
  match a.get_mut() {
    Option::Some(_) => { return 2; },
    Option::None => {},
  }

  return 0;
}
