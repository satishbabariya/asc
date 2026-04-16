// std/core/option.ts — Option<T> (RFC-0013)

/// Optional value: either Some(T) or None.
enum Option<T> {
  Some(own<T>),
  None,
}

impl<T> Option<T> {
  fn is_some(ref<Self>): bool {
    match self {
      Option::Some(_) => true,
      Option::None => false,
    }
  }

  fn is_none(ref<Self>): bool {
    return !self.is_some();
  }

  /// Extract the value. Panics if None.
  fn unwrap(own<Self>): own<T> {
    match self {
      Option::Some(v) => v,
      Option::None => { panic!("called unwrap() on a None value"); },
    }
  }

  /// Extract the value or return a default.
  fn unwrap_or(own<Self>, default: own<T>): own<T> {
    match self {
      Option::Some(v) => v,
      Option::None => default,
    }
  }

  /// Extract the value or compute a default.
  fn unwrap_or_else(own<Self>, f: () -> own<T>): own<T> {
    match self {
      Option::Some(v) => v,
      Option::None => f(),
    }
  }

  /// Apply a function to the contained value.
  fn map<U>(own<Self>, f: (own<T>) -> own<U>): Option<U> {
    match self {
      Option::Some(v) => Option::Some(f(v)),
      Option::None => Option::None,
    }
  }

  /// Apply a function that returns Option.
  fn and_then<U>(own<Self>, f: (own<T>) -> Option<U>): Option<U> {
    match self {
      Option::Some(v) => f(v),
      Option::None => Option::None,
    }
  }

  /// Return self if Some, otherwise call f.
  fn or_else(own<Self>, f: () -> Option<T>): Option<T> {
    match self {
      Option::Some(v) => Option::Some(v),
      Option::None => f(),
    }
  }

  /// Convert to Result, mapping None to the given error.
  fn ok_or<E>(own<Self>, err: own<E>): Result<own<T>, own<E>> {
    match self {
      Option::Some(v) => Result::Ok(v),
      Option::None => Result::Err(err),
    }
  }

  /// Filter the Option with a predicate.
  fn filter(own<Self>, predicate: (ref<T>) -> bool): Option<T> {
    match self {
      Option::Some(v) => {
        if predicate(&v) { Option::Some(v) }
        else { Option::None }
      },
      Option::None => Option::None,
    }
  }

  /// Zip two Options into a tuple Option.
  fn zip<U>(own<Self>, other: Option<U>): Option<(T, U)> {
    match self {
      Option::Some(a) => {
        match other {
          Option::Some(b) => Option::Some((a, b)),
          Option::None => Option::None,
        }
      },
      Option::None => Option::None,
    }
  }

  /// Take the value out, leaving None in its place.
  fn take(refmut<Self>): Option<T> {
    let result = Option::None;
    match self {
      Option::Some(_) => {
        result = *self;
        *self = Option::None;
      },
      Option::None => {},
    }
    return result;
  }

  /// Replace the value, returning the old one.
  fn replace(refmut<Self>, value: own<T>): Option<T> {
    let old = self.take();
    *self = Option::Some(value);
    return old;
  }

  /// Flatten nested Option.
  fn flatten(own<Self>): Option<T>
    where T: Option<U>
  {
    match self {
      Option::Some(inner) => inner,
      Option::None => Option::None,
    }
  }
}

/// Shorthand constructors.
function Some<T>(value: own<T>): Option<T> {
  return Option::Some(value);
}

const None: Option<never> = Option::None;
