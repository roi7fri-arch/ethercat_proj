# Standalone CLI utilities under src/tools/.
#
# Each file in src/tools/ has its own main(), so the directory is deliberately
# excluded from CORE_SRC (see mk/core.mk) and built as separate binaries here.
#
#   ecat_param_tool   read/write drive parameters and push files over FoE.
#                     This is what the configuration GUI shells out to when the
#                     engineer presses "Download to bus".
#
# Bus access needs a raw socket. Either run as root or, once per build:
#   sudo setcap cap_net_raw,cap_net_admin+eip build/host/ecat_param_tool

TOOLS_DIR := $(REPO_ROOT)/src/tools

TOOL_NAMES := ecat_param_tool
TOOL_BINS  := $(addprefix $(BUILD_DIR)/,$(TOOL_NAMES))

TOOL_INC := $(SOEM_INC) $(CORE_INC) $(VENDOR_INC)

TOOL_OBJ_DIR := $(BUILD_DIR)/tools
TOOL_LIB_OBJS := \
   $(patsubst $(REPO_ROOT)/src/%.c,$(TOOL_OBJ_DIR)/src/%.o,$(CORE_SRC) $(VENDOR_SRC)) \
   $(TOOL_OBJ_DIR)/third_party/cJSON.o

$(TOOL_OBJ_DIR)/src/%.o: $(REPO_ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(TOOL_INC) -c $< -o $@

$(TOOL_OBJ_DIR)/third_party/cJSON.o: $(CJSON_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(TOOL_INC) -c $< -o $@

$(TOOL_OBJ_DIR)/%.o: $(TOOLS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(TOOL_INC) -c $< -o $@

# The core objects include the RT application's motion model, which needs -lm.
$(BUILD_DIR)/%: $(TOOL_OBJ_DIR)/%.o $(TOOL_LIB_OBJS) $(SOEM_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(TOOL_OBJ_DIR)/$*.o $(TOOL_LIB_OBJS) $(SOEM_LIB) -o $@ \
	      $(COMMON_LDLIBS) -lm
	@echo "built $@"
