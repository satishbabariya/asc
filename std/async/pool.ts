// std/async/pool.ts — Task pool for managing worker threads (RFC-0020)

import { Mutex } from '../sync/mutex';
import { CondVar } from '../sync/condvar';
import { Channel, Sender, Receiver } from '../sync/channel';

/// A task to be executed by the pool.
type Task = own<() -> void>;

/// A pool of worker threads that execute submitted tasks.
struct TaskPool {
  workers: own<Vec<Worker>>,
  task_sender: own<Sender<Option<Task>>>,
  active_count: own<Mutex<usize>>,
  condvar: own<CondVar>,
}

struct Worker {
  id: usize,
  handle: own<thread::JoinHandle<void>>,
}

impl TaskPool {
  /// Create a new task pool with the given number of worker threads.
  fn new(num_workers: usize): own<TaskPool> {
    assert!(num_workers > 0);
    let chan: own<Channel<Option<Task>>> = Channel::new(num_workers * 4);
    let sender = chan.sender();
    let receiver = chan.receiver();
    let active_count = Mutex::new(0 as usize);
    let condvar = CondVar::new();
    let workers: own<Vec<Worker>> = Vec::with_capacity(num_workers);

    let i: usize = 0;
    while i < num_workers {
      let rx = receiver.clone();
      let ac = ref active_count;
      let cv = ref condvar;
      let handle = thread::spawn(|| {
        loop {
          let msg = rx.recv();
          match msg {
            Option::Some(task) => {
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
            Option::None => { break; }, // Shutdown signal.
          }
        }
      });
      workers.push(Worker { id: i, handle: handle });
      i = i + 1;
    }

    return TaskPool {
      workers: workers,
      task_sender: sender,
      active_count: active_count,
      condvar: condvar,
    };
  }

  /// Submit a task to the pool for execution.
  fn submit(ref<Self>, task: Task): void {
    self.task_sender.send(Option::Some(task));
  }

  /// Submit a function that returns a value, returning a future-like handle.
  fn submit_with_result<T: Send>(ref<Self>, f: () -> own<T>): own<TaskHandle<T>> {
    let chan: own<Channel<own<T>>> = Channel::new(1);
    let sender = chan.sender();
    let receiver = chan.receiver();
    self.submit(|| {
      let result = f();
      sender.send(result);
    });
    return TaskHandle { receiver: receiver };
  }

  /// Get the number of worker threads.
  fn worker_count(ref<Self>): usize {
    return self.workers.len();
  }

  /// Get the number of currently active (running) tasks.
  fn active_tasks(ref<Self>): usize {
    let guard = self.active_count.lock();
    return *guard;
  }

  /// Wait until all submitted tasks have completed.
  fn wait_all(ref<Self>): void {
    let guard = self.active_count.lock();
    while *guard > 0 {
      guard = self.condvar.wait(guard);
    }
  }

  /// Shut down the pool, waiting for all tasks to finish.
  fn shutdown(own<Self>): void {
    // Send shutdown signals.
    let i: usize = 0;
    while i < self.workers.len() {
      self.task_sender.send(Option::None);
      i = i + 1;
    }
    // Join all workers.
    while !self.workers.is_empty() {
      let worker = self.workers.pop().unwrap();
      worker.handle.join();
    }
  }
}

impl Drop for TaskPool {
  fn drop(refmut<Self>): void {
    // Send shutdown signals.
    let i: usize = 0;
    while i < self.workers.len() {
      self.task_sender.send(Option::None);
      i = i + 1;
    }
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
    return self.receiver.try_recv();
  }
}
