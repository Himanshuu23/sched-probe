// sched-probe: measure instruction latency and reciprocal throughput on AArch64.
#ifndef __aarch64__
#error "sched-probe is AArch64 only; use 'make cross' on other hosts"
#endif

#include <asm/hwcap.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/auxv.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "kernels.hpp"

#ifndef HWCAP2_SVE2
#define HWCAP2_SVE2 (1 << 1)
#endif

constexpr int kWarmup = 3;

static double wall_seconds(KernelFn k, uint64_t iters) {
  timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  k(iters);
  clock_gettime(CLOCK_MONOTONIC, &b);
  return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) * 1e-9;
}

static int open_cycle_counter() {
  perf_event_attr attr;
  memset(&attr, 0, sizeof attr);
  attr.type = PERF_TYPE_HARDWARE;
  attr.size = sizeof attr;
  attr.config = PERF_COUNT_HW_CPU_CYCLES;
  attr.disabled = 1;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  return (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
}

// Returns cycles for one kernel call: from perf if available, else from wall time and a calibrated clock.
class CycleTimer {
 public:
  explicit CycleTimer(uint64_t iters) {
    fd_ = open_cycle_counter();
    if (fd_ >= 0 && perf_cycles(lat_add_x, iters) == 0) {
      close(fd_);  // opened but not counting (seen in some VMs)
      fd_ = -1;
    }
    if (fd_ < 0) hz_ = calibrate_hz(iters * 10);
  }
  ~CycleTimer() {
    if (fd_ >= 0) close(fd_);
  }
  bool uses_perf() const { return fd_ >= 0; }
  double hz() const { return hz_; }

  double cycles(KernelFn k, uint64_t iters) const {
    return fd_ >= 0 ? perf_cycles(k, iters) : wall_seconds(k, iters) * hz_;
  }

 private:
  double perf_cycles(KernelFn k, uint64_t iters) const {
    ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
    k(iters);
    ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
    uint64_t v = 0;
    if (read(fd_, &v, sizeof v) != (ssize_t)sizeof v) v = 0;
    return (double)v;
  }

  // Time a long dependent chain of add and assume 1 cycle per add.
  static double calibrate_hz(uint64_t iters) {
    lat_add_x(iters);  // warm up
    double best = 1e30;
    for (int i = 0; i < 5; i++) best = std::min(best, wall_seconds(lat_add_x, iters));
    return (double)iters * kUnroll / best;
  }

  int fd_ = -1;
  double hz_ = 0;
};

static double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

// Median cycles per instruction over reps runs.
static double measure(const CycleTimer& t, KernelFn k, uint64_t iters, int reps) {
  for (int i = 0; i < kWarmup; i++) k(iters);
  std::vector<double> v;
  for (int i = 0; i < reps; i++) v.push_back(t.cycles(k, iters) / ((double)iters * kUnroll));
  return median(v);
}

static int pin_to_current_cpu() {
  int cpu = sched_getcpu();
  if (cpu < 0) cpu = 0;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  if (sched_setaffinity(0, sizeof set, &set) != 0) perror("sched_setaffinity");
  return cpu;
}

static void usage(const char* prog) {
  fprintf(stderr, "usage: %s [--reps N] [--out file.csv] [--quick]\n", prog);
}

int main(int argc, char** argv) {
  int reps = 11;
  std::string out = "measured.csv";
  bool quick = false;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--reps") && i + 1 < argc) {
      reps = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
      out = argv[++i];
    } else if (!strcmp(argv[i], "--quick")) {
      quick = true;
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (reps < 1) {
    usage(argv[0]);
    return 2;
  }

  const uint64_t iters = quick ? 200 : 200000;
  if (quick) puts("quick mode: timings are not meaningful");

  printf("pinned to cpu %d\n", pin_to_current_cpu());
  CycleTimer timer(iters);
  if (timer.uses_perf())
    puts("timing: perf_event_open, CPU cycles counter");
  else
    printf("timing: clock_gettime, calibrated to %.3f GHz (assumes add latency = 1 cycle)\n",
           timer.hz() / 1e9);

  const bool have_sve2 = (getauxval(AT_HWCAP2) & HWCAP2_SVE2) != 0;

  FILE* f = fopen(out.c_str(), "w");
  if (!f) {
    perror(out.c_str());
    return 1;
  }
  fprintf(f, "instruction,latency_cycles,rthroughput_cycles\n");

  for (const Kernel& k : kernels) {
    if (k.sve2 && !have_sve2) {
      printf("%-10s skipped (no SVE2)\n", k.name);
      continue;
    }
    double lat = measure(timer, k.lat, iters, reps);
    double thr = measure(timer, k.thr, iters, reps);
    fprintf(f, "%s,%.3f,%.3f\n", k.name, lat, thr);
    printf("%-10s latency %7.3f  rthroughput %7.3f\n", k.name, lat, thr);
  }

  fclose(f);
  printf("wrote %s\n", out.c_str());
  return 0;
}
