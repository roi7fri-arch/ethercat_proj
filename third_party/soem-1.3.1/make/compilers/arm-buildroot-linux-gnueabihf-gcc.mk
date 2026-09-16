# Prefix of the cross-compiler
CROSS_GCC := arm-buildroot-linux

# Common settings
include $(PRJ_ROOT)/make/compilers/gcc.mk

# Machine settings
-include $(PRJ_ROOT)/make/compilers/$(ARCH).mk

# Default machine settings
#MACHINE ?= -mlittle-endian -mthumb

# Compiler flags
CFLAGS  += $(MACHINE) -fshort-wchar
LDFLAGS += $(MACHINE) -Wl,--no-wchar-size-warning
