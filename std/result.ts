// std/result.ts — Result<T,E> (RFC-0013)

/// A value that is either success (Ok) or failure (Err).
enum Result<T, E> {
  Ok(own<T>),
  Err(own<E>),
}

impl<T, E> Result<T, E> {
  fn is_ok(ref<Self>): bool {
    match self { Result::Ok(_) => true, Result::Err(_) => false }
  }
  fn is_err(ref<Self>): bool { return !self.is_ok(); }

  fn unwrap(own<Self>): own<T> {
    match self {
      Result::Ok(v) => v,
      Result::Err(_) => { panic!("called unwrap() on an Err value"); },
    }
  }
  fn unwrap_or(own<Self>, default: own<T>): own<T> {
    match self { Result::Ok(v) => v, Result::Err(_) => default }
  }
  fn unwrap_or_else(own<Self>, f: (own<E>) -> own<T>): own<T> {
    match self { Result::Ok(v) => v, Result::Err(e) => f(e) }
  }
  fn unwrap_err(own<Self>): own<E> {
    match self {
      Result::Ok(_) => { panic!("called unwrap_err() on an Ok value"); },
      Result::Err(e) => e,
    }
  }

  /// Unwrap or panic with the given message.
  fn expect(own<Self>, msg: ref<String>): own<T> {
    match self {
      Result::Ok(v) => v,
      Result::Err(_) => { panic!(msg); },
    }
  }

  /// Unwrap error or panic with the given message.
  fn expect_err(own<Self>, msg: ref<String>): own<E> {
    match self {
      Result::Ok(_) => { panic!(msg); },
      Result::Err(e) => e,
    }
  }

  fn map<U>(own<Self>, f: (own<T>) -> own<U>): Result<U, E> {
    match self {
      Result::Ok(v) => Result::Ok(f(v)),
      Result::Err(e) => Result::Err(e),
    }
  }

  /// Map the value or return a default if Err.
  fn map_or<U>(own<Self>, default: own<U>, f: (own<T>) -> own<U>): own<U> {
    match self {
      Result::Ok(v) => f(v),
      Result::Err(_) => default,
    }
  }

  /// Map the value or compute the default from the error.
  fn map_or_else<U>(own<Self>, default: (own<E>) -> own<U>, f: (own<T>) -> own<U>): own<U> {
    match self {
      Result::Ok(v) => f(v),
      Result::Err(e) => default(e),
    }
  }

  /// True if Ok and the predicate holds on the inner value.
  fn is_ok_and(ref<Self>, predicate: (ref<T>) -> bool): bool {
    match self {
      Result::Ok(v) => predicate(&v),
      Result::Err(_) => false,
    }
  }

  /// True if Err and the predicate holds on the error value.
  fn is_err_and(ref<Self>, predicate: (ref<E>) -> bool): bool {
    match self {
      Result::Ok(_) => false,
      Result::Err(e) => predicate(&e),
    }
  }

  /// Observe the Ok value without consuming the Result.
  fn inspect(own<Self>, observer: (ref<T>) -> void): Result<T, E> {
    match self {
      Result::Ok(v) => { observer(&v); return Result::Ok(v); },
      Result::Err(e) => { return Result::Err(e); },
    }
  }

  /// Observe the Err value without consuming the Result.
  fn inspect_err(own<Self>, observer: (ref<E>) -> void): Result<T, E> {
    match self {
      Result::Ok(v) => { return Result::Ok(v); },
      Result::Err(e) => { observer(&e); return Result::Err(e); },
    }
  }
  fn map_err<F>(own<Self>, f: (own<E>) -> own<F>): Result<T, F> {
    match self {
      Result::Ok(v) => Result::Ok(v),
      Result::Err(e) => Result::Err(f(e)),
    }
  }
  fn and_then<U>(own<Self>, f: (own<T>) -> Result<U, E>): Result<U, E> {
    match self {
      Result::Ok(v) => f(v),
      Result::Err(e) => Result::Err(e),
    }
  }
  fn or_else<F>(own<Self>, f: (own<E>) -> Result<T, F>): Result<T, F> {
    match self {
      Result::Ok(v) => Result::Ok(v),
      Result::Err(e) => f(e),
    }
  }

  fn ok(own<Self>): Option<T> {
    match self { Result::Ok(v) => Option::Some(v), Result::Err(_) => Option::None }
  }
  fn err(own<Self>): Option<E> {
    match self { Result::Ok(_) => Option::None, Result::Err(e) => Option::Some(e) }
  }
}

function Ok<T, E>(value: own<T>): Result<T, E> { return Result::Ok(value); }
function Err<T, E>(error: own<E>): Result<T, E> { return Result::Err(error); }
