CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
BUILD    := build
MCA      ?= llvm-mca
PYTHON   ?= python3
QEMU     ?= qemu-aarch64
CROSS_CXX ?= aarch64-linux-gnu-g++

all: $(BUILD)/sched_probe

$(BUILD)/sched_probe: src/main.cpp src/kernels.hpp
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp

cross: $(BUILD)/sched_probe-aarch64

$(BUILD)/sched_probe-aarch64: src/main.cpp src/kernels.hpp
	mkdir -p $(BUILD)
	$(CROSS_CXX) $(CXXFLAGS) -static -o $@ src/main.cpp

qemu-check: cross
	$(QEMU) -cpu max $(BUILD)/sched_probe-aarch64 --quick --out $(BUILD)/quick.csv

predict:
	$(PYTHON) scripts/mca_predict.py --mca $(MCA) --out $(BUILD)/predicted.csv

clean:
	rm -rf $(BUILD)

.PHONY: all cross qemu-check predict clean
