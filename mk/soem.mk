# Single source of truth for the SOEM 1.3.1 dependency.
#
# There used to be three copies of SOEM in the tree (learning_ethercat/SOEM-1.3.1,
# new_SOEM/, c45_SOEM/) that differed only by scattered debug printf's. The one
# that actually builds is now third_party/soem-1.3.1; the other two are
# preserved untouched under archive/soem_variants/ for reference.
#
# Note the directory name. third_party/SOEM is a git submodule tracking upstream
# OpenEtherCATsociety/SOEM, and this copy is NOT upstream: it carries local
# debug output and prebuilt aarch64/arm libraries. Keeping them at separate
# paths means `git submodule update` still works and nobody mistakes our patched
# tree for the pristine one.
#
# Include this fragment and use:
#   $(SOEM_INC)        include flags
#   $(SOEM_CORE_SRC)   protocol stack + OS abstraction, transport-agnostic
#   $(SOEM_NIC_SRC)    real raw-socket transport (link this for hardware builds)
#   $(SOEM_LIB)        path to the archived libsoem.a built by `make soem`
#
# Simulation builds link $(SOEM_CORE_SRC) plus their own nicdrv replacement
# instead of $(SOEM_NIC_SRC) - that is the whole trick behind sim_test.

SOEM_DIR ?= $(REPO_ROOT)/third_party/soem-1.3.1

SOEM_INC := \
   -I$(SOEM_DIR)/soem \
   -I$(SOEM_DIR)/osal \
   -I$(SOEM_DIR)/osal/linux \
   -I$(SOEM_DIR)/oshw \
   -I$(SOEM_DIR)/oshw/linux

SOEM_CORE_SRC := \
   $(SOEM_DIR)/soem/ethercatbase.c \
   $(SOEM_DIR)/soem/ethercatcoe.c \
   $(SOEM_DIR)/soem/ethercatconfig.c \
   $(SOEM_DIR)/soem/ethercatdc.c \
   $(SOEM_DIR)/soem/ethercatfoe.c \
   $(SOEM_DIR)/soem/ethercatmain.c \
   $(SOEM_DIR)/soem/ethercatprint.c \
   $(SOEM_DIR)/soem/ethercatsoe.c \
   $(SOEM_DIR)/osal/linux/osal.c \
   $(SOEM_DIR)/oshw/linux/oshw.c

SOEM_NIC_SRC := $(SOEM_DIR)/oshw/linux/nicdrv.c

SOEM_LIB     := $(BUILD_DIR)/libsoem.a
SOEM_OBJ_DIR := $(BUILD_DIR)/soem
SOEM_OBJS    := $(patsubst $(SOEM_DIR)/%.c,$(SOEM_OBJ_DIR)/%.o,\
                   $(SOEM_CORE_SRC) $(SOEM_NIC_SRC))

$(SOEM_OBJ_DIR)/%.o: $(SOEM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(SOEM_INC) -c $< -o $@

$(SOEM_LIB): $(SOEM_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	@echo "built $@"
