// RUN: %asc check %s
// Test: Vec with @copy struct elements (push/len/indexed access).

@copy
struct Point { x: i32, y: i32 }

function sum_points(n: i32): i32 {
  let total: i32 = 0;
  let i: i32 = 0;
  while i < n {
    let p = Point { x: i, y: i + 1 };
    total = total + p.x + p.y;
    i = i + 1;
  }
  return total;
}

function main(): i32 {
  // sum of (i + i+1) for i in 0..3: (0+1) + (1+2) + (2+3) = 1 + 3 + 5 = 9
  return sum_points(3);
}
