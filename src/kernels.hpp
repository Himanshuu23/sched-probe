// Instruction table and inline-asm kernels. Each kernel runs kUnroll instructions per loop iteration.
#pragma once
#include <cstdint>

using KernelFn = void (*)(uint64_t);

constexpr int kUnroll = 32;  // must match REP32 / REP4 below and UNROLL in scripts/mca_predict.py

#define REP2(x) x x
#define REP4(x) REP2(REP2(x))
#define REP8(x) REP2(REP4(x))
#define REP32(x) REP4(REP8(x))

// Register setup. x0-x7 / v0-v7 are the chains, x8/x9 / v8/v9 are constant sources.
// Values avoid denormals, overflow and zero chains.
#define INIT_GPR                                                           \
  ".irp i,0,1,2,3,4,5,6,7\nmov x\\i, #7\n.endr\n"                          \
  "mov x8, #0x7fffffffffffffff\nmov x9, #3\n"
#define INIT_FPD                                                           \
  ".irp i,0,1,2,3,4,5,6,7\nfmov d\\i, #1.5\n.endr\n"                       \
  "fmov d8, #1.0\nfmov d9, #1.0\n"
#define INIT_F4S                                                           \
  ".irp i,0,1,2,3,4,5,6,7\nfmov v\\i\\().4s, #1.5\n.endr\n"                    \
  "fmov v8.4s, #1.0\nfmov v9.4s, #1.0\n"
#define INIT_F2D                                                           \
  ".irp i,0,1,2,3,4,5,6,7\nfmov v\\i\\().2d, #1.5\n.endr\n"                    \
  "fmov v8.2d, #1.0\nfmov v9.2d, #1.0\n"
#define INIT_VINT                                                          \
  ".irp i,0,1,2,3,4,5,6,7\nmovi v\\i\\().4s, #3\n.endr\n"                      \
  "movi v8.4s, #5\nmovi v9.4s, #5\n"
// SVE2 is enabled only inside these kernels, so the rest of the program never uses it
#define INIT_SVE_INT ".arch_extension sve2\n" INIT_VINT
#define INIT_SVE_FP ".arch_extension sve2\n" INIT_F4S

#define CLOBBERS "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", \
                 "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "cc"

