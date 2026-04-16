// std/sync/atomic.ts — Atomic types and Ordering (RFC-0014)

/// Memory ordering for atomic operations.
/// Maps to Wasm atomic instruction semantics.
enum Ordering {
  /// No ordering constraints.
  Relaxed,
  /// Loads with Acquire see all writes from Release stores.
  Acquire,
  /// Stores with Release are visible to Acquire loads.
  Release,
  /// Combined Acquire + Release.
  AcqRel,
  /// Sequential consistency — total ordering across all threads.
  SeqCst,
}

// ---------- AtomicI32 ----------

/// Atomic 32-bit signed integer. NOT @copy.
struct AtomicI32 {
  value: i32,
}

impl AtomicI32 {
  fn new(v: i32): AtomicI32 {
    return AtomicI32 { value: v };
  }

  fn load(ref<Self>, order: Ordering): i32 {
    @extern("i32.atomic.load")
    return atomic_load_i32(&self.value, order);
  }

  fn store(ref<Self>, v: i32, order: Ordering): void {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    @extern("i32.atomic.store")
    atomic_store_i32(ptr, v, order);
  }

  fn swap(ref<Self>, v: i32, order: Ordering): i32 {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    @extern("i32.atomic.rmw.xchg")
    return atomic_swap_i32(ptr, v, order);
  }

  fn fetch_add(ref<Self>, v: i32, order: Ordering): i32 {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    @extern("i32.atomic.rmw.add")
    return atomic_fetch_add_i32(ptr, v, order);
  }

  fn fetch_sub(ref<Self>, v: i32, order: Ordering): i32 {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    @extern("i32.atomic.rmw.sub")
    return atomic_fetch_sub_i32(ptr, v, order);
  }

  fn fetch_and(ref<Self>, v: i32, order: Ordering): i32 {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    @extern("i32.atomic.rmw.and")
    return atomic_fetch_and_i32(ptr, v, order);
  }

  fn fetch_or(ref<Self>, v: i32, order: Ordering): i32 {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    @extern("i32.atomic.rmw.or")
    return atomic_fetch_or_i32(ptr, v, order);
  }

  fn fetch_xor(ref<Self>, v: i32, order: Ordering): i32 {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    @extern("i32.atomic.rmw.xor")
    return atomic_fetch_xor_i32(ptr, v, order);
  }

  fn fetch_max(ref<Self>, v: i32, order: Ordering): i32 {
    // CAS loop for max.
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    loop {
      const current = self.load(Ordering::Relaxed);
      if v <= current { return current; }
      const result = self.compare_exchange(current, v, order, Ordering::Relaxed);
      match result {
        Result::Ok(old) => { return old; },
        Result::Err(_) => { /* retry */ },
      }
    }
  }

  fn fetch_min(ref<Self>, v: i32, order: Ordering): i32 {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    loop {
      const current = self.load(Ordering::Relaxed);
      if v >= current { return current; }
      const result = self.compare_exchange(current, v, order, Ordering::Relaxed);
      match result {
        Result::Ok(old) => { return old; },
        Result::Err(_) => { /* retry */ },
      }
    }
  }

  fn compare_exchange(ref<Self>, expected: i32, new_val: i32,
    success: Ordering, failure: Ordering): Result<i32, i32> {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    @extern("i32.atomic.rmw.cmpxchg")
    return atomic_compare_exchange_i32(ptr, expected, new_val, success, failure);
  }

  fn compare_exchange_weak(ref<Self>, expected: i32, new_val: i32,
    success: Ordering, failure: Ordering): Result<i32, i32> {
    // On Wasm, cmpxchg is always strong. Forward to compare_exchange.
    return self.compare_exchange(expected, new_val, success, failure);
  }
}

// ---------- AtomicU32 ----------

/// Atomic 32-bit unsigned integer. NOT @copy.
struct AtomicU32 {
  value: u32,
}

impl AtomicU32 {
  fn new(v: u32): AtomicU32 { return AtomicU32 { value: v }; }

