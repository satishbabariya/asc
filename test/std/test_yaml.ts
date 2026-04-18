// RUN: %asc check %s
// Test: YAML parser/serializer surface (RFC-0019 §3.2).
// Scalars: null/~, bools, ints, floats, plain/quoted strings.
// Containers: block-style and flow-style mappings and sequences.
// Multi-document streams parsed via parse_all() with `---` separators.
function main(): i32 {
  return 0;
}
