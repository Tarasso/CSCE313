# makefile

all: server client

common.o: common.h common.cpp
	g++ -g -w -std=c++11 -c common.cpp

FIFOreqchannel.o: FIFOreqchannel.h FIFOreqchannel.cpp
	g++ -g -w -std=c++11 -c FIFOreqchannel.cpp

MQreqchannel.o: MQreqchannel.h MQreqchannel.cpp
	g++ -g -w -std=c++11 -c MQreqchannel.cpp

SHMreqchannel.o: SHMreqchannel.h SHMreqchannel.cpp
	g++ -g -w -std=c++11 -c SHMreqchannel.cpp

client: client.cpp FIFOreqchannel.o common.o MQreqchannel.o SHMreqchannel.o
	g++ -g -w -std=c++11 -o client client.cpp FIFOreqchannel.o MQreqchannel.o common.o SHMreqchannel.o -lpthread -lrt

server: server.cpp  FIFOreqchannel.o common.o MQreqchannel.o SHMreqchannel.o
	g++ -g -w -std=c++11 -o server server.cpp FIFOreqchannel.o MQreqchannel.o common.o SHMreqchannel.o -lpthread -lrt

clean:
	rm -rf *.o *.csv fifo* server client data*_*