  fn load(ref<Self>, order: Ordering): u32 {
    @extern("i32.atomic.load")
    return atomic_load_u32(&self.value, order);
  }

  fn store(ref<Self>, v: u32, order: Ordering): void {
    const ptr = unsafe { &self.value as *const u32 as *mut u32 };
    @extern("i32.atomic.store")
    atomic_store_u32(ptr, v, order);
  }

  fn swap(ref<Self>, v: u32, order: Ordering): u32 {
    const ptr = unsafe { &self.value as *const u32 as *mut u32 };
    @extern("i32.atomic.rmw.xchg")
    return atomic_swap_u32(ptr, v, order);
  }

  fn fetch_add(ref<Self>, v: u32, order: Ordering): u32 {
    const ptr = unsafe { &self.value as *const u32 as *mut u32 };
    @extern("i32.atomic.rmw.add")
    return atomic_fetch_add_u32(ptr, v, order);
  }

  fn fetch_sub(ref<Self>, v: u32, order: Ordering): u32 {
    const ptr = unsafe { &self.value as *const u32 as *mut u32 };
    @extern("i32.atomic.rmw.sub")
    return atomic_fetch_sub_u32(ptr, v, order);
  }

  fn fetch_and(ref<Self>, v: u32, order: Ordering): u32 {
    const ptr = unsafe { &self.value as *const u32 as *mut u32 };
    @extern("i32.atomic.rmw.and")
    return atomic_fetch_and_u32(ptr, v, order);
  }

  fn fetch_or(ref<Self>, v: u32, order: Ordering): u32 {
    const ptr = unsafe { &self.value as *const u32 as *mut u32 };
    @extern("i32.atomic.rmw.or")
    return atomic_fetch_or_u32(ptr, v, order);
  }

  fn fetch_xor(ref<Self>, v: u32, order: Ordering): u32 {
    const ptr = unsafe { &self.value as *const u32 as *mut u32 };
    @extern("i32.atomic.rmw.xor")
    return atomic_fetch_xor_u32(ptr, v, order);
  }

  fn compare_exchange(ref<Self>, expected: u32, new_val: u32,
    success: Ordering, failure: Ordering): Result<u32, u32> {
    const ptr = unsafe { &self.value as *const u32 as *mut u32 };
    @extern("i32.atomic.rmw.cmpxchg")
    return atomic_compare_exchange_u32(ptr, expected, new_val, success, failure);
  }

  fn compare_exchange_weak(ref<Self>, expected: u32, new_val: u32,
    success: Ordering, failure: Ordering): Result<u32, u32> {
    return self.compare_exchange(expected, new_val, success, failure);
  }
}

// ---------- AtomicI64 ----------

/// Atomic 64-bit signed integer. NOT @copy.
struct AtomicI64 {
  value: i64,
}

impl AtomicI64 {
  fn new(v: i64): AtomicI64 { return AtomicI64 { value: v }; }

  fn load(ref<Self>, order: Ordering): i64 {
    @extern("i64.atomic.load")
    return atomic_load_i64(&self.value, order);
  }

  fn store(ref<Self>, v: i64, order: Ordering): void {
    const ptr = unsafe { &self.value as *const i64 as *mut i64 };
    @extern("i64.atomic.store")
    atomic_store_i64(ptr, v, order);
  }

  fn swap(ref<Self>, v: i64, order: Ordering): i64 {
    const ptr = unsafe { &self.value as *const i64 as *mut i64 };
    @extern("i64.atomic.rmw.xchg")
    return atomic_swap_i64(ptr, v, order);
  }

  fn fetch_add(ref<Self>, v: i64, order: Ordering): i64 {
    const ptr = unsafe { &self.value as *const i64 as *mut i64 };
    @extern("i64.atomic.rmw.add")
    return atomic_fetch_add_i64(ptr, v, order);
  }

  fn fetch_sub(ref<Self>, v: i64, order: Ordering): i64 {
    const ptr = unsafe { &self.value as *const i64 as *mut i64 };
    @extern("i64.atomic.rmw.sub")
    return atomic_fetch_sub_i64(ptr, v, order);
  }

