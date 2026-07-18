# Benchmark results

Single-run wall-clock times of the LumScript bytecode interpreter against
reference ports of the same benchmarks in Node.js and native C. All ports are
structurally equivalent (same fixed-size data, same loop shapes, same single
workload per run) and every runtime produces bit-identical results.

Ports of the [bolt benchmarks](https://github.com/Beariish/bolt/tree/main/benchmarks).

## Environment

- CPU: Intel Core i5-13600KF
- OS: Windows 11 Home
- lumc: release build (`build.bat release`, MSVC 19.51, `/O2`)
- Node.js: v24.13.0 (V8 JIT; jitless = `node --jitless`, Ignition interpreter only)
- Lua: 5.4.2 (interpreter, luabinaries Win64 build in `../build/lua`)
- C: MSVC 19.51, `cl /O2`

Measured 2026-07-18. Times are a single run; expect a few percent of noise.

## Results

| Benchmark | Workload | Result | lumc | lua 5.4 | node --jitless | node | C /O2 |
|---|---|---|---:|---:|---:|---:|---:|
| recursion | fib(30) | 832040 | 50 ms | 31 ms | 45 ms | 5.1 ms | 2.7 ms |
| mandel | 256x256, 255 iters | 1694719 | 169 ms | 85 ms | 134 ms | 7.0 ms | 4.0 ms |
| fannkuch | n=9 | 30 | 629 ms | 158 ms | 262 ms | 18.7 ms | 13.3 ms |
| nbodies | 500k steps | -0.169097 | 2753 ms | 730 ms | 1260 ms | 34.1 ms | 21.2 ms |

Relative to lumc (higher = faster than lumc):

| Benchmark | lua 5.4 | node --jitless | node | C /O2 |
|---|---:|---:|---:|---:|
| recursion | 1.6x | 1.1x | 9.8x | 18.5x |
| mandel | 2.0x | 1.3x | 25x | 42x |
| fannkuch | 4.0x | 2.4x | 34x | 47x |
| nbodies | 3.8x | 2.2x | 81x | 130x |

## Notes

- The fairest interpreter-to-interpreter comparisons are lumc vs `node
  --jitless` (V8's Ignition) and vs Lua 5.4: lumc is within 1.2-1.7x on
  call-heavy and float-arithmetic code (recursion, mandel) and 2.4-4.2x behind
  on array/struct-heavy code (fannkuch, nbodies). Lua's lead there comes from
  its word-sized dynamic values and single-op table access, versus lumc's raw
  byte-register frames where each element access decodes several operands and
  copies aggregates.
- The Lua ports use idiomatic 1-based tables (0-based indices would fall into
  the table's hash part and unfairly slow Lua); stored values and algorithms
  are otherwise identical.
- Disabling array/slice bounds checks was measured to make no difference
  (within noise) on any benchmark: the checks are predictable branches whose
  cost is dominated by opcode dispatch and operand decoding. The interpreter's
  gap on array-heavy code is per-op overhead, not safety checks.
- nbodies is the most adversarial for the interpreter: its inner loop is a
  dense sequence of `bodies[i].field` accesses, each expanding to a
  slice-ref/load/store op with its own dispatch. Fusing these accesses (or
  caching the element pointer across consecutive accesses to the same element)
  is the most promising optimization target.
- The C recursion port reads `n` from argv; with a constant argument MSVC
  folds `fib(30)` to a literal at compile time and the benchmark measures
  nothing.
- Verification: mandel, nbodies, and fannkuch results were cross-checked
  against independent implementations (Node.js references and, for fannkuch,
  the well-known n=9 value of 30 with checksum 8629). recursion's fib(30) is
  832040 by definition.

## Reproducing

```
run.bat            lumc (builds release lumc.exe first)
run_lua.bat        lua (../build/lua/lua54.exe, falls back to PATH)
run_js.bat         node
run_js_jitless.bat node --jitless
run_c.bat          C (compiles with cl /O2 into ../build/bench_c)
```
