// Test: Path manipulation.
function main(): i32 {
  assert_eq!(path::basename("/home/user/file.ts"), "file.ts");
  assert_eq!(path::dirname("/home/user/file.ts"), "/home/user");
  assert_eq!(path::extname("/home/user/file.ts"), ".ts");
  assert!(path::is_absolute("/usr/bin"));
  assert!(!path::is_absolute("relative/path"));
  const joined = path::join("/home", "user/file.ts");
  assert_eq!(joined.as_str(), "/home/user/file.ts");
  return 0;
}
