#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include <unistd.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <map>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>


class Request {

    public:
        Request(std::string& verb_line, std::vector<std::string>& header_lines)  {

            auto verbs = std::vector<std::string>{};
            auto ss = std::stringstream{verb_line};
            for (std::string s; std::getline(ss, s, ' '); ) 
                verbs.push_back(s);
            
            m_verb = boost::algorithm::trim_copy(verbs[0]);
            m_path = boost::algorithm::trim_copy(verbs[1]);
            m_version = boost::algorithm::trim_copy(verbs[2]);


            for (auto &l: header_lines) {
                std::vector<std::string> result;
                std::string ll = boost::algorithm::trim_copy(l);
                if (ll.size() > 0) {
                    boost::split(result, l, boost::is_any_of(":"));
                    if ( (result[0].size() > 0) && (result[1].size() > 0) ) {
                        std::string key = boost::algorithm::trim_copy(result[0]);
                        std::string value = boost::algorithm::trim_copy(result[1]);
                        if ( (key.size() > 0) && (value.size() > 0) ) 
                            m_headers.emplace(key, value);
                    }
                }
            }
        }

        std::string verb() { return m_verb;}
        std::string path() { return m_path;}
        std::string version() { return m_version;}


    private:
        std::string m_verb;
        std::string m_path;
        std::string m_version;

        std::map<std::string, std::string> m_headers;

};

class HttpServer {
    public:
        HttpServer(unsigned short port) :  server_port(port), backlog(32) {
            m_server_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (m_server_sock > 0) {
                int reuse = 1;
                setsockopt(m_server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
                setsockopt(m_server_sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse));

                std::cout << "Socket Created: " << m_server_sock << "\n";

                socketAddress.sin_family = AF_INET;
                socketAddress.sin_port = htons(server_port);
                socketAddress.sin_addr.s_addr = inet_addr("0.0.0.0");

                if (bind(m_server_sock, (struct sockaddr *)&socketAddress, sizeof(socketAddress)) == 0) {
                    std::cout << "Socket Bound to Port: " << server_port << "\n";   

                    if (listen(m_server_sock, backlog) == 0) {
                        std::cout << "Listening on Port: " << server_port << "\n";        
                    }                
                }
            }
        }
        ~HttpServer() {
            close(m_server_sock);
            std::cout << "Socket Closed: " << m_server_sock << "\n";
        }   

        void Accept() {

            while (true) {
                int client_socket = accept(m_server_sock, NULL, NULL);
                if (client_socket > 0) {
                    if (fork() == 0) {
                        std::cout << "Child Created to Process Request\n";                   
                        close(m_server_sock);
                        char buffer[BUFSIZ];
                        ssize_t bytes = read(client_socket, buffer, BUFSIZ);
                        buffer[bytes] = 0;
                        std::string rcv(buffer);
                        Request request =  process_request(rcv); 
                        std::cout << request.verb() << "\n";

                        if (request.verb() == "GET") {
                            GET(request, client_socket);
                        }   

                        close(client_socket);     
                    }
                    close(client_socket);
                }

            }

        }


    private:

        void GET(Request& request, int client_socket) {
            std::cout << "GET Verb" << "\n";  

            auto path = request.path();
            path.erase(0, 1);

            boost::filesystem::ifstream file;
            boost::filesystem::path content;

            if (path.empty()) {
                const boost::filesystem::path p("./index.html");   
                content = p;     
            } else {
                const boost::filesystem::path p(path);   
                content = p;      
            }

            struct stat stat_struct;
            stat(content.c_str(), &stat_struct);

            std::size_t sz = stat_struct.st_size;
            file.open(content, std::ios_base::binary);
            char buffer[sz];
            file.read(buffer, sz);
            std::ostringstream ss;
            ss << "HTTP/1.1 200 OK\nContent-Type: text/html\nContent-Length: " << sz << "\n\n";
            std::string response(ss.str());
            write(client_socket, response.c_str(), response.size());
            write(client_socket, buffer, sz);
        }

        void HEAD(Request& request, int client_socket) {}
        void PUT(Request& request, int client_socket) {}
        void POST(Request& request, int client_socket) {}

        Request process_request(std::string& req) {

            auto result = std::vector<std::string>{};
            auto ss = std::stringstream{req};

            for (std::string line; std::getline(ss, line, '\n');)
                result.push_back(line);

            std::string verb_line = result[0];
            result.erase(result.begin());
            Request request(verb_line, result);   
            return request;
        }

        int m_server_sock;
        int backlog{32};
        unsigned short server_port{8080};
        struct sockaddr_in socketAddress;
};