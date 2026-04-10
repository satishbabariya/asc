// RUN: %asc check %s
// Test: multi-file import.

import { add, mul } from "./util";

function main(): i32 {
  return add(20, 22);
}