// ID, needs SVE2, init, latency line (dependent chain), throughput line (\\i = stream 0..7)
// scripts/mca_predict.py reads this list, keep one entry per line.
#define KERNEL_LIST(X)                                                                          \
  X(add_x,    0, INIT_GPR, "add x0, x0, x8",             "add x\\i, x8, x9")                    \
  X(mul_x,    0, INIT_GPR, "mul x0, x0, x8",             "mul x\\i, x8, x9")                    \
  X(madd_x,   0, INIT_GPR, "madd x0, x0, x8, x9",        "madd x\\i, x8, x9, x9")               \
  X(ror_x,    0, INIT_GPR, "ror x0, x0, x9",             "ror x\\i, x8, x9")                    \
  X(sdiv_x,   0, INIT_GPR, "sdiv x0, x8, x0",            "sdiv x\\i, x8, x9")                   \
  X(udiv_x,   0, INIT_GPR, "udiv x0, x8, x0",            "udiv x\\i, x8, x9")                   \
  X(and_x,    0, INIT_GPR, "and x0, x0, x8",             "and x\\i, x8, x9")                    \
  X(eor_x,    0, INIT_GPR, "eor x0, x0, x8",             "eor x\\i, x8, x9")                    \
  X(fadd_d,   0, INIT_FPD, "fadd d0, d0, d8",            "fadd d\\i, d8, d9")                   \
  X(fmul_d,   0, INIT_FPD, "fmul d0, d0, d8",            "fmul d\\i, d8, d9")                   \
  X(fmadd_d,  0, INIT_FPD, "fmadd d0, d0, d8, d9",       "fmadd d\\i, d8, d9, d9")              \
  X(fmax_d,   0, INIT_FPD, "fmax d0, d0, d8",            "fmax d\\i, d8, d9")                   \
  X(fdiv_d,   0, INIT_FPD, "fdiv d0, d0, d8",            "fdiv d\\i, d8, d9")                   \
  X(fsqrt_d,  0, INIT_FPD, "fsqrt d0, d0",               "fsqrt d\\i, d8")                      \
  X(add_4s,   0, INIT_VINT, "add v0.4s, v0.4s, v8.4s",   "add v\\i\\().4s, v8.4s, v9.4s")           \
  X(mul_4s,   0, INIT_VINT, "mul v0.4s, v0.4s, v8.4s",   "mul v\\i\\().4s, v8.4s, v9.4s")           \
  X(mla_4s,   0, INIT_VINT, "mla v0.4s, v8.4s, v9.4s",   "mla v\\i\\().4s, v8.4s, v9.4s")           \
  X(and_16b,  0, INIT_VINT, "and v0.16b, v0.16b, v8.16b", "and v\\i\\().16b, v8.16b, v9.16b")       \
  X(eor_16b,  0, INIT_VINT, "eor v0.16b, v0.16b, v8.16b", "eor v\\i\\().16b, v8.16b, v9.16b")       \
  X(fadd_4s,  0, INIT_F4S, "fadd v0.4s, v0.4s, v8.4s",   "fadd v\\i\\().4s, v8.4s, v9.4s")          \
  X(fmul_4s,  0, INIT_F4S, "fmul v0.4s, v0.4s, v8.4s",   "fmul v\\i\\().4s, v8.4s, v9.4s")          \
  X(fmla_4s,  0, INIT_F4S, "fmla v0.4s, v8.4s, v9.4s",   "fmla v\\i\\().4s, v8.4s, v9.4s")          \
  X(fdiv_4s,  0, INIT_F4S, "fdiv v0.4s, v0.4s, v8.4s",   "fdiv v\\i\\().4s, v8.4s, v9.4s")          \
  X(fsqrt_4s, 0, INIT_F4S, "fsqrt v0.4s, v0.4s",         "fsqrt v\\i\\().4s, v8.4s")                \
  X(fadd_2d,  0, INIT_F2D, "fadd v0.2d, v0.2d, v8.2d",   "fadd v\\i\\().2d, v8.2d, v9.2d")          \
  X(fmul_2d,  0, INIT_F2D, "fmul v0.2d, v0.2d, v8.2d",   "fmul v\\i\\().2d, v8.2d, v9.2d")          \
  X(fmla_2d,  0, INIT_F2D, "fmla v0.2d, v8.2d, v9.2d",   "fmla v\\i\\().2d, v8.2d, v9.2d")          \
  X(sve_add_s,  1, INIT_SVE_INT, "add z0.s, z0.s, z8.s",   "add z\\i\\().s, z8.s, z9.s")            \
  X(sve_mul_s,  1, INIT_SVE_INT, "mul z0.s, z0.s, z8.s",   "mul z\\i\\().s, z8.s, z9.s")            \
  X(sve_fadd_s, 1, INIT_SVE_FP,  "fadd z0.s, z0.s, z8.s",  "fadd z\\i\\().s, z8.s, z9.s")           \
  X(sve_fmul_s, 1, INIT_SVE_FP,  "fmul z0.s, z0.s, z8.s",  "fmul z\\i\\().s, z8.s, z9.s")

// asm volatile + noinline keeps the compiler from removing or merging the kernels
#define DEFINE_KERNELS(ID, SVE, INIT, L, T)                                       \
  [[gnu::noinline]] static void lat_##ID(uint64_t n) {                            \
    asm volatile(INIT "1:\n" REP32(L "\n") "subs %[n], %[n], #1\nb.ne 1b\n"       \
                 : [n] "+r"(n) : : CLOBBERS);                                     \
  }                                                                               \
  [[gnu::noinline]] static void thr_##ID(uint64_t n) {                            \
    asm volatile(INIT "1:\n" REP4(".irp i,0,1,2,3,4,5,6,7\n" T "\n.endr\n")       \
                 "subs %[n], %[n], #1\nb.ne 1b\n"                                 \
                 : [n] "+r"(n) : : CLOBBERS);                                     \
  }

KERNEL_LIST(DEFINE_KERNELS)

struct Kernel {
  const char* name;
  bool sve2;
  KernelFn lat;
  KernelFn thr;
};

#define KERNEL_ENTRY(ID, SVE, INIT, L, T) {#ID, SVE != 0, lat_##ID, thr_##ID},
static const Kernel kernels[] = {KERNEL_LIST(KERNEL_ENTRY)};
