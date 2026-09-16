# Vendor (drive-family) specific code.
#
# Convention: one directory per drive family under src/vendors/<vendor>/
#
#   src/vendors/elmo/       elmo_platinum.c, elmo_gold.c, elmo_objects.h, ...
#   src/vendors/copley/     copley_accelnet.c, ...
#   src/vendors/maxon/      maxon_epos4.c, ...
#
# Rules for anything living in here:
#   1. It may include src/drive/ and src/pdo/ headers. It must NOT be included
#      BY them - the dependency only ever points inwards.
#   2. Each file registers exactly one `drive_profile_t` whose `.id` matches the
#      "profile" string in the JSON config (e.g. "elmo_platinum").
#   3. Anything that is plain CiA 402 (controlword bit patterns, the
#      0x06 -> 0x07 -> 0x0F enable sequence, 0x6040/0x6041/0x6060/0x6061)
#      belongs in src/drive/cia402.c, NOT here. A vendor file should only hold
#      what is genuinely non-standard for that family: manufacturer object
#      indices, start-up quirks, fault-code tables, multi-axis-per-slave layout.
#
# Adding a new drive family is therefore: create the directory, drop one .c in
# it, add its profile to the registry. No edits to this file are needed - the
# glob below picks it up automatically.

VENDORS_DIR := $(REPO_ROOT)/src/vendors

VENDOR_SRC  := $(wildcard $(VENDORS_DIR)/*.c) $(wildcard $(VENDORS_DIR)/*/*.c)
VENDOR_INC  := -I$(VENDORS_DIR) \
               $(addprefix -I,$(sort $(dir $(wildcard $(VENDORS_DIR)/*/))))
