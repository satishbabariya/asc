// Test: asc fmt normalizes indentation.
// Run: asc fmt this_file.ts
// Expected: 2-space indent, newlines after { and ;

function main(): i32 {
const x: i32 = 42;
return x;
}
