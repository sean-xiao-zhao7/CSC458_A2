################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
O_SRCS += \
../client.o \
../connection_demux.o \
../mysock.o \
../mysock_api.o \
../network.o \
../network_io.o \
../network_io_socket.o \
../network_io_tcp.o \
../proxyget.o \
../server.o \
../stcp_api.o \
../tcp_sum.o \
../transport.o 

C_SRCS += \
../client.c \
../connection_demux.c \
../mysock.c \
../mysock_api.c \
../network.c \
../network_io.c \
../network_io_socket.c \
../network_io_tcp.c \
../proxyget.c \
../server.c \
../stcp_api.c \
../tcp_sum.c \
../transport.c 

OBJS += \
./client.o \
./connection_demux.o \
./mysock.o \
./mysock_api.o \
./network.o \
./network_io.o \
./network_io_socket.o \
./network_io_tcp.o \
./proxyget.o \
./server.o \
./stcp_api.o \
./tcp_sum.o \
./transport.o 

C_DEPS += \
./client.d \
./connection_demux.d \
./mysock.d \
./mysock_api.d \
./network.d \
./network_io.d \
./network_io_socket.d \
./network_io_tcp.d \
./proxyget.d \
./server.d \
./stcp_api.d \
./tcp_sum.d \
./transport.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


