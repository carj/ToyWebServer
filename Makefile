CPPFLAGS=-DBOOST_LOG_DYN_LINK -g -Wall -Wextra
LDFLAGS=-g
LDLIBS=-lboost_log

server: server.o
	g++ $(LDFLAGS) -o server server.o $(LDLIBS)

server.o: server.cpp HttpServer.hpp
	g++ $(CPPFLAGS) -c server.cpp

