//===-- TaskOps.td - Task dialect operations --------------*- tablegen -*---===//
//
// Operation definitions for the 'task' dialect.
//
//===----------------------------------------------------------------------===//

#ifndef ASC_HIR_TASK_OPS_TD
#define ASC_HIR_TASK_OPS_TD

include "mlir/IR/OpBase.td"

class Task_Op<string mnemonic, list<Trait> traits = []>
    : Op<TaskDialect, mnemonic, traits>;

def TaskSpawnOp : Task_Op<"spawn"> {
  let summary = "Spawn a concurrent task";
  let description = [{
    Spawns a task. All captured values must be Send (!own.val with send=true).
  }];
  let arguments = (ins Variadic<AnyType>:$captures);
  let results = (outs AnyType:$handle);
  let regions = (region SizedRegion<1>:$body);
}

def TaskJoinOp : Task_Op<"join"> {
  let summary = "Wait for task completion";
  let arguments = (ins AnyType:$handle);
  let results = (outs AnyType:$result);
}

def ChanMakeOp : Task_Op<"chan.make"> {
  let summary = "Create a bounded channel";
  let arguments = (ins I32:$capacity);
  let results = (outs AnyType:$tx, AnyType:$rx);
}

def ChanSendOp : Task_Op<"chan.send"> {
  let summary = "Send value through channel (consumes ownership)";
  let arguments = (ins AnyType:$tx, AnyType:$value);
}

def ChanRecvOp : Task_Op<"chan.recv"> {
  let summary = "Receive value from channel (takes ownership)";
  let arguments = (ins AnyType:$rx);
  let results = (outs AnyType:$value);
}

#endif // ASC_HIR_TASK_OPS_TD
