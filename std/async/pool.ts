// std/async/pool.ts — Task pool for managing worker threads (RFC-0020 §6)

import { Mutex } from '../sync/mutex';
import { CondVar } from '../sync/condvar';
import { Sender, Receiver, bounded } from '../thread/channel';

/// A task to be executed by the pool.
type Task = own<() -> void>;

/// Error returned by `try_execute` when the pool's queue is full.
struct Full {
  task: Task,
}

impl Full {
  fn into_inner(own<Self>): Task { return self.task; }
}

/// Error returned by `execute`/`try_execute` when the pool has been shut down.
struct PoolClosed {
  task: Task,
}

impl PoolClosed {
  fn into_inner(own<Self>): Task { return self.task; }
}

/// Outcome of a `try_execute` call.
enum TryExecuteError {
  Full(own<Full>),
  Closed(own<PoolClosed>),
}

/// A pool of worker threads that execute submitted tasks.
struct TaskPool {
  workers: own<Vec<Worker>>,
  task_sender: own<Sender<Option<Task>>>,
  active_count: own<Mutex<usize>>,
  queue_depth: own<Mutex<usize>>,
  queue_cap: usize,
  closed: own<Mutex<bool>>,
  condvar: own<CondVar>,
}

struct Worker {
  id: usize,
  handle: own<thread::JoinHandle<void>>,
}

impl TaskPool {
  /// Create a new task pool.
  ///
  /// * `workers`   — number of worker threads (must be > 0).
  /// * `queue_cap` — bounded capacity of the task queue. When full,
  ///                 `execute()` blocks and `try_execute()` returns `Full`.
  fn new(workers: usize, queue_cap: usize): own<TaskPool> {
    assert!(workers > 0);
    assert!(queue_cap > 0);

    let (sender, receiver) = bounded::<Option<Task>>(queue_cap);
    let active_count = Mutex::new(0 as usize);
    let queue_depth = Mutex::new(0 as usize);
    let closed = Mutex::new(false);
    let condvar = CondVar::new();
    let worker_vec: own<Vec<Worker>> = Vec::with_capacity(workers);

    let i: usize = 0;
    while i < workers {
      let rx = receiver.clone();
      let ac = ref active_count;
      let qd = ref queue_depth;
      let cv = ref condvar;
      let handle = task::spawn(|| {
        loop {
          let msg = rx.recv();
          match msg {
            Result::Ok(Option::Some(task)) => {
              {
                let guard = qd.lock();
                if *guard > 0 { *guard = *guard - 1; }
              }
              {
                let guard = ac.lock();
                *guard = *guard + 1;
              }
              task();
              {
                let guard = ac.lock();
                *guard = *guard - 1;
              }
              cv.notify_all();
            },
            // Poison pill (graceful shutdown) or all senders dropped.
            Result::Ok(Option::None) => { break; },
            Result::Err(_) => { break; },
          }
        }
      });
      worker_vec.push(Worker { id: i, handle: handle });
      i = i + 1;
    }

    return TaskPool {
      workers: worker_vec,
      task_sender: sender,
      active_count: active_count,
      queue_depth: queue_depth,
      queue_cap: queue_cap,
      closed: closed,
      condvar: condvar,
    };
  }

  /// Submit a task. Blocks when the queue is full.
  /// Returns `Err(PoolClosed)` if the pool has been shut down.
  fn execute(ref<Self>, work: Task): Result<void, PoolClosed> {
    if self.is_closed() {
      return Result::Err(PoolClosed { task: work });
    }
    self.bump_queue_depth(1);
    match self.task_sender.send(Option::Some(work)) {
      Result::Ok(_) => { return Result::Ok(()); },
      Result::Err(send_err) => {
        // Roll back: the send never took ownership.
        self.bump_queue_depth(-1);
        return Result::Err(PoolClosed { task: unwrap_envelope(send_err.into_inner()) });
      },
    }
  }

  /// Submit a task without blocking.
  /// Returns `Err(Full)` when the queue is full and
  /// `Err(Closed)` when the pool has been shut down.
  fn try_execute(ref<Self>, work: Task): Result<void, TryExecuteError> {
    if self.is_closed() {
      return Result::Err(TryExecuteError::Closed(PoolClosed { task: work }));
    }
    match self.task_sender.try_send(Option::Some(work)) {
      Result::Ok(_) => {
        self.bump_queue_depth(1);
        return Result::Ok(());
      },
      Result::Err(TrySendError::Full(envelope)) => {
        return Result::Err(TryExecuteError::Full(Full { task: unwrap_envelope(envelope) }));
      },
      Result::Err(TrySendError::Disconnected(envelope)) => {
        return Result::Err(TryExecuteError::Closed(PoolClosed { task: unwrap_envelope(envelope) }));
      },
    }
  }

