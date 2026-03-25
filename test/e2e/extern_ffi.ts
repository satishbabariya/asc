// Test: external function declaration.

@extern("env", "log")
function extern_log(x: i32): void;

function main(): i32 {
  return 42;
}
