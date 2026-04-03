//===-- OwnOps.td - Own dialect operations ---------------*- tablegen -*---===//
//
// Operation definitions for the 'own' dialect.
//
//===----------------------------------------------------------------------===//

#ifndef ASC_HIR_OWN_OPS_TD
#define ASC_HIR_OWN_OPS_TD

include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

// Base class for own dialect ops.
class Own_Op<string mnemonic, list<Trait> traits = []>
    : Op<OwnDialect, mnemonic, traits>;

def OwnAllocOp : Own_Op<"alloc"> {
  let summary = "Allocate a new owned value";
  let description = [{
    Allocates a value of type T. Stack by default; heap via escape analysis
    or @heap attribute.
  }];
  let results = (outs AnyType:$result);
  let assemblyFormat = "attr-dict `:` type($result)";
}

def OwnMoveOp : Own_Op<"move"> {
  let summary = "Transfer ownership of a value";
  let description = [{
    Moves ownership from source to result. Source operand is consumed.
  }];
  let arguments = (ins AnyType:$source);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$source attr-dict `:` type($source)";
}

def OwnDropOp : Own_Op<"drop"> {
  let summary = "Drop an owned value";
  let description = [{
    Runs destructor for T (if any) and frees allocation. Inserted by
    drop insertion pass at scope exits.
  }];
  let arguments = (ins AnyType:$value);
  let assemblyFormat = "$value attr-dict `:` type($value)";
}

def OwnCopyOp : Own_Op<"copy"> {
  let summary = "Deep copy a @copy value";
  let description = [{
    Produces a deep copy. Type must have @copy attribute.
  }];
  let arguments = (ins AnyType:$source);
  let results = (outs AnyType:$original, AnyType:$copy);
  let assemblyFormat = "$source attr-dict `:` type($source)";
}

def BorrowRefOp : Own_Op<"borrow.ref"> {
  let summary = "Create a shared borrow";
  let arguments = (ins AnyType:$source);
  let results = (outs AnyType:$borrow);
  let assemblyFormat = "$source attr-dict `:` type($source) `->` type($borrow)";
}

def BorrowMutOp : Own_Op<"borrow.mut"> {
  let summary = "Create an exclusive mutable borrow";
  let arguments = (ins AnyType:$source);
  let results = (outs AnyType:$borrow);
  let assemblyFormat = "$source attr-dict `:` type($source) `->` type($borrow)";
}

#endif // ASC_HIR_OWN_OPS_TD