  /// Adjust queue_depth by `delta`, saturating at 0.
  fn bump_queue_depth(ref<Self>, delta: isize): void {
    let guard = self.queue_depth.lock();
    if delta >= 0 {
      *guard = *guard + (delta as usize);
    } else {
      let d = (-delta) as usize;
      if *guard > d { *guard = *guard - d; } else { *guard = 0; }
    }
  }

  /// Graceful shutdown: stop accepting new tasks, drain the remaining
  /// queue, then join every worker.
  fn shutdown(own<Self>): void {
    self.mark_closed();
    self.send_poison_pills();
    self.join_all();
  }

  /// Force shutdown: stop accepting, discard queued tasks that have not
  /// started yet, then join. In-flight tasks still run to completion.
  fn shutdown_now(own<Self>): void {
    self.mark_closed();
    // Queued tasks are dropped by the channel's receiver-drop path; we
    // zero the counter eagerly so `queue_depth()` reflects the cancel.
    {
      let guard = self.queue_depth.lock();
      *guard = 0;
    }
    self.send_poison_pills();
    self.join_all();
  }

  /// Mark the pool closed so further `execute` / `try_execute` short-circuit.
  fn mark_closed(ref<Self>): void {
    let guard = self.closed.lock();
    *guard = true;
  }

  /// Send one poison pill per worker so each wakes and exits cleanly.
  fn send_poison_pills(ref<Self>): void {
    let i: usize = 0;
    while i < self.workers.len() {
      let _ = self.task_sender.send(Option::None);
      i = i + 1;
    }
  }

  /// Pop and join every worker handle.
  fn join_all(refmut<Self>): void {
    while !self.workers.is_empty() {
      let worker = self.workers.pop().unwrap();
      let _ = worker.handle.join();
    }
  }

  /// Number of tasks currently queued (not yet picked up by a worker).
  fn queue_depth(ref<Self>): usize {
    let guard = self.queue_depth.lock();
    return *guard;
  }

  /// Number of workers currently executing a task.
  fn active_count(ref<Self>): usize {
    let guard = self.active_count.lock();
    return *guard;
  }

  /// Bounded capacity of the internal task queue.
  fn capacity(ref<Self>): usize {
    return self.queue_cap;
  }

  /// Number of worker threads.
  fn worker_count(ref<Self>): usize {
    return self.workers.len();
  }

  /// Has the pool been marked closed?
  fn is_closed(ref<Self>): bool {
    let guard = self.closed.lock();
    return *guard;
  }

  /// Wait until all submitted tasks have completed.
  fn wait_all(ref<Self>): void {
    let guard = self.active_count.lock();
    while *guard > 0 {
      guard = self.condvar.wait(guard);
    }
  }

  /// Submit a function that returns a value, returning a future-like handle.
  fn submit_with_result<T: Send>(ref<Self>, f: () -> own<T>): own<TaskHandle<T>> {
    let (sender, receiver) = bounded::<own<T>>(1);
    let _ = self.execute(|| {
      let result = f();
      let _ = sender.send(result);
    });
    return TaskHandle { receiver: receiver };
  }
}

impl Drop for TaskPool {
  fn drop(refmut<Self>): void {
    // Best-effort fallback; prefer explicit shutdown() / shutdown_now().
    self.send_poison_pills();
  }
}

/// Recover a Task from a rejected `Option<Task>` envelope. A rejection that
/// carries `None` would mean the pool's own shutdown sentinel bounced, which
/// is impossible on the user-submission paths.
fn unwrap_envelope(env: Option<Task>): Task {
  match env {
    Option::Some(t) => { return t; },
    Option::None => { panic!("pool rejected shutdown sentinel"); },
  }
}

/// Handle for retrieving the result of a submitted task.
struct TaskHandle<T> {
  receiver: own<Receiver<own<T>>>,
}

impl<T> TaskHandle<T> {
  /// Block until the result is available.
  fn get(own<Self>): own<T> {
    return self.receiver.recv().unwrap();
  }

  /// Try to get the result without blocking.
  fn try_get(ref<Self>): Option<own<T>> {
    match self.receiver.try_recv() {
      Result::Ok(v) => { return Option::Some(v); },
      Result::Err(_) => { return Option::None; },
    }
  }
}
