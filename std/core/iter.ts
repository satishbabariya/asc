// std/core/iter.ts — Iterator traits and adapters (RFC-0011)

/// The core iterator trait. All iterators implement this.
trait Iterator {
  type Item;

  /// Advance the iterator and return the next value.
  fn next(refmut<Self>): Option<own<Item>>;

  // --- Provided methods (implemented in terms of next) ---

  fn count(own<Self>): usize {
    let n: usize = 0;
    while self.next().is_some() { n = n + 1; }
    return n;
  }

  fn last(own<Self>): Option<own<Item>> {
    let result: Option<own<Item>> = Option::None;
    loop {
      match self.next() {
        Option::Some(v) => { result = Option::Some(v); },
        Option::None => { break; },
      }
    }
    return result;
  }

  fn nth(refmut<Self>, n: usize): Option<own<Item>> {
    let i: usize = 0;
    while i < n {
      if self.next().is_none() { return Option::None; }
      i = i + 1;
    }
    return self.next();
  }

  fn any(own<Self>, predicate: (ref<Item>) -> bool): bool {
    loop {
      match self.next() {
        Option::Some(v) => { if predicate(&v) { return true; } },
        Option::None => { return false; },
      }
    }
  }

  fn all(own<Self>, predicate: (ref<Item>) -> bool): bool {
    loop {
      match self.next() {
        Option::Some(v) => { if !predicate(&v) { return false; } },
        Option::None => { return true; },
      }
    }
  }

  fn find(own<Self>, predicate: (ref<Item>) -> bool): Option<own<Item>> {
    loop {
      match self.next() {
        Option::Some(v) => { if predicate(&v) { return Option::Some(v); } },
        Option::None => { return Option::None; },
      }
    }
  }

  fn position(own<Self>, predicate: (ref<Item>) -> bool): Option<usize> {
    let i: usize = 0;
    loop {
      match self.next() {
        Option::Some(v) => {
          if predicate(&v) { return Option::Some(i); }
          i = i + 1;
        },
        Option::None => { return Option::None; },
      }
    }
  }

  fn fold<B>(own<Self>, init: own<B>, f: (own<B>, own<Item>) -> own<B>): own<B> {
    let acc = init;
    loop {
      match self.next() {
        Option::Some(v) => { acc = f(acc, v); },
        Option::None => { return acc; },
      }
    }
  }

  fn for_each(own<Self>, f: (own<Item>) -> void): void {
    loop {
      match self.next() {
        Option::Some(v) => { f(v); },
        Option::None => { return; },
      }
    }
  }

  fn sum<S: Add + Default>(own<Self>): own<S> {
    return self.fold(S::default(), (acc, x) => acc + x);
  }

  fn product<P: Mul + Default>(own<Self>): own<P> {
    return self.fold(P::default(), (acc, x) => acc * x);
  }
}

/// Conversion into an Iterator.
trait IntoIterator {
  type Item;
  type IntoIter: Iterator;
  fn into_iter(own<Self>): own<IntoIter>;
}

/// Build a collection from an Iterator.
trait FromIterator<T> {
  fn from_iter<I: Iterator>(iter: own<I>): own<Self>;
}

/// Iterator that yields (index, value) pairs.
struct Enumerate<I: Iterator> {
  iter: own<I>,
  count: usize,
}

impl<I: Iterator> Iterator for Enumerate<I> {
  type Item = (usize, I::Item);
  fn next(refmut<Self>): Option<own<(usize, I::Item)>> {
    match self.iter.next() {
      Option::Some(v) => {
        const idx = self.count;
        self.count = self.count + 1;
        return Option::Some((idx, v));
      },
      Option::None => { return Option::None; },
    }
  }
}

/// Iterator that applies a function to each element.
struct Map<I: Iterator, B> {
  iter: own<I>,
  f: (own<I::Item>) -> own<B>,
}

impl<I: Iterator, B> Iterator for Map<I, B> {
  type Item = B;
  fn next(refmut<Self>): Option<own<B>> {
    match self.iter.next() {
      Option::Some(v) => { return Option::Some((self.f)(v)); },
      Option::None => { return Option::None; },
    }
  }
}

/// Iterator that filters elements by a predicate.
struct Filter<I: Iterator> {
  iter: own<I>,
  predicate: (ref<I::Item>) -> bool,
}

impl<I: Iterator> Iterator for Filter<I> {
  type Item = I::Item;
  fn next(refmut<Self>): Option<own<I::Item>> {
    loop {
      match self.iter.next() {
        Option::Some(v) => {
          if (self.predicate)(&v) { return Option::Some(v); }
        },
        Option::None => { return Option::None; },
      }
    }
  }
}

/// Iterator that takes at most n elements.
struct Take<I: Iterator> {
  iter: own<I>,
  remaining: usize,
}

impl<I: Iterator> Iterator for Take<I> {
  type Item = I::Item;
  fn next(refmut<Self>): Option<own<I::Item>> {
    if self.remaining == 0 { return Option::None; }
    self.remaining = self.remaining - 1;
    return self.iter.next();
  }
}

/// Iterator that skips n elements.
struct Skip<I: Iterator> {
  iter: own<I>,
  remaining: usize,
}

impl<I: Iterator> Iterator for Skip<I> {
  type Item = I::Item;
  fn next(refmut<Self>): Option<own<I::Item>> {
    while self.remaining > 0 {
      self.remaining = self.remaining - 1;
      self.iter.next();
    }
    return self.iter.next();
  }
}

/// Iterator that chains two iterators.
struct Chain<A: Iterator, B: Iterator> {
  a: own<A>,
  b: own<B>,
  a_done: bool,
}

impl<A: Iterator, B: Iterator> Iterator for Chain<A, B>
  where A::Item == B::Item
{
  type Item = A::Item;
  fn next(refmut<Self>): Option<own<A::Item>> {
    if !self.a_done {
      match self.a.next() {
        Option::Some(v) => { return Option::Some(v); },
        Option::None => { self.a_done = true; },
      }
    }
    return self.b.next();
  }
}

/// Iterator that zips two iterators into pairs.
struct Zip<A: Iterator, B: Iterator> {
  a: own<A>,
  b: own<B>,
}

impl<A: Iterator, B: Iterator> Iterator for Zip<A, B> {
  type Item = (A::Item, B::Item);
  fn next(refmut<Self>): Option<own<(A::Item, B::Item)>> {
    match self.a.next() {
      Option::Some(va) => {
        match self.b.next() {
          Option::Some(vb) => { return Option::Some((va, vb)); },
          Option::None => { return Option::None; },
        }
      },
      Option::None => { return Option::None; },
    }
  }
}

/// Range iterator for i32.
impl Iterator for Range<i32> {
  type Item = i32;
  fn next(refmut<Self>): Option<i32> {
    if self.start < self.end {
      const val = self.start;
      self.start = self.start + 1;
      return Option::Some(val);
    }
    return Option::None;
  }
}
