#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>

#include <boost/log/trivial.hpp>
#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>

typedef std::map<std::string, std::string> Headers;
typedef std::map<std::string, std::string> QueryParams;
typedef std::map<std::string, std::string> MimeTypes;

/**
 *  The client Response
 *  Create the headers to send back to the client
 *
 */
class Response
{

public:
    Response() : m_headers(), m_mime()
    {
        const std::time_t result = std::time(nullptr);
        std::string str{std::ctime(&result)};
        str.pop_back();

        // Some Default Headers
        m_headers.emplace("Date", str);
        m_headers.emplace("Content-Length", "0");
        m_headers.emplace("Server", "C++ Test Server");

        mime_types();
    }

    static constexpr std::string_view OK = "HTTP/1.1 200 OK";
    static constexpr std::string_view NOT_FOUND = "HTTP/1.1 404 Not Found";
    static constexpr std::string_view CREATED = "HTTP/1.1 201 Created";
    static constexpr std::string_view NO_CONTENT = "HTTP/1.1 204 No Content";
    static constexpr std::string_view NOT_MODIFIED = "HTTP/1.1 304 Not Modified";
    static constexpr std::string_view BAD_REQUEST = "HTTP/1.1 400 Bad Request";
    static constexpr std::string_view NOT_ALLOWED = "HTTP/1.1 405 Method Not Allowed";
    static constexpr std::string_view EXISTS = "HTTP/1.1 409 Conflict";
    static constexpr std::string_view SERVER_ERROR = "HTTP/1.1 500 Internal Server Error";

    inline void setContentLength(int length) {
        m_headers["Content-Length"] = std::to_string(length);    
    }
    inline void setContentType(std::string extension) {
        m_headers["Content-Type"] = m_mime[extension];
    }

    /**
     *  Add HTTP Response headers which need file information
     *
     */
    inline void addFileHeaders(const struct stat *details, const std::filesystem::path &path)
    {

        std::string last_mod{std::ctime(&(details->st_mtim).tv_sec)};
        last_mod.pop_back();
        m_headers["Last-Modified"] = last_mod;

        std::ostringstream oss;
        oss << details->st_ino << "-" << details->st_size << "-" << details->st_mtim.tv_sec;
        m_headers["Etag"] = oss.str();

        m_headers["Content-Length"] = std::to_string(details->st_size);
        m_headers["Content-Type"] = content_type(path.filename());
    }

    /**
     *  Return the response headers as a single string
     *
     */
    std::string headers_str()
    {
        std::ostringstream ss;
        for (auto const &header : m_headers)
            ss << header.first << ": " << header.second << "\n";

        ss << "\n";
        std::string response(ss.str());
        return response;
    }

    inline void addHeader(std::string key, std::string value) {
        m_headers[key] = value;
    }

    /**
     *  Get a Request Header
     *
     */
    inline std::string getHeader(const char *key)
    {
        if (m_headers.find(key) == m_headers.end())
            return "";
        else
            return m_headers[key];
    }

private:
    /**
     *  return the mime type based on the filename
     *
     */
    inline std::string content_type(const std::filesystem::path &filename)
    {
        std::string extension = filename.extension();
        if (m_mime.find(extension) == m_mime.end())
            return "application/octet-stream";
        else
            return m_mime[filename.extension()];
    }

    /**
     *  define some simple mime types
     *  based on file extensions
     *
     */
    void mime_types()
    {
        m_mime.emplace(".html", "text/html");
        m_mime.emplace(".htm", "text/html");
        m_mime.emplace(".ico", "image/x-icon");
        m_mime.emplace(".png", "image/png");
        m_mime.emplace(".aac", "audio/aac");
        m_mime.emplace(".apng", "image/apng");
        m_mime.emplace(".avi", "video/x-msvideo");
        m_mime.emplace(".bin", "application/octet-stream");
        m_mime.emplace(".css", "text/css");
        m_mime.emplace(".jpeg", "image/jpeg");
        m_mime.emplace(".jpg", "image/jpeg");
        m_mime.emplace(".js", "text/javascript");
        m_mime.emplace(".json", "application/json");
        m_mime.emplace(".mjs", "text/javascript");
        m_mime.emplace(".mp3", "audio/mpeg");
        m_mime.emplace(".mp4", "video/mp4");
        m_mime.emplace(".mpeg", "video/mpeg");
        m_mime.emplace(".svg", "image/svg+xml");
        m_mime.emplace(".tif", "image/tiff");
        m_mime.emplace(".tiff", "image/tiff");
        m_mime.emplace(".ttf", "font/ttf");
        m_mime.emplace(".txt", "text/plain");
        m_mime.emplace(".wav", "audio/wav");
        m_mime.emplace(".weba", "audio/webm");
        m_mime.emplace(".webm", "video/webm");
        m_mime.emplace(".xhtml", "application/xhtml+xml");
        m_mime.emplace(".webp", "image/webp");
        m_mime.emplace(".xml", "application/xml");
    }

