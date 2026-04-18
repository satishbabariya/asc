// RUN: %asc check %s
// Test: Path manipulation (RFC-0019 §1.2–1.4).
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

  // --- normalize ------------------------------------------------------------
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
