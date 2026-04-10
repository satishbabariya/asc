// RUN: %asc check %s
struct Data { value: i32 }
function make(): i32 { let d = Data { value: 42 }; return d.value; }
function double(x: i32): i32 { return x * 2; }
function main(): i32 { return double(make()); }
