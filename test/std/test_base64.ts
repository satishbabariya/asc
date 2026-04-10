// RUN: %asc check %s
// Test: Base64 encoding and decoding.
function main(): i32 {
  const encoded = base64::encode("Hello, World!".as_bytes());
  assert_eq!(encoded.as_str(), "SGVsbG8sIFdvcmxkIQ==");

  const decoded = base64::decode("SGVsbG8sIFdvcmxkIQ==").unwrap();
  assert_eq!(decoded.len(), 13);

  // Empty input.
  const empty = base64::encode("".as_bytes());
  assert_eq!(empty.as_str(), "");

  return 0;
}
