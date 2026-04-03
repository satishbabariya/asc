// Test: struct with impl methods — Matrix determinant and trace.

struct Matrix2x2 {
  a: i32, b: i32,
  c: i32, d: i32,
}

impl Matrix2x2 {
  fn new(a: i32, b: i32, c: i32, d: i32): Matrix2x2 {
    return Matrix2x2 { a: a, b: b, c: c, d: d };
  }

  fn determinant(ref<Matrix2x2>): i32 {
    return self.a * self.d - self.b * self.c;
  }

  fn trace(ref<Matrix2x2>): i32 {
    return self.a + self.d;
  }
}

function main(): i32 {
  let m = Matrix2x2 { a: 3, b: 2, c: 1, d: 4 };
  let det: i32 = m.determinant();
  let tr: i32 = m.trace();
  return det + tr;
}
