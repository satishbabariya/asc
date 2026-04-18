// RUN: %asc check %s
// Test: RFC 3986 percent-encoding (RFC-0018 §1.5).
//
// Exercises encode/encode_except/encode_path_segment/encode_query_component
// and decode/decode_lossy/decode_in_place. `asc check` parses and
// type-checks the call sites; runtime behavior is covered by the unit
// code inside `std/encoding/percent.ts`.

function main(): i32 {
  // --- encode: unreserved set passes through unchanged ---------------
  let a = percent::encode("AZaz09-._~");
  assert_eq!(a.as_str(), "AZaz09-._~");

  // Space and special characters are %-encoded with uppercase hex.
  let b = percent::encode("Hello, World!");
  assert_eq!(b.as_str(), "Hello%2C%20World%21");

  // Empty input yields empty output.
  let c = percent::encode("");
  assert_eq!(c.as_str(), "");

  // --- encode_except: caller extends the safe set --------------------
  // '/' is reserved, but allowed here — should pass through.
  let p = percent::encode_except("path/to/file", "/");
  assert_eq!(p.as_str(), "path/to/file");

  // '&' and '=' kept, space still encoded.
  let q = percent::encode_except("a=1&b=2 x", "&=");
  assert_eq!(q.as_str(), "a=1&b=2%20x");

  // --- component helpers --------------------------------------------
  // Path segment: '/' MUST be encoded, ':' ok.
  let seg = percent::encode_path_segment("a:b/c");
  assert_eq!(seg.as_str(), "a:b%2Fc");

  // Query component: '&' and '=' MUST be encoded, '/' ok.
  let qc = percent::encode_query_component("k=v&x/y");
  assert_eq!(qc.as_str(), "k%3Dv%26x/y");

  // --- decode: valid sequences ---------------------------------------
  let d = percent::decode("Hello%2C%20World%21").unwrap();
  assert_eq!(d.as_str(), "Hello, World!");

  // Lowercase hex digits accepted on decode.
  let e = percent::decode("a%2fb").unwrap();
  assert_eq!(e.as_str(), "a/b");

  // Round-trip of unreserved characters is identity.
  let rt = percent::decode(percent::encode("AZaz09-._~").as_str()).unwrap();
  assert_eq!(rt.as_str(), "AZaz09-._~");

  // --- decode: errors ------------------------------------------------
  // Truncated escape: only one hex digit after '%'.
  assert!(percent::decode("abc%2").is_err());
  // Non-hex character after '%'.
  assert!(percent::decode("abc%ZZ").is_err());
  // Decoded bytes are invalid UTF-8 (lone 0xFF).
  assert!(percent::decode("%FF").is_err());

  // --- decode_lossy: survives bad input ------------------------------
  // Valid UTF-8 round-trips unchanged.
  let ok = percent::decode_lossy("Hello%20World");
  assert_eq!(ok.as_str(), "Hello World");

  // Invalid UTF-8 is replaced with U+FFFD (3 bytes EF BF BD).
  let lossy = percent::decode_lossy("%FF");
  assert_eq!(lossy.len(), 3);

  // Malformed escape (too short) — '%' passed through literally,
  // rest is valid so no replacement needed.
  let partial = percent::decode_lossy("a%2");
  assert_eq!(partial.as_str(), "a%2");

  // --- decode_in_place: collapses on the same buffer -----------------
  let buf: own<Vec<u8>> = Vec::new();
  buf.push(0x61); // 'a'
  buf.push(0x25); // '%'
  buf.push(0x32); // '2'
  buf.push(0x30); // '0'
  buf.push(0x62); // 'b'
  assert!(percent::decode_in_place(buf).is_ok());
  assert_eq!(buf.len(), 3);
  assert_eq!(buf[0], 0x61); // 'a'
  assert_eq!(buf[1], 0x20); // ' '
  assert_eq!(buf[2], 0x62); // 'b'

  // Malformed in-place decode reports the offending offset.
  let bad: own<Vec<u8>> = Vec::new();
  bad.push(0x25); // '%'
  bad.push(0x5A); // 'Z' — not a hex digit
  bad.push(0x31); // '1'
  assert!(percent::decode_in_place(bad).is_err());

  return 0;
}
