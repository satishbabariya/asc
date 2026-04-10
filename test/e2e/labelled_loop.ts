// RUN: %asc check %s

function find_pair(): i32 {
  let result: i32 = 0;
  outer: for (let i of 0..5) {
    for (let j of 0..5) {
      if (i + j == 7) {
        result = i;
        break outer;
      }
    }
  }
  return result;
}

function main(): i32 {
  return find_pair();
}
