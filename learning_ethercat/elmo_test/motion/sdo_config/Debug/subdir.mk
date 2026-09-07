################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../data_functions.c \
../elmo_com.c \
../osal.c \
../params.c 

OBJS += \
./data_functions.o \
./elmo_com.o \
./osal.o \
./params.o 

C_DEPS += \
./data_functions.d \
./elmo_com.d \
./osal.d \
./params.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I../../../../SOEM-1.3.1/oshw/linux -I../../../../SOEM-1.3.1/soem -I../../../../SOEM-1.3.1/osal/linux -I../../../../SOEM-1.3.1/osal -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


