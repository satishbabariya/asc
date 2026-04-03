// Test conditional move: value moved in one branch but not another.
// Expected: W001 warning (drop flag inserted).

struct Resource {
  id: i32,
}

function consume(r: own<Resource>): void { }

function either(flag: bool, a: own<Resource>, b: own<Resource>): void {
  if flag {
    consume(a);
  } else {
    consume(b);
  }
}

function main(): void {
  let r1 = Resource { id: 1 };
  let r2 = Resource { id: 2 };
  either(true, r1, r2);
}
