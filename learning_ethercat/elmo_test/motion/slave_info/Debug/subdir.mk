################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../osal.c \
../slaveinfo.c 

OBJS += \
./osal.o \
./slaveinfo.o 

C_DEPS += \
./osal.d \
./slaveinfo.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	aarch64-buildroot-linux-gnu-gcc -I../../../../SOEM-1.3.1/oshw/linux -I../../../../SOEM-1.3.1/osal/linux -I../../../../SOEM-1.3.1/osal -I../../../../SOEM-1.3.1/soem -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


