// RUN: %asc check %s

struct Celsius { value: f64 }

function main(): i32 {
  let c = Celsius { value: 100.0 };
  return 0;
}
