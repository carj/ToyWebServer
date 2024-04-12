#include <cstdlib>
#include "HttpServer.hpp"

int main() {

    const char* content_root = "/home/Webserver/www";

    HttpServer server(8080, content_root);
    server.Accept();

    return EXIT_SUCCESS;
}
