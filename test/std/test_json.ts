// Test: JSON parse and stringify.
function main(): i32 {
  // Parse simple values.
  const null_val = json::parse("null").unwrap();
  assert!(null_val.is_null());

  const bool_val = json::parse("true").unwrap();
  assert_eq!(bool_val.as_bool().unwrap(), true);

  const num_val = json::parse("42").unwrap();
  assert_eq!(num_val.as_i64().unwrap(), 42);

  const str_val = json::parse("\"hello\"").unwrap();
  assert_eq!(str_val.as_str().unwrap(), "hello");

  // Parse object.
  const obj = json::parse("{\"name\": \"Alice\", \"age\": 30}").unwrap();
  assert_eq!(obj["name"].as_str().unwrap(), "Alice");
  assert_eq!(obj["age"].as_i64().unwrap(), 30);

  // Stringify.
  const output = json::stringify(&num_val);
  assert_eq!(output.as_str(), "42");

  return 0;
}
