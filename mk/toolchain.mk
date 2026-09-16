# Toolchain selection shared by every build in this repo.
#
# Pick a target with TARGET=<name> on the make command line:
#   make                      -> host    (native gcc, for sim tests / unit tests)
#   make TARGET=aarch64       -> Xilinx MPSoC   (aarch64-buildroot-linux-gnu-)
#   make TARGET=arm           -> Intel Cyclone  (arm-buildroot-linux-gnueabihf-)
#
# Every artefact lands under build/$(TARGET)/ so the three targets can coexist
# without cleaning in between.

TARGET ?= host

ifeq ($(TARGET),host)
  CROSS_COMPILE :=
else ifeq ($(TARGET),aarch64)
  CROSS_COMPILE ?= aarch64-buildroot-linux-gnu-
else ifeq ($(TARGET),arm)
  CROSS_COMPILE ?= arm-buildroot-linux-gnueabihf-
else
  $(error unknown TARGET '$(TARGET)' - use host, aarch64 or arm)
endif

CC  := $(CROSS_COMPILE)gcc
AR  := $(CROSS_COMPILE)ar
LD  := $(CROSS_COMPILE)gcc

BUILD_DIR := $(REPO_ROOT)/build/$(TARGET)

# Warnings are deliberately not -Werror yet: the legacy sources still trip them.
# Tighten this once the refactor (src/) replaces learning_ethercat/.
COMMON_CFLAGS := -Wall -O2 -g
COMMON_LDLIBS := -lpthread -lrt
