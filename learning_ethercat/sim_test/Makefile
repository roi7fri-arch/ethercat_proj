# Standalone host tests: SOEM 1.3.1 master + in-process virtual slave(s).
#
# Uses the shared SOEM sources from third_party/soem-1.3.1 (see mk/soem.mk) but
# swaps oshw/linux/nicdrv.c for nicdrv_sim.c, so no NIC / root / real hardware
# is required. Everything else - config loader, PDO setup, telemetry, diag - is
# the *production* code compiled verbatim, so the tests exercise what ships.
#
# Works standalone (`make` here) and from the repo root (`make sim` / `make test`,
# which passes REPO_ROOT in).

REPO_ROOT ?= $(abspath ../..)

# Includes below define rules of their own; pin the default goal first.
.DEFAULT_GOAL := all

include $(REPO_ROOT)/mk/toolchain.mk
include $(REPO_ROOT)/mk/soem.mk
include $(REPO_ROOT)/mk/core.mk
include $(REPO_ROOT)/src/vendors/vendors.mk

MPSOC   := $(REPO_ROOT)/learning_ethercat/elmo_test/motion/elmo_motion_test_mpsoc
CFLAGS  := $(COMMON_CFLAGS) -I. $(SOEM_INC) $(CORE_INC) $(VENDOR_INC)
LDFLAGS := $(COMMON_LDLIBS)

# SOEM core + the simulated transport and virtual slave.
COMMON := $(SOEM_CORE_SRC) nicdrv_sim.c slave_sim.c

# Vendor-neutral core, now under src/ (config loader, CiA 402, PDO binding).
CORE     := $(CJSON_SRC) $(CORE_DIR)/config/config_loader.c
CIA402   := $(CORE_DIR)/drive/cia402.c
PDO_BIND := $(CORE_DIR)/pdo/pdo_bind.c
AXIS     := $(CORE_DIR)/drive/axis.c
PROFILE  := $(CORE_DIR)/drive/drive_profile.c $(VENDOR_SRC)
MOTION   := $(CORE_DIR)/motion/motion.c $(CORE_DIR)/motion/ECD_Motor.c
PARAMS   := $(CORE_DIR)/params/param_set.c $(CORE_DIR)/params/param_apply.c

# PRE-OP setup now goes through the profile registry; ecat_coe.c is the only
# piece that touches SOEM.
SETUP_SRC := $(PROFILE) $(CORE_DIR)/ecat/ecat_coe.c

TARGETS := sim_test stack_tests gui_config_test telemetry_sim_test diag_sim_test \
           multivendor_sim_test \
           cia402_test pdo_bind_test axis_test drive_profile_test motion_test \
           param_test config_loader_test

all: $(TARGETS)

# --- pure host tests: no SOEM, no virtual bus ---------------------------- #
cia402_test: $(CIA402) cia402_test.c
	$(CC) $(CFLAGS) $^ -o $@

pdo_bind_test: $(CORE) $(CIA402) $(PDO_BIND) pdo_bind_test.c
	$(CC) $(CFLAGS) $^ -o $@

axis_test: $(CORE) $(CIA402) $(PDO_BIND) $(AXIS) axis_test.c
	$(CC) $(CFLAGS) $^ -o $@

drive_profile_test: $(CORE) $(CIA402) $(PROFILE) drive_profile_test.c
	$(CC) $(CFLAGS) $^ -o $@

motion_test: $(MOTION) motion_test.c
	$(CC) $(CFLAGS) $^ -o $@ -lm

param_test: $(CORE) $(CIA402) $(PROFILE) $(PARAMS) param_test.c
	$(CC) $(CFLAGS) $^ -o $@

config_loader_test: $(CORE) config_loader_test.c
	$(CC) $(CFLAGS) $^ -o $@

# --- tests against the in-process virtual bus ---------------------------- #
sim_test: $(COMMON) sim_test_main.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

stack_tests: $(COMMON) stack_tests.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

gui_config_test: $(COMMON) $(CORE) $(CIA402) $(SETUP_SRC) gui_config_test.c
	$(CC) $(CFLAGS) -I$(MPSOC) $^ -o $@ $(LDFLAGS)

telemetry_sim_test: $(COMMON) $(CORE) $(CIA402) $(SETUP_SRC) $(MPSOC)/telemetry.c telemetry_sim_test.c
	$(CC) $(CFLAGS) -I$(MPSOC) $^ -o $@ $(LDFLAGS)

diag_sim_test: $(COMMON) $(CORE) $(CIA402) $(SETUP_SRC) $(CORE_DIR)/ecat/ecat_diag.c $(CORE_DIR)/ecat/ecat_foe.c diag_sim_test.c
	$(CC) $(CFLAGS) -I$(MPSOC) $^ -o $@ $(LDFLAGS)

# A bus with no Elmo on it: proves a new drive family needs no core change.
multivendor_sim_test: $(COMMON) $(CORE) $(CIA402) $(PDO_BIND) $(AXIS) $(SETUP_SRC) \
                      slave_sim_profile.c multivendor_sim_test.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Run the assertion-based suites (sim_test is the interactive demo).
test: cia402_test pdo_bind_test axis_test drive_profile_test motion_test \
      param_test config_loader_test stack_tests gui_config_test \
      telemetry_sim_test diag_sim_test multivendor_sim_test
	@echo "===== cia402_test ====="         && ./cia402_test
	@echo "===== pdo_bind_test ====="       && ./pdo_bind_test
	@echo "===== axis_test ====="           && ./axis_test
	@echo "===== drive_profile_test =====" && ./drive_profile_test
	@echo "===== motion_test ====="         && ./motion_test
	@echo "===== param_test ====="          && ./param_test
	@echo "===== config_loader_test =====" && ./config_loader_test
	@echo "===== stack_tests ====="         && ./stack_tests
	@echo "===== gui_config_test ====="     && ./gui_config_test
	@echo "===== telemetry_sim_test =====" && ./telemetry_sim_test
	@echo "===== diag_sim_test ====="       && ./diag_sim_test
	@echo "===== multivendor_sim_test =====" && ./multivendor_sim_test

clean:
	rm -f $(TARGETS)

.PHONY: all test clean
