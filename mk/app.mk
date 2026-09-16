# Production master application (currently the Elmo MPSoC motion app).
#
# Replaces the Eclipse-generated Debug/makefile, which hard-codes absolute
# paths from other developers' machines (/mnt/storageDevice/..., /media/bsp/...)
# and therefore does not build anywhere else.
#
#   make app                   build for the host (useful for compile checks)
#   make app TARGET=aarch64    build for the Xilinx MPSoC
#
# This will move to src/app/ once the refactor lands; the target name stays.

APP_DIR  := $(REPO_ROOT)/learning_ethercat/elmo_test/motion/elmo_motion_test_mpsoc
APP_NAME := ecat_master
APP_BIN  := $(BUILD_DIR)/$(APP_NAME)

# Explicit list: the directory also holds standalone test mains
# (config_loader_test.c, telemetry_test.c) that must not be linked in.
APP_SRC := \
   $(APP_DIR)/data_functions.c \
   $(APP_DIR)/elmo_com.c \
   $(APP_DIR)/elmo_device.c \
   $(APP_DIR)/ini.c \
   $(APP_DIR)/joysitck.c \
   $(APP_DIR)/osal.c \
   $(APP_DIR)/params.c \
   $(APP_DIR)/slave_mapping_info.c \
   $(APP_DIR)/telemetry.c \
   $(APP_DIR)/varstable.c

APP_INC := -I$(APP_DIR) $(SOEM_INC) $(CORE_INC) $(VENDOR_INC)

APP_OBJ_DIR := $(BUILD_DIR)/app
APP_OBJS    := $(patsubst $(APP_DIR)/%.c,$(APP_OBJ_DIR)/%.o,$(APP_SRC)) \
               $(patsubst $(REPO_ROOT)/src/%.c,$(APP_OBJ_DIR)/src/%.o,$(CORE_SRC) $(VENDOR_SRC)) \
               $(APP_OBJ_DIR)/third_party/cJSON.o

$(APP_OBJ_DIR)/third_party/cJSON.o: $(CJSON_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(APP_INC) -c $< -o $@

$(APP_OBJ_DIR)/%.o: $(APP_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(APP_INC) -c $< -o $@

$(APP_OBJ_DIR)/src/%.o: $(REPO_ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(APP_INC) -c $< -o $@

# The app carries its own osal.c, so libsoem.a's copy is simply never pulled in.
$(APP_BIN): $(APP_OBJS) $(SOEM_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(APP_OBJS) $(SOEM_LIB) -o $@ $(COMMON_LDLIBS) -lm
	@echo "built $@"
