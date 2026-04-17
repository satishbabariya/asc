// RUN: %asc check %s
// Test: thread::Scope compiles — spawns fn pointers and auto-joins on drop.
function worker(): i32 { return 42; }

function main(): i32 {
  let s: own<Scope<i32>> = Scope::new();
  s.spawn(worker);
  s.spawn(worker);
  const results = s.join_all();
  if results.len() != 2 { return 1; }
  return 0;
}
