// RUN: %asc check %s > %t.out 2>&1; grep -q "no variant" %t.out
// RUN: %asc check %s > %t.out 2>&1; grep -q "Option" %t.out
// Test: referencing a nonexistent enum variant is rejected by Sema.
// Previously checkPathExpr returned the enum type as soon as the enum name
// matched, without validating the variant segment — so Option::Nope typed
// fine and silently confused downstream matching.

function main(): i32 {
  let x = Option::Nope;
  return 0;
}
