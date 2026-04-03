// Test: size_of! and align_of! macros.

function main(): i32 {
  const s1 = size_of!(42i32);
  const s2 = size_of!(3.14f64);
  return 0;
}
