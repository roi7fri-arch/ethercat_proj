################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../data_functions.c \
../elmo_com.c \
../elmo_device.c \
../joysitck.c \
../osal.c \
../params.c \
../slave_mapping_info.c 

OBJS += \
./data_functions.o \
./elmo_com.o \
./elmo_device.o \
./joysitck.o \
./osal.o \
./params.o \
./slave_mapping_info.o 

C_DEPS += \
./data_functions.d \
./elmo_com.d \
./elmo_device.d \
./joysitck.d \
./osal.d \
./params.d \
./slave_mapping_info.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	arm-buildroot-linux-gnueabihf-gcc -I../../../../SOEM-1.3.1/oshw/linux -I../../../../SOEM-1.3.1/soem -I../../../../SOEM-1.3.1/osal/ -I../../../../SOEM-1.3.1/osal/linux -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


