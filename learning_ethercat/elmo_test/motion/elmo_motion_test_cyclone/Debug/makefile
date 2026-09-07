################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: elmo_motion_test_cyclone

# Tool invocations
elmo_motion_test_cyclone: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: Cross GCC Linker'
	arm-buildroot-linux-gnueabihf-gcc -L/media/bsp/13313ca3-5e12-437a-858a-ab8400dd8066/learning_ethercat/SOEM-1.3.1/lib/arm -o "elmo_motion_test_cyclone" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(EXECUTABLES)$(OBJS)$(C_DEPS) elmo_motion_test_cyclone
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
