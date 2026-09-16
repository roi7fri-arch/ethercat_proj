# Vendor-neutral core of the master, under src/.
#
#   src/config/  JSON bus configuration -> plain C structs (no SOEM)
#   src/params/  drive parameter sets: CoE values + files to push (no SOEM)
#   src/ecat/    EtherCAT transport: master init, DC, ESM, recovery, diagnostics
#   src/pdo/     process-image access: semantic signal -> bit offset binding
#   src/drive/   generic drive layer: CiA 402 state machine, axis object, registry
#   src/motion/  trajectory / control algorithms (no EtherCAT knowledge)
#   src/tools/   standalone CLI utilities (each has its own main())
#   src/app/     wiring and the RT loop
#
# Dependencies only ever point downwards in that list. Nothing here may include
# a header from src/vendors/ - vendors plug in through the profile registry.
#
# Directories are globbed, so adding a .c file needs no edit to this fragment.
# Test mains must NOT live under src/ or they end up linked into the app; put
# them in learning_ethercat/sim_test/ alongside the other suites.

CORE_DIR := $(REPO_ROOT)/src

# src/tools is deliberately NOT here: every file in it has its own main().
CORE_LAYERS := config params ecat pdo drive motion app

CORE_SRC := $(foreach l,$(CORE_LAYERS),$(wildcard $(CORE_DIR)/$(l)/*.c))
CORE_INC := $(foreach l,$(CORE_LAYERS),-I$(CORE_DIR)/$(l)) -I$(CORE_DIR)/tools

# Bundled cJSON (used by src/config/config_loader.c). Kept out of CORE_SRC so
# each build can place its object where it likes; its include path is part of
# CORE_INC because config_loader.c needs the header.
CJSON_DIR := $(REPO_ROOT)/third_party/cJSON
CJSON_SRC := $(CJSON_DIR)/cJSON.c
CJSON_INC := -I$(CJSON_DIR)

CORE_INC += $(CJSON_INC)
