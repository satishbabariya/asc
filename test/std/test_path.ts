// RUN: %asc check %s
// Test: Path manipulation (POSIX + Windows variants, RFC-0019 §1.2–1.4).
function main(): i32 {
  // --- Inspection -----------------------------------------------------------
  assert_eq!(path::basename("/home/user/file.ts"), "file.ts");
  assert_eq!(path::dirname("/home/user/file.ts"), "/home/user");
  assert_eq!(path::extname("/home/user/file.ts"), ".ts");
  assert!(path::is_absolute("/usr/bin"));
  assert!(!path::is_absolute("relative/path"));
  assert!(path::is_relative("relative/path"));
  assert!(path::has_trailing_sep("/foo/"));
  assert!(!path::has_trailing_sep("/foo"));

  // --- Construction: join ---------------------------------------------------
  const joined = path::join("/home", "user/file.ts");
  assert_eq!(joined.as_str(), "/home/user/file.ts");

  // --- Windows --------------------------------------------------------------

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

  // --- POSIX: normalize -----------------------------------------------------
  assert_eq!(path::normalize("/foo/./bar").as_str(), "/foo/bar");
  assert_eq!(path::normalize("/foo/../bar").as_str(), "/bar");
  assert_eq!(path::normalize("/foo//bar///baz").as_str(), "/foo/bar/baz");
  assert_eq!(path::normalize("").as_str(), ".");
  assert_eq!(path::normalize("/").as_str(), "/");
  assert_eq!(path::normalize("./a/./b").as_str(), "a/b");
  // `..` beyond the root stays anchored at root.
  assert_eq!(path::normalize("/../..").as_str(), "/");
  // `..` in a relative path that underflows is preserved.
  assert_eq!(path::normalize("../a").as_str(), "../a");

  // --- resolve --------------------------------------------------------------
  assert_eq!(path::resolve("/foo", "bar").as_str(), "/foo/bar");
  assert_eq!(path::resolve("/foo", "/bar").as_str(), "/bar"); // absolute resets
  assert_eq!(path::resolve("/foo", "bar", "../baz").as_str(), "/foo/baz");
  // No absolute segment: anchors at "/".
  assert_eq!(path::resolve("foo", "bar").as_str(), "/foo/bar");

  // --- relative -------------------------------------------------------------
  assert_eq!(path::relative("/foo/bar", "/foo/baz").as_str(), "../baz");
  assert_eq!(path::relative("/a/b/c", "/a/b/c").as_str(), ".");
  assert_eq!(path::relative("/a/b", "/a/b/c/d").as_str(), "c/d");
  assert_eq!(path::relative("/a/b/c/d", "/a/b").as_str(), "../..");

  // --- with_extname (accepts ext with or without leading dot) ---------------
  assert_eq!(path::with_extname("foo.ts", "js").as_str(), "foo.js");
  assert_eq!(path::with_extname("foo.ts", ".js").as_str(), "foo.js");
  assert_eq!(path::with_extname("foo", "js").as_str(), "foo.js");
  assert_eq!(path::with_extname("/a/b/c.txt", "md").as_str(), "/a/b/c.md");
  assert_eq!(path::with_extname("foo.ts", "").as_str(), "foo");
  // Round-trip: replacing with the same extension is a no-op.
  assert_eq!(
    path::with_extname("/x/y/z.ts", ".ts").as_str(),
    "/x/y/z.ts",
  );

  // --- with_basename --------------------------------------------------------
  assert_eq!(path::with_basename("/foo/bar.ts", "baz.ts").as_str(), "/foo/baz.ts");
  assert_eq!(path::with_basename("/only", "other").as_str(), "/other");
  assert_eq!(path::with_basename("only.ts", "other.ts").as_str(), "./other.ts");

  // --- stem -----------------------------------------------------------------
  assert_eq!(path::stem("/a/foo.tar.gz").as_str(), "foo.tar");
  assert_eq!(path::stem(".hidden").as_str(), ".hidden");

  // --- glob_match -----------------------------------------------------------
  // Literal and `?`.
  assert!(path::glob_match("foo.ts", "foo.ts"));
  assert!(path::glob_match("f?o.ts", "foo.ts"));
  assert!(!path::glob_match("f?o.ts", "fo.ts"));
  // `*` does not cross '/'.
  assert!(path::glob_match("*.ts", "foo.ts"));
  assert!(!path::glob_match("*.ts", "a/foo.ts"));
  // `**` crosses '/'.
  assert!(path::glob_match("**/*.ts", "a/b/c.ts"));
  assert!(path::glob_match("**/*.ts", "c.ts"));
  // Character class.
  assert!(path::glob_match("f[oa]o", "foo"));
  assert!(path::glob_match("f[oa]o", "fao"));
  assert!(!path::glob_match("f[oa]o", "fxo"));
  assert!(path::glob_match("f[!x]o", "foo"));
  assert!(!path::glob_match("f[!x]o", "fxo"));
  // Range.
  assert!(path::glob_match("v[0-9].ts", "v3.ts"));
  assert!(!path::glob_match("v[0-9].ts", "va.ts"));
  // Empty pattern matches only empty.
  assert!(path::glob_match("", ""));
  assert!(!path::glob_match("", "a"));
  // `**` alone matches anything.
  assert!(path::glob_match("**", "a/b/c"));

  // --- Round-trip: dirname + basename reconstruct the path -----------------
  const joined_back = path::join(path::dirname("/a/b/c.ts").as_str(),
                                 path::basename("/a/b/c.ts").as_str());
  assert_eq!(joined_back.as_str(), "/a/b/c.ts");

  // --- Edge: empty and root -------------------------------------------------
  assert_eq!(path::basename("").as_str(), ".");
  assert_eq!(path::dirname("").as_str(), ".");
  assert_eq!(path::basename("/").as_str(), "/");
  assert_eq!(path::dirname("/").as_str(), "/");

  return 0;
}
