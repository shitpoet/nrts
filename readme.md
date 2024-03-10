# nrts - no-rotate tetris simulator

`nrts` is a fast no-rotate tetris simulator using bit-packing and bit-operations.

`nrts` can do around 20+ million drops per second. Where a drop is placing a piece considering all its possible places + considering all possible drops of a next piece.

The project doesn't have any AI, it just drops pieces at random. But there's an extension point (callback) for an user-provided evaluation function.

## Compilation

```bash
clang++ --std=c++20 -O2 -ffast-math -fno-math-errno -funroll-loops -march=native -fno-exceptions -Wall -Wno-unused-variable -Wno-unused-function main.cc
```

## Some details

Algorithmic approach is very simple. It's essentially a partially parallelized brute force.

The program stores 10x20 playfield as four 64-bit words (called segments).

Each segment contains 5 lines. One bit per cell.

Drop places are found using bit operations.

I tried to write branch-less code, but nevertheless the code have some loops and some branching there and there:

* some loops for moving a piece
* a loop to enumerate drops
* and some other loops and branches

The code doesn't explicitly use SIMD instructions, so there can be a significant room for low-level improvements in that area.

