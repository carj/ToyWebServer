CPPFLAGS=-DBOOST_LOG_DYN_LINK -g -Wall # -Wextra
LDFLAGS=
LDLIBS=-lboost_log -lboost_url

server: server.o
	g++ $(LDFLAGS) -o server server.o $(LDLIBS)

server.o: server.cpp HttpServer.hpp S3HttpServer.hpp
	g++ $(CPPFLAGS) -c server.cpp

clean:
	rm server.o server

