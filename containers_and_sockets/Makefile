
all: sockets.o container.o

sockets.o: sockets.cpp
	g++ -Wall -std=c++11 sockets.cpp -o sockets

container.o: container.cpp
	g++ -Wall -std=c++11 container.cpp -o container

clean:
	$(RM) container sockets