    Headers m_headers;
    MimeTypes m_mime;
};

/**
 *  The client Request
 *
 *  The HTTP method and path
 *  The HTTP request headers are stored in the map.
 *
 */
class Request
{

public:
    Request(const std::string &request_line) : m_method(), m_path(), m_version("HTTP/1.1"), m_headers(), m_params()
    {
        using namespace boost;

        auto header_lines = std::vector<std::string>{};
        auto sstream = std::stringstream{request_line};
        for (std::string line; std::getline(sstream, line, '\n');)
            header_lines.push_back(algorithm::trim_copy(line));

        const std::string request_first_line = header_lines[0];
        header_lines.erase(header_lines.begin());

        auto request_parts = std::vector<std::string>{};
        auto sstream_method = std::stringstream{request_first_line};
        for (std::string line; std::getline(sstream_method, line, ' ');)
            request_parts.push_back(algorithm::trim_copy(line));

        m_method = request_parts[0];
        m_version = request_parts[2];

        for (std::string &line : header_lines)
        {
            line = algorithm::trim_copy(line);
            if (!line.empty())
            {
                auto npos = line.find(":", 0);
                auto key = line.substr(0, npos);
                auto value = line.substr(npos + 1, line.length());
                key = boost::algorithm::to_lower_copy(algorithm::trim_copy(key));
                m_headers.emplace(key, algorithm::trim_copy(value));
            }
        }

        std::string url = "http://" + m_headers["Host"] + request_parts[1];
        BOOST_LOG_TRIVIAL(debug) << url;

        urls::url_view u = urls::parse_uri(url).value();
        m_path = u.path();

        for (auto param : u.params())
            m_params.emplace(algorithm::trim_copy(param.key), algorithm::trim_copy(param.value));
    }

    /**
     *  Get a Request Header by its Key
     *
     */
    std::string getHeader(const char *key)
    {
        if (m_headers.find(key) == m_headers.end())
            return "";
        else
            return m_headers[key];
    }

    std::string method() const { return m_method; }
    std::string path() const { return m_path; }
    std::string version() const { return m_version; }
    QueryParams params() { return m_params; }
    Headers headers() { return m_headers; }

private:
    std::string m_method;
    std::string m_path;
    std::string m_version;
    Headers m_headers;
    QueryParams m_params;
};

/**
 *  The Webserver
 *
 *  Call Accept() to start the server
 *
 */
