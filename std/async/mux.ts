// std/async/mux.ts — MuxChannel: multiplexes multiple receivers (RFC-0020)

import { Mutex } from '../sync/mutex';
import { Channel, Sender, Receiver } from '../sync/channel';

/// A multiplexed channel that merges multiple input receivers into a single stream.
/// Messages from any input channel are forwarded to the output channel.
struct MuxChannel<T: Send> {
  output_sender: own<Sender<own<T>>>,
  output_receiver: own<Receiver<own<T>>>,
  sources: own<Vec<MuxSource<T>>>,
  closed: own<Mutex<bool>>,
}

struct MuxSource<T> {
  id: usize,
  handle: own<thread::JoinHandle<void>>,
}

impl<T: Send> MuxChannel<T> {
  /// Create a new MuxChannel with the given buffer capacity for the output channel.
  fn new(capacity: usize): own<MuxChannel<T>> {
    let chan: own<Channel<own<T>>> = Channel::new(capacity);
    return MuxChannel {
      output_sender: chan.sender(),
      output_receiver: chan.receiver(),
      sources: Vec::new(),
      closed: Mutex::new(false),
    };
  }

  /// Add a receiver as an input source. Messages from this receiver will be
  /// forwarded to the mux output. Returns the source ID.
  fn add(refmut<Self>, receiver: own<Receiver<own<T>>>): usize {
    let id = self.sources.len();
    let sender = self.output_sender.clone();
    let closed_ref = ref self.closed;

    let handle = thread::spawn(move || {
      loop {
        let msg = receiver.recv();
        match msg {
          Option::Some(val) => {
            let guard = closed_ref.lock();
            if *guard { break; }
            sender.send(val);
          },
          Option::None => { break; }, // Source channel closed.
        }
      }
    });

    self.sources.push(MuxSource { id: id, handle: handle });
    return id;
  }

  /// Add a channel (both sender and receiver), returning the sender for the
  /// caller to use and automatically muxing the receiver.
  fn add_channel(refmut<Self>, capacity: usize): (own<Sender<own<T>>>, usize) {
    let chan: own<Channel<own<T>>> = Channel::new(capacity);
    let sender = chan.sender();
    let receiver = chan.receiver();
    let id = self.add(receiver);
    return (sender, id);
  }

  /// Receive the next message from any of the muxed sources.
  fn recv(ref<Self>): Option<own<T>> {
    return self.output_receiver.recv();
  }

  /// Try to receive a message without blocking.
  fn try_recv(ref<Self>): Option<own<T>> {
    return self.output_receiver.try_recv();
  }

  /// Get a clone of the output receiver for use in select! or other operations.
  fn receiver(ref<Self>): ref<Receiver<own<T>>> {
    return ref self.output_receiver;
  }

  /// Get the number of muxed sources.
  fn source_count(ref<Self>): usize {
    return self.sources.len();
  }

  /// Close the mux channel. No more messages will be forwarded.
  fn close(refmut<Self>): void {
    let guard = self.closed.lock();
    *guard = true;
  }
}

impl<T: Send> Drop for MuxChannel<T> {
  fn drop(refmut<Self>): void {
    self.close();
  }
}

/// Convenience: merge multiple receivers into a single receiver.
function merge<T: Send>(receivers: own<Vec<own<Receiver<own<T>>>>>, capacity: usize): own<MuxChannel<T>> {
  let mux = MuxChannel::new(capacity);
  let i: usize = 0;
  while i < receivers.len() {
    let rx = receivers.remove(0);
    mux.add(rx);
    i = i + 1;
  }
  return mux;
}

/// Select from two receivers, returning whichever produces a value first.
function select2<A: Send, B: Send>(
  rx_a: ref<Receiver<own<A>>>,
  rx_b: ref<Receiver<own<B>>>,
): Either<own<A>, own<B>> {
  // Implementation note: uses runtime select! primitive.
  // This is a simplified representation; the actual implementation
  // would use the task runtime's select mechanism.
  match select! {
    a = rx_a.recv() => Either::Left(a.unwrap()),
    b = rx_b.recv() => Either::Right(b.unwrap()),
  }
}

enum Either<A, B> {
  Left(own<A>),
  Right(own<B>),
}

/// A source function yields successive values. Returns `None` when exhausted.
/// This is a closure-shaped adapter so MuxAsyncIterator can be generic over
/// heterogeneous iterators without requiring `dyn Iterator` trait objects.
type MuxSrc<T> = own<() -> Option<own<T>>>;

/// MuxAsyncIterator<T> multiplexes values from multiple iterator-like sources
/// with fair round-robin scheduling. Inspired by Deno's `@std/async`
/// `MuxAsyncIterator`. Each source is a closure yielding `Option<own<T>>`
/// where `None` signals exhaustion. Exhausted sources are removed so
/// remaining sources continue to interleave fairly. Returns `None` from
/// `next()` once all sources are exhausted. `close()` drops all sources
/// immediately so no further values are produced.
struct MuxAsyncIterator<T> {
  sources: own<Vec<MuxSrc<T>>>,
  cursor: usize,
  closed: bool,
}

impl<T> MuxAsyncIterator<T> {
  /// Create an empty multiplexer.
  fn new(): own<MuxAsyncIterator<T>> {
    return MuxAsyncIterator {
      sources: Vec::new(),
      cursor: 0,
      closed: false,
    };
  }

  /// Register a new source. Sources yield values via their `next` closure;
  /// returning `None` signals that source is exhausted and it will be
  /// removed on the mux's next poll.
  fn add(refmut<Self>, source: MuxSrc<T>): void {
    if self.closed { return; }
    self.sources.push(source);
  }

  /// Number of live (not-yet-exhausted, not-closed) sources.
  fn source_count(ref<Self>): usize {
    return self.sources.len();
  }

  /// True when no live sources remain or `close()` was called.
  fn is_done(ref<Self>): bool {
    return self.closed || self.sources.is_empty();
  }

  /// Return the next value from any source in fair round-robin order.
  /// Exhausted sources are removed. Returns `None` when every source is
  /// exhausted or the mux was closed.
  fn next(refmut<Self>): Option<own<T>> {
    if self.closed { return Option::None; }

    // Each loop turn either yields a value (returning) or removes an
    // exhausted source at the cursor; `attempts` bounds the sweep so we
    // make at most one pass over the currently-live sources.
    let attempts: usize = 0;
    while self.sources.len() > 0 && attempts < self.sources.len() {
      if self.cursor >= self.sources.len() { self.cursor = 0; }
      let src = ref self.sources[self.cursor];
      match src() {
        Option::Some(v) => {
          self.cursor = self.cursor + 1;
          return Option::Some(v);
        },
        Option::None => {
          // Remove the exhausted source; its successor slides into this
          // index, so we leave the cursor where it is and charge an attempt.
          self.sources.remove(self.cursor);
          attempts = attempts + 1;
        },
      }
    }

    return Option::None;
  }

  /// Close the mux: drop all sources and refuse further values. Idempotent.
  fn close(refmut<Self>): void {
    self.closed = true;
    self.sources.clear();
  }
}

impl<T> Drop for MuxAsyncIterator<T> {
  fn drop(refmut<Self>): void {
    self.close();
  }
}
