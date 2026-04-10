// RUN: %asc check %s
// End-to-end test: ownership and borrowing.

struct Buffer {
  data: own<Vec<i32>>,
  len: i32,
}

function create_buffer(): own<Buffer> {
  return Buffer { data: Vec::new(), len: 0 };
}

function inspect(buf: ref<Buffer>): i32 {
  return buf.len;
}

function fill(buf: refmut<Buffer>, value: i32): void {
  buf.len = buf.len + 1;
}

function main(): i32 {
  let buf = create_buffer();
  let len: i32 = inspect(buf);
  fill(buf, 0);
  return len;
}
