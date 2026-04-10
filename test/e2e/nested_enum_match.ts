// RUN: %asc check %s
// Test: enum match with nested conditionals.

enum Outer {
  A(i32),
  B(i32),
}

function classify(o: Outer): i32 {
  match o {
    Outer::A(x) => {
      if x > 0 { return 1; }
      return -1;
    },
    Outer::B(x) => {
      if x > 0 { return 2; }
      return -2;
    },
    _ => { return 0; },
  }
}

function main(): i32 {
  let r1 = classify(Outer::A(5));
  let r2 = classify(Outer::B(10));
  let r3 = classify(Outer::A(-1));
  return r1 + r2 + r3;
}
