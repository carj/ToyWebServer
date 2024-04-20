#include <filesystem>
#include <fcntl.h>

#include "HttpServer.hpp"

class S3HttpServer : public HttpServer
{

public:
    S3HttpServer(unsigned short port, const char *storage_root, const char *path) : HttpServer(port, storage_root), m_path(path)
    {
        BOOST_LOG_TRIVIAL(debug) << "S3HttpServer Started";
    }

protected:
    virtual void HEAD(Request &request, int client_socket)
    {
        std::string req_path{request.path()};
        std::string::size_type i = req_path.find(m_path);

        if (i != std::string::npos)
            req_path.erase(i, m_path.length());

        if (req_path.empty())
        {
            req_path = "/";
        }

        if (req_path == "/")
        {
            HEAD_BUCKET(request, client_socket);
        }
    }

    virtual void GET(Request &request, int client_socket)
    {
        std::string req_path{request.path()};
        std::string::size_type i = req_path.find(m_path);

        if (i != std::string::npos)
            req_path.erase(i, m_path.length());

        if (req_path.empty())
        {
            req_path = "/";
        }

        if (req_path == "/")
        {
            GET_BUCKET(request, client_socket);
        }
    }

    virtual void PUT(Request &request, int client_socket)
    {
        std::string req_path{request.path()};
        std::string::size_type i = req_path.find(m_path);

        if (i != std::string::npos)
            req_path.erase(i, m_path.length());

        if (req_path.empty())
        {
            req_path = "/";
        }

        if (req_path == "/")
        {
            PUT_BUCKET(request, client_socket);
        }
    }

    virtual void POST(Request &request, int client_socket)
    {
    }

private:
    void GET_BUCKET(Request &request, int client_socket)
    {
        std::filesystem::path path = getRootPath();

        Response response{};

        std::ostringstream mesg;
        mesg << "<ListAllMyBucketsResult>\n";
        mesg << "\t<Buckets>\n";
        for (const auto &entry : std::filesystem::directory_iterator(path))
        {
            mesg << "\t\t<Bucket>\n";

            struct stat struct_stat;
            stat(entry.path().c_str(), &struct_stat);
            std::string last_mod{std::ctime(&(struct_stat.st_mtim).tv_sec)};
            last_mod.pop_back();

            mesg << "\t\t\t<CreationDate>" << last_mod << "</CreationDate>\n";
            mesg << "\t\t\t<Name>" << entry.path().filename().c_str() << "</Name>\n";
            mesg << "\t\t</Bucket>\n";
        }
        mesg << "\t</Buckets>\n";
        mesg << "</ListAllMyBucketsResult>\n";
        std::string mesg_buff(mesg.str());

        response.setContentLength(mesg_buff.length());
        response.setContentType(".xml");

        std::ostringstream ss;
        ss << Response::OK << "\n";
        ss << response.headers_str();
        ss << mesg_buff;
        std::string response_buff(ss.str());

        write(client_socket, response_buff.data(), response_buff.size());
    }

    void PUT_BUCKET(Request &request, int client_socket)
    {

        std::string bucket = request.getHeader("host");
        std::filesystem::path path = getRootPath() / std::filesystem::path{bucket};
        Response response{};

        if (std::filesystem::exists(path))
        {
            std::ostringstream ss;
            ss << Response::EXISTS << "\n";
            ss << response.headers_str();
            std::string response_buff(ss.str());
            write(client_socket, response_buff.c_str(), response_buff.size());
        }
        else
        {
            if (std::filesystem::create_directory(path))
            {
                bucket.insert(0, 1, '/');
                response.addHeader("Location", bucket);
                std::ostringstream ss;
                ss << Response::OK << "\n";
                ss << response.headers_str();
                std::string response_buff(ss.str());
                write(client_socket, response_buff.c_str(), response_buff.size());
            }
            else
            {
                std::ostringstream ss;
                ss << Response::SERVER_ERROR << "\n";
                ss << response.headers_str();
                std::string response_buff(ss.str());
                write(client_socket, response_buff.c_str(), response_buff.size());
            }
        }
    }

    void HEAD_BUCKET(Request &request, int client_socket)
    {

        std::string bucket = request.getHeader("host");
        std::filesystem::path path = getRootPath() / std::filesystem::path{bucket};

        BOOST_LOG_TRIVIAL(debug) << path;

        Response response{};

        if (std::filesystem::exists(path))
        {
            std::ostringstream ss;
            ss << Response::OK << "\n";
            ss << response.headers_str();
            std::string response_buff(ss.str());
            write(client_socket, response_buff.c_str(), response_buff.size());
        }
        else
        {
            std::ostringstream ss;
            ss << Response::NOT_FOUND << "\n";
            ss << response.headers_str();
            std::string response_buff(ss.str());
            write(client_socket, response_buff.c_str(), response_buff.size());
        }
    }

    void HEAD_KEY(Request &request, int client_socket)
    {
    }

    std::string m_path;
};