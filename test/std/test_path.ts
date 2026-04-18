// RUN: %asc check %s
// Test: Path manipulation (POSIX and Windows variants).
function main(): i32 {
  // POSIX
  assert_eq!(path::basename("/home/user/file.ts"), "file.ts");
  assert_eq!(path::dirname("/home/user/file.ts"), "/home/user");
  assert_eq!(path::extname("/home/user/file.ts"), ".ts");
  assert!(path::is_absolute("/usr/bin"));
  assert!(!path::is_absolute("relative/path"));
  const joined = path::join("/home", "user/file.ts");
  assert_eq!(joined.as_str(), "/home/user/file.ts");

  // --- Windows ---

  // is_absolute
  assert!(windows::is_absolute("C:\\Windows"));
  assert!(windows::is_absolute("C:/Windows"));
  assert!(windows::is_absolute("\\\\server\\share"));
  assert!(windows::is_absolute("//server/share"));
  assert!(!windows::is_absolute("C:foo"));           // drive-relative
  assert!(!windows::is_absolute("relative\\path"));
  assert!(!windows::is_absolute("\\rooted-no-drive"));
  assert!(windows::is_relative("foo\\bar"));

  // drive_letter — uppercased
  const d1 = windows::drive_letter("c:\\a");
  match d1 {
    Option::Some(c) => { assert_eq!(c, 'C'); },
    Option::None => { assert!(false); },
  }
  const d2 = windows::drive_letter("foo");
  assert!(d2.is_none());

  // UNC detection
  assert!(windows::is_unc("\\\\srv\\share"));
  assert!(windows::is_unc("//srv/share"));
  assert!(!windows::is_unc("C:\\foo"));

  // basename — drive, UNC, trailing sep
  assert_eq!(windows::basename("C:\\Users\\admin\\file.txt"), "file.txt");
  assert_eq!(windows::basename("C:\\Users\\admin\\"), "admin");
  assert_eq!(windows::basename("\\\\srv\\share\\dir\\f.txt"), "f.txt");
  assert_eq!(windows::basename("file.txt"), "file.txt");
  assert_eq!(windows::basename("C:\\"), "");
  assert_eq!(windows::basename("C:"), "");

  // dirname — drive, UNC, root, relative
  assert_eq!(windows::dirname("C:\\Users\\admin\\file.txt"), "C:\\Users\\admin");
  assert_eq!(windows::dirname("C:\\file.txt"), "C:\\");
  assert_eq!(windows::dirname("\\\\srv\\share\\dir\\f.txt"), "\\\\srv\\share\\dir");
  assert_eq!(windows::dirname("file.txt"), ".");
  assert_eq!(windows::dirname("C:foo"), "C:");

  // extname / stem
  assert_eq!(windows::extname("C:\\tmp\\archive.tar.gz"), ".gz");
  assert_eq!(windows::stem("C:\\tmp\\archive.tar.gz"), "archive.tar");
  assert_eq!(windows::extname(".hidden"), "");
  assert_eq!(windows::extname("no_ext"), "");

  // normalize — mixed separators, `.` and `..`
  assert_eq!(windows::normalize("C:/Users/./admin/../public"), "C:\\Users\\public");
  assert_eq!(windows::normalize("foo\\bar\\..\\baz"), "foo\\baz");
  assert_eq!(windows::normalize("\\\\srv\\share\\a\\..\\b"), "\\\\srv\\share\\b");
  assert_eq!(windows::normalize("./a/./b"), "a\\b");
  assert_eq!(windows::normalize(""), ".");

  // join — mixed, absolute-reset, drive-reset
  const j1 = windows::join("C:\\a", "b", "c.txt");
  assert_eq!(j1.as_str(), "C:\\a\\b\\c.txt");
  const j2 = windows::join("C:\\a", "D:\\b");   // drive-rooted second wins
  assert_eq!(j2.as_str(), "D:\\b");
  const j3 = windows::join("foo", "bar/baz");
  assert_eq!(j3.as_str(), "foo\\bar\\baz");

  // split — prefix separated from body
  const parts = windows::split("C:\\Users\\admin");
  assert_eq!(parts.len(), 3);

  // resolve
  const r1 = windows::resolve("C:\\a", "b", "..\\c");
  assert_eq!(r1.as_str(), "C:\\a\\c");

  // relative — case-insensitive common prefix
  const rel = windows::relative("C:\\FOO\\bar", "c:\\foo\\baz");
  assert_eq!(rel.as_str(), "..\\baz");

  // with_extname / with_basename
  const w1 = windows::with_extname("C:\\tmp\\notes.md", ".txt");
  assert_eq!(w1.as_str(), "C:\\tmp\\notes.txt");
  const w2 = windows::with_basename("C:\\tmp\\notes.md", "readme.md");
  assert_eq!(w2.as_str(), "C:\\tmp\\readme.md");

  // eq_ignore_case
  assert!(windows::eq_ignore_case("HELLO", "hello"));
  assert!(windows::eq_ignore_case("MixED", "mixed"));
  assert!(!windows::eq_ignore_case("abc", "abd"));

  // Reserved names
  assert!(windows::is_reserved_name("CON"));
  assert!(windows::is_reserved_name("nul.txt"));
  assert!(windows::is_reserved_name("COM1"));
  assert!(windows::is_reserved_name("lpt9"));
  assert!(!windows::is_reserved_name("README"));
  assert!(!windows::is_reserved_name("COM0"));

  // to_windows / to_posix
  const tw = windows::to_windows("a/b/c");
  assert_eq!(tw.as_str(), "a\\b\\c");
  const tp = windows::to_posix("a\\b\\c");
  assert_eq!(tp.as_str(), "a/b/c");

  // has_trailing_sep
  assert!(windows::has_trailing_sep("C:\\foo\\"));
  assert!(!windows::has_trailing_sep("C:\\foo"));

  return 0;
}
