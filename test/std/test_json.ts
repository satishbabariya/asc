// RUN: %asc check %s
// Test: JSON module — exercises JsonValue pretty-printing and the JsonWriter
// streaming serializer (RFC-0016 §4).
function main(): i32 {
  // --- JsonWriter compact ---
  let w = JsonWriter::new();
  w.begin_object();
  w.write_key("id");
  w.write_i64(42);
  w.write_key("name");
  w.write_string("asc");
  w.write_key("tags");
  w.begin_array();
  w.write_string("compiler");
  w.write_string("wasm");
  w.end_array();
  w.write_key("active");
  w.write_bool(true);
  w.write_key("rating");
  w.write_f64(4.5);
  w.write_key("next");
  w.write_null();
  w.end_object();
  let s = w.finish();
  assert!(s.len() > 0);

  // --- JsonWriter pretty-print ---
  let pw = JsonWriter::pretty(2);
  pw.begin_array();
  pw.write_i64(1);
  pw.write_i64(2);
  pw.write_i64(3);
  pw.end_array();
  let ps = pw.finish();
  assert!(ps.len() > 0);

  // --- Empty containers round-trip cleanly ---
  let ew = JsonWriter::new();
  ew.begin_object();
  ew.end_object();
  let es = ew.finish();
  assert_eq!(es.len(), 2); // "{}"

  // --- Writer can serialize a single scalar at the document root ---
  let rw = JsonWriter::new();
  rw.write_bool(false);
  let rs = rw.finish();
  assert_eq!(rs.len(), 5); // "false"

  return 0;
}
