#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>

#include <unistd.h>
#include <fcntl.h>

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <filesystem>
#include <fstream>
#include <algorithm> 


/**
 *  The client Request 
 * 
 *  The HTTP verb and path 
 *  The HTTP request headers are stored in the map.
 * 
*/
class Request {

    public:
        Request(std::string& verb_line, std::vector<std::string>& header_lines)  {

            auto verbs = std::vector<std::string>{};
            auto ss = std::stringstream{verb_line};
            for (std::string s; std::getline(ss, s, ' '); ) {
                verbs.push_back(trim(s));
            }
            
            m_verb = verbs[0];
            m_path = verbs[1];
            m_version = verbs[2];

            for (std::string &line: header_lines) {
                line = trim(line);
                if (!line.empty()) {
                    auto npos = line.find(":", 0); 
                    auto key = line.substr(0, npos);
                    auto value = line.substr(npos+1, line.length());
                    m_headers.emplace(trim(key), trim(value));
                }       
            }
        }

        std::string verb() { return m_verb;}
        std::string path() { return m_path;}
        std::string version() { return m_version;}
        std::map<std::string, std::string> headers() { return m_headers;}


    private:

        // trim from start (in place)
        inline void ltrim(std::string &s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
        }

        // trim from end (in place)
        inline void rtrim(std::string &s) {
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), s.end());
        }

        inline std::string trim(std::string &s) {
            rtrim(s);
            ltrim(s);
            return s;
        }

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

                struct sockaddr_in socketAddress;

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


        /**
         *  Wait for client requests
         *  fork() a child process for each request
         * 
        */
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

                        // parse the client request
                        Request request =  process_request(rcv); 

                        // process the GET requests
                        if (request.verb() == "GET") {
                            GET(request, client_socket);
                        }   
                        
                        // close the client socket from the child
                        close(client_socket);     

                        _exit(0);
                    }
                    // close the client socket from the parent
                    close(client_socket);
                }
            }
        }


    private:

        std::string_view OK{"HTTP/1.1 200 OK"};
        std::string_view NF{"HTTP/1.1 404 Not Found"};

        /**
         *  Process any GET request
         *  send the files rqeuested back to the client
         * 
        */
        void GET(Request& request, int client_socket) {
            std::cout << "GET Verb: " << request.path() <<  "\n";  

            std::filesystem::path root{m_www_root};
            std::filesystem::path req_path{request.path()};

            std::filesystem::path full_path = root;
            full_path += req_path;

            if (std::filesystem::is_directory(full_path)) {
              full_path += std::filesystem::path("/index.html");  
            }
                   
            // Check file can be read and exists
            if (access(full_path.c_str(), R_OK) != 0) { 
                write(client_socket, NF.data(), NF.size());   
                return; 
            }

            std::size_t size = std::filesystem::file_size(full_path);
            
            std::ostringstream ss;
            ss << OK << "\n" << "Content-Type: text/html" << "\n" << "Content-Length: " << size << "\n\n";
            std::string response(ss.str());
            write(client_socket, response.c_str(), response.size());

            int in_fd = open(full_path.c_str(), O_RDONLY);
            sendfile(client_socket, in_fd, NULL, size);
            
        }

        /**
        *  Parse the request from the client 
        *  including the request headers
        */
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
        
};
