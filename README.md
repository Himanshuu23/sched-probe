# sched-probe

A small C++20 microbenchmark for AArch64 that measures instruction latency and
reciprocal throughput, and compares the numbers with the predictions of
llvm-mca for the neoverse-v2 CPU model.

For each instruction in `src/kernels.hpp` there are two kernels, both written
as unrolled inline asm (32 instructions per loop iteration):

- latency: one dependent chain, each instruction uses the previous result
- throughput: 8 independent streams, each with its own destination register

`scripts/mca_predict.py` reads the same instruction list, generates matching
asm snippets, runs llvm-mca and writes a CSV in the same format as the harness.
`scripts/compare.py` joins the two CSVs.

31 instructions are covered: integer, scalar double FP, NEON, and four SVE2
ones. The SVE2 kernels are skipped at runtime if the CPU does not support it.

## Requirements

- Linux on AArch64 for the harness (or an x86 machine with `aarch64-linux-gnu-g++`
  and `qemu-aarch64` for a correctness check)
- g++ with C++20 support
- llvm-mca from LLVM 17 or newer (older versions have no neoverse-v2 model)
- Python 3; matplotlib is optional (only for the chart)

## Build

    make                # native build on AArch64 -> build/sched_probe
    make cross          # static cross build -> build/sched_probe-aarch64
    make qemu-check     # cross build, then run under qemu-aarch64 with --quick
    make predict        # llvm-mca predictions -> build/predicted.csv
    make clean

If llvm-mca has a versioned name, use `make predict MCA=llvm-mca-18`.

## Run

    ./build/sched_probe --reps 21 --out build/measured.csv
    python3 scripts/compare.py build/measured.csv build/predicted.csv --png build/compare.png

Flags:

- `--reps N`: number of timed repetitions, the median is reported (default 11)
- `--out file.csv`: output file (default `measured.csv`)
- `--quick`: very small iteration counts, only for checking that the kernels run
  (for example under QEMU); the timings are not meaningful

To use a particular core, start it with `taskset -c N`; the program pins itself
to the core it starts on.

Timing uses the `perf_event_open` CPU cycles counter. If that is not available
it falls back to `clock_gettime`, with the clock rate calibrated by timing a long
dependent chain of `add` and assuming 1 cycle per add. The method used is
printed at startup.

`compare.py` prints measured and predicted values with the percent difference
`(measured - predicted) / predicted * 100`, marks rows differing by more than
20%, and saves one PNG with two bar charts (latency and throughput, log scale).

## Output format

    instruction,latency_cycles,rthroughput_cycles

Both columns are cycles per instruction. The measured and predicted CSVs use
the same format. Instruction names come from the table in `src/kernels.hpp`.

## Results

