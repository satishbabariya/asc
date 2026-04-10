// RUN: %asc check %s
// Test: Sieve of Eratosthenes using arrays and loops.
// Count primes up to N.

function count_primes(n: i32): i32 {
  // Use an array to track composite numbers (1 = composite, 0 = prime)
  let sieve = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
               0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
               0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  // Mark 0 and 1 as non-prime
  sieve[0] = 1;
  sieve[1] = 1;

  let i: i32 = 2;
  while i * i < 30 {
    if sieve[i] == 0 {
      let j: i32 = i * i;
      while j < 30 {
        sieve[j] = 1;
        j = j + i;
      }
    }
    i = i + 1;
  }

  // Count primes
  let count: i32 = 0;
  for (const k of 2..n) {
    if sieve[k] == 0 {
      count = count + 1;
    }
  }
  return count;
}

function main(): i32 {
  // Primes below 30: 2,3,5,7,11,13,17,19,23,29 = 10
  return count_primes(30);
}
