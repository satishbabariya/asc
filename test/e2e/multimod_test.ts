// Test: multi-module import from another file.

import { add, mul } from "./import_basic/util";

function main(): i32 {
  return add(20, 22);
}
