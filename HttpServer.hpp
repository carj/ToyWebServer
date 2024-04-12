#include <sys/socket.h>
#include <arpa/inet.h>


#include <unistd.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <filesystem>
#include <fstream>

#include <boost/algorithm/string.hpp>



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
        HttpServer(unsigned short port, const char* www_root) :  m_server_port(port), m_backlog(32), m_www_root(www_root) {
            m_server_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (m_server_sock > 0) {
                int reuse = 1;
                setsockopt(m_server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
                setsockopt(m_server_sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse));

                std::cout << "Socket Created: " << m_server_sock << "\n";

                socketAddress.sin_family = AF_INET;
                socketAddress.sin_port = htons(m_server_port);
                socketAddress.sin_addr.s_addr = inet_addr("0.0.0.0");

                if (bind(m_server_sock, (struct sockaddr *)&socketAddress, sizeof(socketAddress)) == 0) {
                    std::cout << "Socket Bound to Port: " << m_server_port << "\n";   

                    if (listen(m_server_sock, m_backlog) == 0) {
                        std::cout << "Listening on Port: " << m_server_port << "\n";        
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
                        // close the parent process server socket          
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
                        // close the client socket from the child
                        close(client_socket);     
                    }
                    // close the client socket from the parent
                    close(client_socket);
                }
            }
        }


    private:

        /**
         *  Process any GET request
         *  send the files rqeuested back to the client
         * 
        */
        void GET(Request& request, int client_socket) {
            std::cout << "GET Verb: " << request.path() <<  "\n";  

            // remove the leading "/" 
            auto path = request.path();
            path.erase(0, 1);

            std::filesystem::path root{m_www_root};
            std::filesystem::path content;

            if (path.empty()) {
                content = root / std::filesystem::path("index.html");  
            } else {
                content = root / path;
            }

            // Check file can be read and exists
            if (access(content.c_str(), R_OK) != 0) { 
                std::string response("HTTP/1.1 404 Not Found");
                write(client_socket, response.c_str(), response.size());   
                return; 
            }

            // copy file into byte buffer
            std::size_t size = std::filesystem::file_size(content);
            char buffer[size];
            std::fstream ifs{content, std::ios::in|std::ios::binary};
            ifs.seekg(0);
            ifs.read(buffer, size);
            ifs.close();

            std::ostringstream ss;
            ss << "HTTP/1.1 200 OK\nContent-Type: text/html\nContent-Length: " << size << "\n\n";
            std::string response(ss.str());
            write(client_socket, response.c_str(), response.size());
            write(client_socket, buffer, size);
        }

        void HEAD(Request& request, int client_socket) {}
        void PUT(Request& request, int client_socket) {}
        void POST(Request& request, int client_socket) {}

        Request process_request(std::string& req) {

            auto request_lines = std::vector<std::string>{};
            auto ss = std::stringstream{req};

            for (std::string line; std::getline(ss, line, '\n');)
                request_lines.push_back(line);

            std::string verb_line = request_lines[0];
            request_lines.erase(request_lines.begin());
            Request request(verb_line, request_lines);   
            return request;
        }

        int m_server_sock;
        unsigned short m_server_port;
        int m_backlog;
        std::string m_www_root;
        struct sockaddr_in socketAddress;
};