The comparison below is between llvm-mca (neoverse-v2) and the values in
Arm's Neoverse V2 Core Software Optimization Guide (Issue 3.0, r0p2), stored
in `data/arm_v2.csv`. Values were checked against the guide's instruction tables (3-3, 3-4, 3-6, 3-11, 3-15, 3-16, 3-24, 3-25). Latency is the guide's execution latency (the
accumulate latency for `mla` and `fmla`, which chain through the
accumulator). Reciprocal throughput is 1 divided by the guide's execution
throughput. For divide and square root the guide gives a range; the slow end
of the range is used.

    python3 scripts/compare.py data/arm_v2.csv build/predicted.csv --label reference --png build/compare.png

    instruction  lat reference  lat pred   diff % thr reference  thr pred   diff %
    add_x                1.000     1.001     -0.1         0.167     0.168     -0.6
    mul_x                2.000     2.001     -0.0         0.500     0.501     -0.2
    madd_x               2.000     2.001     -0.0         1.000     1.001     -0.1
    ror_x                1.000     1.001     -0.1         0.167     0.168     -0.6
    sdiv_x              20.000    20.001     -0.0        20.000    20.001     -0.0
    udiv_x              20.000    20.001     -0.0        20.000    20.001     -0.0
    and_x                1.000     1.001     -0.1         0.167     0.168     -0.6
    eor_x                1.000     1.001     -0.1         0.167     0.168     -0.6
    fadd_d               2.000     2.001     -0.0         0.250     0.251     -0.4
    fmul_d               3.000     3.001     -0.0         0.250     0.252     -0.8
    fmadd_d              4.000     4.001     -0.0         0.250     0.252     -0.8
    fmax_d               2.000     2.001     -0.0         0.250     0.251     -0.4
    fdiv_d              15.000    15.001     -0.0         3.500     4.003    -12.6
    fsqrt_d             16.000    16.001     -0.0         3.750     4.003     -6.3
    add_4s               2.000     2.001     -0.0         0.250     0.251     -0.4
    mul_4s               4.000     4.001     -0.0         0.500     0.502     -0.4
    mla_4s               1.000     1.002     -0.2         0.500     0.502     -0.4
    and_16b              2.000     2.001     -0.0         0.250     0.251     -0.4
    eor_16b              2.000     2.001     -0.0         0.250     0.251     -0.4
    fadd_4s              2.000     2.001     -0.0         0.250     0.251     -0.4
    fmul_4s              3.000     3.001     -0.0         0.250     0.252     -0.8
    fmla_4s              2.000     2.002     -0.1         0.250     0.252     -0.8
    fdiv_4s             10.000    10.001     -0.0         4.500     5.001    -10.0
    fsqrt_4s            10.000    10.001     -0.0         4.500     4.501     -0.0
    fadd_2d              2.000     2.001     -0.0         0.250     0.251     -0.4
    fmul_2d              3.000     3.001     -0.0         0.250     0.252     -0.8
    fmla_2d              2.000     2.002     -0.1         0.250     0.252     -0.8
    sve_add_s            2.000     2.001     -0.0         0.250     0.251     -0.4
    sve_mul_s            4.000     4.001     -0.0         0.500     0.502     -0.4
    sve_fadd_s           2.000     2.001     -0.0         0.250     0.251     -0.4
    sve_fmul_s           3.000     3.001     -0.0         0.250     0.252     -0.8

0 of 31 rows differ by more than 20%. The largest difference is the
throughput of `fdiv_d` (-12.6%), which comes from the range choice above.
The llvm-mca model agrees closely with the guide. This is a model-vs-documentation
check, not a hardware measurement. Measured results from the harness on a real
Neoverse V2 machine are still to do.

## Status and future work

Done: the harness builds for AArch64 and runs correctly under QEMU (functional
check only), and the llvm-mca neoverse-v2 predictions have been compared with
Arm's published values as shown above.

Not done: the harness has not been run on real AArch64 hardware, because I did
not have access to a machine. No measured results are included. Planned next:

- run `sched_probe` on a Neoverse V2 machine (for example AWS Graviton4) and
  record the timing method it reports
- compare the measured CSV with the llvm-mca predictions and the Arm values
- investigate any rows that differ by more than 20% and add them here

## Limitations

- AArch64 Linux only.
- `perf_event_open` may be blocked by `kernel.perf_event_paranoid` or by a VM;
  the clock fallback is only as good as its fixed-frequency assumption.
- Each loop iteration also contains `subs` and `b.ne` (2 extra instructions per 32).
- Division timings depend on operand values; the chains alternate between a
  large and a small quotient.
- `madd` and `fmadd` chain through a multiplicand. `fmla` and `mla` can only
  chain through the accumulator, which may have forwarding that is faster than
  the multiplier path.
- With 8 streams, accumulating instructions (`fmla`, `mla`) may not reach the
  true peak throughput if their latency divided by 8 is above the real
  reciprocal throughput.
- The SVE kernels use whatever vector length the CPU has; llvm-mca's model
  assumes the neoverse-v2 width.
- llvm-mca is a static model, so differences from the hardware are expected,
  especially for divide and square root.
- Results under QEMU only show that the code runs.
