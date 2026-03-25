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

  fn map<U>(own<Self>, f: (own<T>) -> own<U>): Result<U, E> {
    match self {
      Result::Ok(v) => Result::Ok(f(v)),
      Result::Err(e) => Result::Err(e),
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
