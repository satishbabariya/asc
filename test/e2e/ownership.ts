// End-to-end test: ownership and borrowing.

struct Buffer {
  data: own<Vec<u8>>,
  len: usize,
}

function create_buffer(): own<Buffer> {
  return Buffer { data: Vec::new(), len: 0 };
}

function inspect(buf: ref<Buffer>): usize {
  return buf.len;
}

function fill(buf: refmut<Buffer>, value: u8): void {
  buf.len = buf.len + 1;
}

function main(): i32 {
  let buf = create_buffer();
  const len = inspect(&buf);
  fill(&buf, 0);
  return 0;
}
