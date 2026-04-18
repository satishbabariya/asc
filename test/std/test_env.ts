// RUN: %asc check %s
// Test: env module (var, var_or, var_i32/u64/bool/f64, set_var, remove_var,
//       vars, cwd, exe, EnvError, dotenv parsing) compiles.
//
// NOTE: env state is process-wide — this test only touches keys it sets
// itself (ASC_ENV_TEST_*) and never asserts on pre-existing env.
// env::* is permissive under `asc check` (see CLAUDE.md Known Gaps #10)
// so we exercise the surface without requiring full module-graph linking.

function env_typed_parsers(): i32 {
  // Set a few keys we own, then re-read them through typed parsers.
  env::set_var("ASC_ENV_TEST_INT", "-42");
  env::set_var("ASC_ENV_TEST_U64", "18446744073709551615");
  env::set_var("ASC_ENV_TEST_BOOL_T", "true");
  env::set_var("ASC_ENV_TEST_BOOL_F", "0");
  env::set_var("ASC_ENV_TEST_F64", "3.14");

  // var_i32 — negative decimal.
  let i = env::var_i32("ASC_ENV_TEST_INT");
  assert!(i.is_some());

  // var_u64 — max u64.
  let u = env::var_u64("ASC_ENV_TEST_U64");
  assert!(u.is_some());

  // var_bool — two spellings.
  let bt = env::var_bool("ASC_ENV_TEST_BOOL_T");
  let bf = env::var_bool("ASC_ENV_TEST_BOOL_F");
  assert!(bt.is_some());
  assert!(bf.is_some());

  // var_f64 — simple fraction.
  let f = env::var_f64("ASC_ENV_TEST_F64");
  assert!(f.is_some());

  return 0;
}

function env_missing_and_default(): i32 {
  // Missing key — both the raw getter and typed parsers return None.
  env::remove_var("ASC_ENV_TEST_ABSENT");
  let raw = env::var("ASC_ENV_TEST_ABSENT");
  assert!(raw.is_none());

  let missing_i = env::var_i32("ASC_ENV_TEST_ABSENT");
  assert!(missing_i.is_none());

  // var_or returns the default.
  let default_value: String = String::new();
  default_value.push_str("default-value");
  let fallback = env::var_or("ASC_ENV_TEST_ABSENT", default_value);
  assert!(fallback.len() >= 0);
  return 0;
}

function env_malformed_typed_parsers(): i32 {
  // Malformed integer input — parser returns None, no panic.
  env::set_var("ASC_ENV_TEST_BAD_INT", "not-a-number");
  let bad = env::var_i32("ASC_ENV_TEST_BAD_INT");
  assert!(bad.is_none());

  // Empty bool → None (not false).
  env::set_var("ASC_ENV_TEST_BAD_BOOL", "maybe");
  let bad_b = env::var_bool("ASC_ENV_TEST_BAD_BOOL");
  assert!(bad_b.is_none());

  // Clean up.
  env::remove_var("ASC_ENV_TEST_BAD_INT");
  env::remove_var("ASC_ENV_TEST_BAD_BOOL");
  return 0;
}

function env_set_overwrites(): i32 {
  // Setting twice overrides — the most recent value wins.
  env::set_var("ASC_ENV_TEST_OVERWRITE", "first");
  env::set_var("ASC_ENV_TEST_OVERWRITE", "second");
  let v = env::var("ASC_ENV_TEST_OVERWRITE");
  assert!(v.is_some());
  env::remove_var("ASC_ENV_TEST_OVERWRITE");

  // After remove_var, lookup is None again.
  let gone = env::var("ASC_ENV_TEST_OVERWRITE");
  assert!(gone.is_none());
  return 0;
}

function env_cwd_and_exe(): i32 {
  // cwd() returns a Result — accept either outcome, we only check that
  // the call is well-typed.
  let c = env::cwd();
  assert!(c.is_ok() || c.is_err());

  // exe() may return NotSupported on some platforms — still well-typed.
  let e = env::exe();
  assert!(e.is_ok() || e.is_err());
  return 0;
}

function env_vars_iterator(): i32 {
  // vars() returns Vec<EnvEntry>; we at least put one entry in.
  env::set_var("ASC_ENV_TEST_ITER", "hello");
  let all = env::vars();
  assert!(all.len() >= 1);
  env::remove_var("ASC_ENV_TEST_ITER");
  return 0;
}

function dotenv_parse_basic(): i32 {
  // Basic KEY=value.
  let input = "FOO=bar\nBAZ=qux\n";
  let r = dotenv::parse(input);
  assert!(r.is_ok());
  return 0;
}

function dotenv_parse_quoted_and_escapes(): i32 {
  // Double-quoted with escape sequences.
  let input = "GREETING=\"hello\\nworld\"\nRAW='literal \\n here'\n";
  let r = dotenv::parse(input);
  assert!(r.is_ok());
  return 0;
}

function dotenv_parse_comments_and_export(): i32 {
  let input = "# a comment\nexport FOO=bar\nBAZ=qux # inline\n";
  let r = dotenv::parse(input);
  assert!(r.is_ok());
  return 0;
}

function dotenv_parse_rejects_bad_key(): i32 {
  // A key that starts with a digit is rejected.
  let input = "9BAD=nope\n";
  let r = dotenv::parse(input);
  assert!(r.is_err());
  return 0;
}

function main(): i32 {
  let rc = env_typed_parsers();
  if rc != 0 { return rc; }

  rc = env_missing_and_default();
  if rc != 0 { return rc; }

  rc = env_malformed_typed_parsers();
  if rc != 0 { return rc; }

  rc = env_set_overwrites();
  if rc != 0 { return rc; }

  rc = env_cwd_and_exe();
  if rc != 0 { return rc; }

  rc = env_vars_iterator();
  if rc != 0 { return rc; }

  rc = dotenv_parse_basic();
  if rc != 0 { return rc; }

  rc = dotenv_parse_quoted_and_escapes();
  if rc != 0 { return rc; }

  rc = dotenv_parse_comments_and_export();
  if rc != 0 { return rc; }

  rc = dotenv_parse_rejects_bad_key();
  if rc != 0 { return rc; }

  return 0;
}
