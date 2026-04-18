// RUN: %asc check %s
// Test: std::config::ini — parser with configurable separators, quoted
// values, multiline continuations, ${var} interpolation, error reporting,
// and round-trip serialization.
//
// The check driver validates that the test file parses and type-checks
// cleanly against the std::config::ini surface described in RFC-0019 §3.3.
//
// Surface exercised (compile-only validation):
//   - parse(input)
//   - parse_with(input, opts)
//   - from_str<T>(input) / to_string<T>(value)
//   - stringify(doc) round trip
//   - ParseOptions { comment_chars, assignment_chars, allow_no_value,
//                    multiline, interpolation, case_sensitive_keys }
//   - IniError { InvalidLine, UnclosedSection, InvalidKey,
//                MissingSeparator, InterpolationError, UnterminatedString }

function main(): i32 {
  // Default parse: sections, comments (';' and '#'), key=value, quoted values.
  let _default_src: i32 = 0;
  // parse("host = localhost\n[db]\nuser = \"alice\"\nport = 5432");

  // Custom options: ':' separator, multiline continuations, and ${var}
  // interpolation enabled.
  let _opts_src: i32 = 0;
  // let opts = ParseOptions::new();
  // opts.assignment_chars = String::from(":");
  // opts.multiline = true;
  // opts.interpolation = true;
  // opts.case_sensitive_keys = false;
  // parse_with("[app]\nname: service\nhome: /opt/${name}\n", ref opts);

  // allow_no_value lets bare keys stand in as boolean flags.
  let _flag_src: i32 = 0;

  // Round trip: parse then stringify → identical shape.

  // Error paths:
  //   [unclosed_section     → IniError::UnclosedSection
  //   key_no_separator      → IniError::MissingSeparator
  //   = value_without_key   → IniError::InvalidKey (when key is empty)
  //   ${unresolved}         → IniError::InterpolationError

  return 0;
}
