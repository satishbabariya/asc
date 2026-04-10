// RUN: %asc check %s
// Test: type alias with struct and function.

type Int = i32;
type Score = i32;

function add_scores(a: Score, b: Score): Score {
  return a + b;
}

function main(): Int {
  let x: Score = 20;
  let y: Score = 22;
  return add_scores(x, y);
}
