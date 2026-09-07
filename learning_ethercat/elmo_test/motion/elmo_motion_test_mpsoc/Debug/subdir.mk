################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ECD_Motor.c \
../cJSON.c \
../config_loader.c \
../data_functions.c \
../ecat_diag.c \
../ecat_foe.c \
../elmo_com.c \
../elmo_config_setup.c \
../elmo_device.c \
../ini.c \
../joysitck.c \
../osal.c \
../params.c \
../slave_mapping_info.c \
../telemetry.c \
../varstable.c 

OBJS += \
./ECD_Motor.o \
./cJSON.o \
./config_loader.o \
./data_functions.o \
./ecat_diag.o \
./ecat_foe.o \
./elmo_com.o \
./elmo_config_setup.o \
./elmo_device.o \
./ini.o \
./joysitck.o \
./osal.o \
./params.o \
./slave_mapping_info.o \
./telemetry.o \
./varstable.o 

C_DEPS += \
./ECD_Motor.d \
./cJSON.d \
./config_loader.d \
./data_functions.d \
./ecat_diag.d \
./ecat_foe.d \
./elmo_com.d \
./elmo_config_setup.d \
./elmo_device.d \
./ini.d \
./joysitck.d \
./osal.d \
./params.d \
./slave_mapping_info.d \
./telemetry.d \
./varstable.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	aarch64-buildroot-linux-gnu-gcc -I../../../../SOEM-1.3.1/oshw/linux -I../../../../SOEM-1.3.1/soem -I../../../../SOEM-1.3.1/osal -I../../../../SOEM-1.3.1/osal/linux -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