  fn fetch_and(ref<Self>, v: i64, order: Ordering): i64 {
    const ptr = unsafe { &self.value as *const i64 as *mut i64 };
    @extern("i64.atomic.rmw.and")
    return atomic_fetch_and_i64(ptr, v, order);
  }

  fn fetch_or(ref<Self>, v: i64, order: Ordering): i64 {
    const ptr = unsafe { &self.value as *const i64 as *mut i64 };
    @extern("i64.atomic.rmw.or")
    return atomic_fetch_or_i64(ptr, v, order);
  }

  fn fetch_xor(ref<Self>, v: i64, order: Ordering): i64 {
    const ptr = unsafe { &self.value as *const i64 as *mut i64 };
    @extern("i64.atomic.rmw.xor")
    return atomic_fetch_xor_i64(ptr, v, order);
  }

  fn compare_exchange(ref<Self>, expected: i64, new_val: i64,
    success: Ordering, failure: Ordering): Result<i64, i64> {
    const ptr = unsafe { &self.value as *const i64 as *mut i64 };
    @extern("i64.atomic.rmw.cmpxchg")
    return atomic_compare_exchange_i64(ptr, expected, new_val, success, failure);
  }

  fn compare_exchange_weak(ref<Self>, expected: i64, new_val: i64,
    success: Ordering, failure: Ordering): Result<i64, i64> {
    return self.compare_exchange(expected, new_val, success, failure);
  }
}

// ---------- AtomicU64 ----------

/// Atomic 64-bit unsigned integer. NOT @copy.
struct AtomicU64 {
  value: u64,
}

impl AtomicU64 {
  fn new(v: u64): AtomicU64 { return AtomicU64 { value: v }; }

  fn load(ref<Self>, order: Ordering): u64 {
    @extern("i64.atomic.load")
    return atomic_load_u64(&self.value, order);
  }

  fn store(ref<Self>, v: u64, order: Ordering): void {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.store")
    atomic_store_u64(ptr, v, order);
  }

  fn swap(ref<Self>, v: u64, order: Ordering): u64 {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.xchg")
    return atomic_swap_u64(ptr, v, order);
  }

  fn fetch_add(ref<Self>, v: u64, order: Ordering): u64 {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.add")
    return atomic_fetch_add_u64(ptr, v, order);
  }

  fn fetch_sub(ref<Self>, v: u64, order: Ordering): u64 {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.sub")
    return atomic_fetch_sub_u64(ptr, v, order);
  }

  fn fetch_and(ref<Self>, v: u64, order: Ordering): u64 {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.and")
    return atomic_fetch_and_u64(ptr, v, order);
  }

  fn fetch_or(ref<Self>, v: u64, order: Ordering): u64 {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.or")
    return atomic_fetch_or_u64(ptr, v, order);
  }

  fn fetch_xor(ref<Self>, v: u64, order: Ordering): u64 {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.xor")
    return atomic_fetch_xor_u64(ptr, v, order);
  }

  fn compare_exchange(ref<Self>, expected: u64, new_val: u64,
    success: Ordering, failure: Ordering): Result<u64, u64> {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.cmpxchg")
    return atomic_compare_exchange_u64(ptr, expected, new_val, success, failure);
  }

  fn compare_exchange_weak(ref<Self>, expected: u64, new_val: u64,
    success: Ordering, failure: Ordering): Result<u64, u64> {
    return self.compare_exchange(expected, new_val, success, failure);
  }
}

// ---------- AtomicUsize ----------

/// Atomic pointer-sized unsigned integer. Backed by u64 storage. NOT @copy.
struct AtomicUsize {
  value: u64,
}

impl AtomicUsize {
  fn new(v: usize): AtomicUsize { return AtomicUsize { value: v as u64 }; }

  fn load(ref<Self>, order: Ordering): usize {
    @extern("i64.atomic.load")
    const raw = atomic_load_u64(&self.value, order);
    return raw as usize;
  }

