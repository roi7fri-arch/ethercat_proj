# Top-level build for the EtherCAT master project.
#
#   make                 build libsoem.a for the host (native gcc)
#   make TARGET=aarch64  build libsoem.a for the Xilinx MPSoC
#   make TARGET=arm      build libsoem.a for the Intel Cyclone
#   make app             build the master application
#   make tools           build the CLI utilities (ecat_param_tool)
#   make sim             build the simulated-bus test binaries
#   make test            build + run every test suite (C and config GUI)
#   make clean           remove build/ and the sim_test binaries
#
# Artefacts go to build/$(TARGET)/ ; nothing is written into third_party/.

REPO_ROOT := $(CURDIR)

# Includes below define rules of their own; pin the default goal first.
.DEFAULT_GOAL := all

include $(REPO_ROOT)/mk/toolchain.mk
include $(REPO_ROOT)/mk/soem.mk
include $(REPO_ROOT)/mk/core.mk
include $(REPO_ROOT)/src/vendors/vendors.mk
include $(REPO_ROOT)/mk/app.mk
include $(REPO_ROOT)/mk/tools.mk

SIM_DIR := $(REPO_ROOT)/learning_ethercat/sim_test
GUI_DIR := $(REPO_ROOT)/tools/config_gui

PYTHON ?= python3

.PHONY: all soem app tools sim test test-c test-gui clean

all: soem

soem: $(SOEM_LIB)

app: $(APP_BIN)

tools: $(TOOL_BINS)

sim:
	$(MAKE) -C $(SIM_DIR) REPO_ROOT=$(REPO_ROOT)

test: test-c test-gui

test-c:
	$(MAKE) -C $(SIM_DIR) REPO_ROOT=$(REPO_ROOT) test

# Guards against the GUI's profile list drifting away from src/vendors/.
# Skipped with a notice when python3 is not installed.
test-gui:
	@if command -v $(PYTHON) >/dev/null 2>&1; then \
	   echo "===== config_gui meta ====="; \
	   $(PYTHON) $(GUI_DIR)/backend/test_meta.py; \
	 else \
	   echo "skipping config GUI tests: $(PYTHON) not found"; \
	 fi

clean:
	rm -rf $(REPO_ROOT)/build
	$(MAKE) -C $(SIM_DIR) REPO_ROOT=$(REPO_ROOT) clean
