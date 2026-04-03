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
