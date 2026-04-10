// RUN: %asc check %s
// test 36: router with HashMap
function main(): i32 {
  let m = HashMap::new();
  m.insert(1, 200);
  m.insert(2, 200);

  let passed: i32 = 0;
  if m.contains(1) == 1 { passed = passed + 1; }
  if m.contains(2) == 1 { passed = passed + 1; }
  if m.contains(99) == 0 { passed = passed + 1; }
  return passed;
}