  fn store(ref<Self>, v: usize, order: Ordering): void {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.store")
    atomic_store_u64(ptr, v as u64, order);
  }

  fn swap(ref<Self>, v: usize, order: Ordering): usize {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.xchg")
    const old = atomic_swap_u64(ptr, v as u64, order);
    return old as usize;
  }

  fn fetch_add(ref<Self>, v: usize, order: Ordering): usize {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.add")
    const old = atomic_fetch_add_u64(ptr, v as u64, order);
    return old as usize;
  }

  fn fetch_sub(ref<Self>, v: usize, order: Ordering): usize {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.sub")
    const old = atomic_fetch_sub_u64(ptr, v as u64, order);
    return old as usize;
  }

  fn compare_exchange(ref<Self>, expected: usize, new_val: usize,
    success: Ordering, failure: Ordering): Result<usize, usize> {
    const ptr = unsafe { &self.value as *const u64 as *mut u64 };
    @extern("i64.atomic.rmw.cmpxchg")
    const result = atomic_compare_exchange_u64(ptr, expected as u64, new_val as u64, success, failure);
    match result {
      Result::Ok(old) => { return Result::Ok(old as usize); },
      Result::Err(actual) => { return Result::Err(actual as usize); },
    }
  }
}

// ---------- AtomicBool ----------

/// Atomic boolean. NOT @copy.
struct AtomicBool {
  value: i32,  // stored as i32 for Wasm atomic compatibility
}

impl AtomicBool {
  fn new(v: bool): AtomicBool {
    return AtomicBool { value: if v { 1 } else { 0 } };
  }

  fn load(ref<Self>, order: Ordering): bool {
    @extern("i32.atomic.load")
    const raw = atomic_load_i32(&self.value, order);
    return raw != 0;
  }

  fn store(ref<Self>, v: bool, order: Ordering): void {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    const raw: i32 = if v { 1 } else { 0 };
    @extern("i32.atomic.store")
    atomic_store_i32(ptr, raw, order);
  }

  fn swap(ref<Self>, v: bool, order: Ordering): bool {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    const raw: i32 = if v { 1 } else { 0 };
    @extern("i32.atomic.rmw.xchg")
    const old = atomic_swap_i32(ptr, raw, order);
    return old != 0;
  }

  fn fetch_and(ref<Self>, v: bool, order: Ordering): bool {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    const raw: i32 = if v { 1 } else { 0 };
    @extern("i32.atomic.rmw.and")
    const old = atomic_fetch_and_i32(ptr, raw, order);
    return old != 0;
  }

  fn fetch_or(ref<Self>, v: bool, order: Ordering): bool {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    const raw: i32 = if v { 1 } else { 0 };
    @extern("i32.atomic.rmw.or")
    const old = atomic_fetch_or_i32(ptr, raw, order);
    return old != 0;
  }

  fn fetch_xor(ref<Self>, v: bool, order: Ordering): bool {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    const raw: i32 = if v { 1 } else { 0 };
    @extern("i32.atomic.rmw.xor")
    const old = atomic_fetch_xor_i32(ptr, raw, order);
    return old != 0;
  }

  fn compare_exchange(ref<Self>, expected: bool, new_val: bool,
    success: Ordering, failure: Ordering): Result<bool, bool> {
    const ptr = unsafe { &self.value as *const i32 as *mut i32 };
    const exp: i32 = if expected { 1 } else { 0 };
    const nv: i32 = if new_val { 1 } else { 0 };
    @extern("i32.atomic.rmw.cmpxchg")
    const result = atomic_compare_exchange_i32(ptr, exp, nv, success, failure);
    match result {
      Result::Ok(old) => { return Result::Ok(old != 0); },
      Result::Err(actual) => { return Result::Err(actual != 0); },
    }
  }
}

/// Atomic thread fence.
fn fence(order: Ordering): void {
  @extern("atomic.fence")
  atomic_fence(order);
}
