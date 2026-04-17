// RUN: %asc check %s
// Test: Box::into_raw returns the underlying pointer without freeing,
//       and Box::from_raw reconstructs an owned Box from it.
function main(): i32 {
  const b = Box::new(99);
  const raw = Box::into_raw(b);
  const b2 = unsafe { Box::from_raw(raw) };
  if b2.as_ref().clone() != 99 { return 1; }
  return 0;
}
