################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ebox.c \
../osal.c 

OBJS += \
./ebox.o \
./osal.o 

C_DEPS += \
./ebox.d \
./osal.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I../../../../SOEM-1.3.1/oshw/linux -I../../../../SOEM-1.3.1/soem -I../../../../SOEM-1.3.1/osal -I../../../../SOEM-1.3.1/osal/linux -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


