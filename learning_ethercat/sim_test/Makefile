# Standalone host tests: SOEM 1.3.1 master + in-process virtual slave(s).
# Builds SOEM from source but replaces oshw/linux/nicdrv.c with nicdrv_sim.c,
# so no NIC / root / real hardware is required.

SOEM   := ../SOEM-1.3.1
MPSOC  := ../elmo_test/motion/elmo_motion_test_mpsoc
CC     := gcc
CFLAGS := -Wall -O2 -g \
          -I. \
          -I$(SOEM)/soem \
          -I$(SOEM)/osal \
          -I$(SOEM)/osal/linux \
          -I$(SOEM)/oshw \
          -I$(SOEM)/oshw/linux
LDFLAGS := -lpthread -lrt

# SOEM core (everything except the real nicdrv.c, which we replace).
SOEM_SRC := \
   $(SOEM)/soem/ethercatbase.c \
   $(SOEM)/soem/ethercatcoe.c \
   $(SOEM)/soem/ethercatconfig.c \
   $(SOEM)/soem/ethercatdc.c \
   $(SOEM)/soem/ethercatfoe.c \
   $(SOEM)/soem/ethercatmain.c \
   $(SOEM)/soem/ethercatprint.c \
   $(SOEM)/soem/ethercatsoe.c \
   $(SOEM)/osal/linux/osal.c \
   $(SOEM)/oshw/linux/oshw.c

# SOEM core + the simulated transport and virtual slave.
COMMON := $(SOEM_SRC) nicdrv_sim.c slave_sim.c

# Production config-loading code (reused verbatim from the target project).
GUI_SRC := $(MPSOC)/cJSON.c $(MPSOC)/config_loader.c $(MPSOC)/elmo_config_setup.c

TARGETS := sim_test stack_tests gui_config_test telemetry_sim_test diag_sim_test

all: $(TARGETS)

sim_test: $(COMMON) sim_test_main.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

stack_tests: $(COMMON) stack_tests.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

gui_config_test: $(COMMON) $(GUI_SRC) gui_config_test.c
	$(CC) $(CFLAGS) -I$(MPSOC) $^ -o $@ $(LDFLAGS)

telemetry_sim_test: $(COMMON) $(GUI_SRC) $(MPSOC)/telemetry.c telemetry_sim_test.c
	$(CC) $(CFLAGS) -I$(MPSOC) $^ -o $@ $(LDFLAGS)

diag_sim_test: $(COMMON) $(GUI_SRC) $(MPSOC)/ecat_diag.c $(MPSOC)/ecat_foe.c diag_sim_test.c
	$(CC) $(CFLAGS) -I$(MPSOC) $^ -o $@ $(LDFLAGS)

# Run the assertion-based suites (sim_test is the interactive demo).
test: stack_tests gui_config_test telemetry_sim_test diag_sim_test
	@echo "===== stack_tests ====="        && ./stack_tests
	@echo "===== gui_config_test ====="    && ./gui_config_test
	@echo "===== telemetry_sim_test =====" && ./telemetry_sim_test
	@echo "===== diag_sim_test ====="      && ./diag_sim_test

clean:
	rm -f $(TARGETS)

.PHONY: all test clean
