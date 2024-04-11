#include <cstdlib>
#include "HttpServer.hpp"

int main() {

    HttpServer server(8080);
    server.Accept();

    return EXIT_SUCCESS;
}