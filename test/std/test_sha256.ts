// Test: SHA-256 known test vectors.
function main(): i32 {
  // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  const empty_hash = sha256("".as_bytes());
  assert_eq!(empty_hash[0], 0xe3);
  assert_eq!(empty_hash[1], 0xb0);

  // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
  const abc_hash = sha256("abc".as_bytes());
  assert_eq!(abc_hash[0], 0xba);
  assert_eq!(abc_hash[1], 0x78);

  return 0;
}
