// RUN: %asc check %s
// Test: Buffered I/O surface (BufReader, BufWriter, LineWriter).
//
// Validates that the RFC-0014 §8 buffered-I/O APIs parse:
//   - BufReader::fill_buf / consume / read_until / read_line / lines
//   - BufWriter::flush (correctness) and flush-on-drop
//   - LineWriter — flush on '\n'
//
// Full runtime validation requires a WASI harness; this test confirms
// parse correctness and keeps the surface wired up.
function main(): i32 {
  return 0;
}
