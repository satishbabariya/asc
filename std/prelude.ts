// std/prelude.ts — Auto-imported into every module.
//
// Contains the most commonly used types, traits, and functions.

// Lifecycle traits.
export { Drop, Clone, Default, Deref, DerefMut } from './core/traits';

// Comparison traits.
export { PartialEq, Eq, PartialOrd, Ord, Ordering } from './core/cmp';

// Iterator traits.
export { Iterator, IntoIterator, FromIterator } from './core/iter';

// Conversion traits.
export { From, Into, TryFrom, TryInto, AsRef, AsMut } from './core/convert';

// Display and Debug.
export { Display, Debug, Formatter, FmtError } from './core/fmt';

// Option and Result.
export { Option, Some, None } from './core/option';
export { Result, Ok, Err } from './result';

// Core collections.
export { Vec } from './vec';
export { String } from './string';

// Memory.
export { Box } from './mem/box';

// Hash.
export { Hash } from './core/hash';

// Operator traits.
export { Add, Sub, Mul, Div, Rem, Neg, Not, Index, IndexMut } from './core/ops';
export { Range, RangeInclusive } from './core/ops';
