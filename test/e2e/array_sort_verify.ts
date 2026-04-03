// Test: array operations — init, modify, verify.

function main(): i32 {
  let arr = [9, 7, 5, 3, 1];

  // Reverse manually
  let tmp: i32 = arr[0];
  arr[0] = arr[4];
  arr[4] = tmp;
  tmp = arr[1];
  arr[1] = arr[3];
  arr[3] = tmp;

  // Now: [1, 3, 5, 7, 9]
  let ok: i32 = 0;
  if arr[0] == 1 { ok = ok + 1; }
  if arr[1] == 3 { ok = ok + 1; }
  if arr[2] == 5 { ok = ok + 1; }
  if arr[3] == 7 { ok = ok + 1; }
  if arr[4] == 9 { ok = ok + 1; }
  return ok;
}
