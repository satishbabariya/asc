// std/mem/cell.ts — Cell<T> and RefCell<T>: interior mutability (RFC-0012)

/// Interior mutability for Copy types. No runtime borrow tracking needed.
/// Send if T: Send, NOT Sync.
struct Cell<T: Copy> {
  value: T,
}

impl<T: Copy> Cell<T> {
  /// Creates a new Cell containing the given value.
  fn new(value: T): Cell<T> {
    return Cell { value: value };
  }

  /// Returns a copy of the contained value.
  fn get(ref<Self>): T {
    return self.value;
  }

  /// Replaces the contained value.
  fn set(ref<Self>, value: T): void {
    // Interior mutability: writes through a shared reference.
    // The compiler inserts an unsafe interior-mut store here.
    unsafe { (self as *const Cell<T> as *mut Cell<T>).value = value; }
  }

  /// Applies `f` to the contained value, stores the result, and returns it.
  fn update(ref<Self>, f: (T) -> T): T {
    const new_value = f(self.get());
    self.set(new_value);
    return new_value;
  }

  /// Replaces the contained value and returns the old one.
  fn replace(ref<Self>, value: T): T {
    const old = self.get();
    self.set(value);
    return old;
  }
}

// ---------- RefCell<T> ----------

/// Runtime borrow state encoding.
/// 0 = not borrowed, positive = N shared borrows, -1 = mutably borrowed.
const REFCELL_UNBORROWED: isize = 0;
const REFCELL_MUT_BORROWED: isize = -1;

/// Interior mutability with runtime borrow checking.
/// Send if T: Send, NOT Sync.
struct RefCell<T> {
  borrow_state: isize,
  value: T,
}

impl<T> RefCell<T> {
  /// Creates a new RefCell wrapping the given value.
  fn new(value: own<T>): own<RefCell<T>> {
    return RefCell { borrow_state: REFCELL_UNBORROWED, value: value };
  }

  /// Immutably borrows the wrapped value. Panics if a mutable borrow is active.
  fn borrow(ref<Self>): own<Ref<T>> {
    if self.borrow_state == REFCELL_MUT_BORROWED {
      panic!("RefCell: already mutably borrowed");
    }
    // Increment shared borrow count via interior mutability.
    const state_ptr = unsafe { &self.borrow_state as *const isize as *mut isize };
    unsafe { *state_ptr = *state_ptr + 1; }
    return Ref { cell: self, ptr: &self.value };
  }

  /// Mutably borrows the wrapped value. Panics if any borrow is active.
  fn borrow_mut(ref<Self>): own<RefMut<T>> {
    if self.borrow_state != REFCELL_UNBORROWED {
      panic!("RefCell: already borrowed");
    }
    const state_ptr = unsafe { &self.borrow_state as *const isize as *mut isize };
    unsafe { *state_ptr = REFCELL_MUT_BORROWED; }
    const value_ptr = unsafe { &self.value as *const T as *mut T };
    return RefMut { cell: self, ptr: value_ptr };
  }

  /// Tries to immutably borrow. Returns Err if a mutable borrow is active.
  fn try_borrow(ref<Self>): Result<own<Ref<T>>, BorrowError> {
    if self.borrow_state == REFCELL_MUT_BORROWED {
      return Result::Err(BorrowError {});
    }
    const state_ptr = unsafe { &self.borrow_state as *const isize as *mut isize };
    unsafe { *state_ptr = *state_ptr + 1; }
    return Result::Ok(Ref { cell: self, ptr: &self.value });
  }

  /// Tries to mutably borrow. Returns Err if any borrow is active.
  fn try_borrow_mut(ref<Self>): Result<own<RefMut<T>>, BorrowMutError> {
    if self.borrow_state != REFCELL_UNBORROWED {
      return Result::Err(BorrowMutError {});
    }
    const state_ptr = unsafe { &self.borrow_state as *const isize as *mut isize };
    unsafe { *state_ptr = REFCELL_MUT_BORROWED; }
    const value_ptr = unsafe { &self.value as *const T as *mut T };
    return Result::Ok(RefMut { cell: self, ptr: value_ptr });
  }

  /// Consumes the RefCell, returning the wrapped value.
  fn into_inner(own<Self>): own<T> {
    return self.value;
  }
}

/// RAII guard for a shared borrow from RefCell.
struct Ref<T> {
  cell: ref<RefCell<T>>,
  ptr: ref<T>,
}

impl<T> Ref<T> {
  fn deref(ref<Self>): ref<T> {
    return self.ptr;
  }
}

impl<T> Drop for Ref<T> {
  fn drop(refmut<Self>): void {
    // Decrement the shared borrow count.
    const state_ptr = unsafe { &self.cell.borrow_state as *const isize as *mut isize };
    unsafe { *state_ptr = *state_ptr - 1; }
  }
}

/// RAII guard for a mutable borrow from RefCell.
struct RefMut<T> {
  cell: ref<RefCell<T>>,
  ptr: *mut T,
}

impl<T> RefMut<T> {
  fn deref(ref<Self>): ref<T> {
    return unsafe { &*self.ptr };
  }

  fn deref_mut(refmut<Self>): refmut<T> {
    return unsafe { &mut *self.ptr };
  }
}

impl<T> Drop for RefMut<T> {
  fn drop(refmut<Self>): void {
    // Release the mutable borrow.
    const state_ptr = unsafe { &self.cell.borrow_state as *const isize as *mut isize };
    unsafe { *state_ptr = REFCELL_UNBORROWED; }
  }
}

struct BorrowError {}
struct BorrowMutError {}

impl Display for BorrowError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str("already mutably borrowed");
  }
}

impl Display for BorrowMutError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str("already borrowed");
  }
}
