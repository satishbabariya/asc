// RUN: %asc check %s

function main(): i32 {
  let f = File { fd: 1 };
  return f.fd;
}