class HttpServer
{
public:
    HttpServer(unsigned short port, const char *www_root) : m_server_port(port), m_backlog(32), m_www_root(www_root)
    {
        m_server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (m_server_sock > 0)
        {
            int reuse = 1;
            setsockopt(m_server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
            setsockopt(m_server_sock, SOL_SOCKET, SO_REUSEPORT, (const char *)&reuse, sizeof(reuse));

            BOOST_LOG_TRIVIAL(debug) << "Socket Created: " << std::to_string(m_server_sock);

            struct sockaddr_in socketAddress;

            socketAddress.sin_family = AF_INET;
            socketAddress.sin_port = htons(m_server_port);
            socketAddress.sin_addr.s_addr = inet_addr("0.0.0.0");

            if (bind(m_server_sock, (struct sockaddr *)&socketAddress, sizeof(socketAddress)) == 0)
            {
                BOOST_LOG_TRIVIAL(debug) << "Socket Bound to Port: " << m_server_port;

                if (listen(m_server_sock, m_backlog) == 0)
                {
                    BOOST_LOG_TRIVIAL(info) << "Listening on Port: " << m_server_port;
                }
            }
            else
            {
                BOOST_LOG_TRIVIAL(error) << strerror(errno);
                throw std::runtime_error("Cannot Bind to Port");
            }
        }
    }

    ~HttpServer()
    {
        close(m_server_sock);
        BOOST_LOG_TRIVIAL(debug) << "Socket Closed: " << std::to_string(m_server_sock);
    }

    /**
     *  Wait for client requests
     *  fork() a child process for each request
     *
     */
    void Accept()
    {
        while (true)
        {
            struct sockaddr_in clientAddress;
            socklen_t socklen = sizeof(clientAddress);
            int client_socket = accept(m_server_sock, (struct sockaddr *)&clientAddress, &socklen);
            BOOST_LOG_TRIVIAL(debug) << "Client Connection From: " << inet_ntoa(clientAddress.sin_addr);
            if (client_socket > 0)
            {
                if (fork() == 0)
                {
                    // close the parent process server socket
                    close(m_server_sock);

                    char buffer[BUFSIZ];
                    ssize_t bytes = read(client_socket, buffer, BUFSIZ);
                    buffer[bytes] = 0;
                    std::string rcv(buffer);

                    // parse the client request
                    Request request{rcv};

                    BOOST_LOG_TRIVIAL(info) << "Method: " << request.method();
                    BOOST_LOG_TRIVIAL(info) << "Path: " << request.path();

                    // process the GET requests
                    if (request.method() == "GET")
                    {
                        GET(request, client_socket);
                    }

                    if (request.method() == "HEAD")
                    {
                        HEAD(request, client_socket);
                    }

                    if (request.method() == "PUT")
                    {
                        PUT(request, client_socket);
                    }

                    if (request.method() == "POST")
                    {
                        POST(request, client_socket);
                    }

                    // close the client socket from the child
                    close(client_socket);

                    // exit the child process
                    _exit(0);
                }
                // close the client socket from the parent
                close(client_socket);
            }
        }
    }

protected:

    virtual void PUT(Request &request, int client_socket) {
        not_allowed(client_socket);
    }

    virtual void POST(Request &request, int client_socket) {
        not_allowed(client_socket);
    }

    /**
     *  process HEAD requests
     *
     *  Send response headers but no content
     *
     */
    virtual void HEAD(Request &request, int client_socket)
    {
        std::filesystem::path root{m_www_root};
        std::filesystem::path req_path{request.path()};

        std::filesystem::path full_path = root;
        full_path += req_path;

        if (std::filesystem::is_directory(full_path))
            full_path += std::filesystem::path("/index.html");

        Response response{};

        // Check file can be read and exists
        if (access(full_path.c_str(), R_OK) != 0)
        {
            std::ostringstream ss;
            ss << Response::NOT_FOUND << "\n";
            ss << response.headers_str();
            std::string response_buff(ss.str());
            write(client_socket, response_buff.c_str(), response_buff.size());
            return;
        }

        struct stat file_details;
        if (stat(full_path.c_str(), &file_details) == 0)
        {
            response.addFileHeaders(&file_details, full_path);
            std::ostringstream ss;
            ss << Response::OK << "\n";
            ss << response.headers_str();
            std::string response_buff(ss.str());
            write(client_socket, response_buff.c_str(), response_buff.size());
        }
    }

    /**
     *  Process any GET request
     *  send the files rqeuested back to the client
     *
     */
    virtual void GET(Request &request, int client_socket)
    {

        std::filesystem::path root{m_www_root};
        std::filesystem::path req_path{request.path()};

        std::filesystem::path full_path = root;
        full_path += req_path;

        if (std::filesystem::is_directory(full_path))
            full_path += std::filesystem::path("/index.html");

        Response response{};

        // Check file can be read and exists
        if (access(full_path.c_str(), R_OK) != 0)
        {
            std::ostringstream ss;
            ss << Response::NOT_FOUND << "\n";
            ss << response.headers_str();
            std::string response_buff(ss.str());
            write(client_socket, response_buff.c_str(), response_buff.size());
            return;
        }

        struct stat file_details;
        if (stat(full_path.c_str(), &file_details) == 0)
        {
            response.addFileHeaders(&file_details, full_path);
            // check for etag match
            if (request.getHeader("If-None-Match") == response.getHeader("Etag"))
            {
                std::ostringstream ss;
                ss << Response::NOT_MODIFIED << "\n";
                ss << response.headers_str();
                std::string response_buff(ss.str());
                write(client_socket, response_buff.c_str(), response_buff.size());
            }
            else
            {
                std::ostringstream ss;
                ss << Response::OK << "\n";
                ss << response.headers_str();
                std::string response_buff(ss.str());
                write(client_socket, response_buff.c_str(), response_buff.size());

                // send content
                off_t off = 0;
                int in_fd = open(full_path.c_str(), O_RDONLY);
                ssize_t sentbytes = sendfile(client_socket, in_fd, &off, file_details.st_size);
                close(in_fd);

                if (sentbytes != file_details.st_size)
                    BOOST_LOG_TRIVIAL(error) << "Error sending file contents";
            }
        }
    }

    std::string getRootPath() { return std::filesystem::path(m_www_root); }

private:
    void not_allowed(int client_socket)
    {
        Response response{};

        std::ostringstream ss;
        ss << Response::NOT_ALLOWED << "\n";
        ss << "Allow: GET, HEAD"
           << "\n";
        ss << response.headers_str();
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

    int m_server_sock;
    unsigned short m_server_port;
    int m_backlog;
    std::string m_www_root;
};
