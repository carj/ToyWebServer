Header file Tiny C++ Webserver (HTTP Only)
Only Supports HEAD and GET methods
Uses a few boost libraries for logging and url parsing.

g++ -g -DBOOST_LOG_DYN_LINK   server.cpp -Wall  -o server -lboost_log -lboost_url

