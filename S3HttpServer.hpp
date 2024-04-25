#include <filesystem>
#include <fcntl.h>

#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>

#include "HttpServer.hpp"

class S3HttpServer : public HttpServer
{

public:
    S3HttpServer(unsigned short port, const char *storage_root, const char *path) : HttpServer(port, storage_root), m_path(path)
    {
        using namespace boost;

        BOOST_LOG_TRIVIAL(debug) << "S3HttpServer Started";

        urls::url_view u = urls::parse_origin_form(path).value();

        for (auto seg : u.encoded_segments())
            m_path_parts.emplace_back(seg.decode());
    }

protected:
    virtual void DELETE(Request &request, int client_socket)
    {
        std::list<std::string> components = request.segments();
        for (auto p : m_path_parts)
        {
            for (auto s : request.segments())
            {
                if (p == s)
                    components.remove(p);
            }
        }

        if (components.size() == 1)
        {
            DELETE_BUCKET(request, client_socket, components.front());
        }
    }

    virtual void HEAD(Request &request, int client_socket)
    {
        std::list<std::string> components = request.segments();
        for (auto p : m_path_parts)
        {
            for (auto s : request.segments())
            {
                if (p == s)
                    components.remove(p);
            }
        }

        if (components.size() == 1)
        {
            std::string bucket = components.front();
            HEAD_BUCKET(request, client_socket, bucket);
        }
        else
        {
            std::string bucket = components.front();
            components.pop_front();
            HEAD_OBJECT(request, client_socket, bucket, components);
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
        std::list<std::string> components = request.segments();
        for (auto p : m_path_parts)
        {
            for (auto s : request.segments())
            {
                if (p == s)
                    components.remove(p);
            }
        }

        if (components.size() == 1)
        {
            PUT_BUCKET(request, client_socket, components.front());
        }
        else
        {
            PUT_OBJECT()
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

    void PUT_BUCKET(Request &request, int client_socket, std::string &bucket)
    {
        std::filesystem::path path = getRootPath() / std::filesystem::path{bucket};
        Response response{};

        std::ostringstream ss;

        if (std::filesystem::exists(path))
        {
            ss << Response::EXISTS << "\n";
        }
        else
        {
            if (std::filesystem::create_directory(path))
            {
                bucket.insert(0, 1, '/');
                response.addHeader("Location", bucket);
                ss << Response::OK << "\n";
            }
            else
            {
                std::ostringstream ss;
                ss << Response::SERVER_ERROR << "\n";
            }
        }

        ss << response.headers_str();
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

    void DELETE_BUCKET(Request &request, int client_socket, std::string &bucket)
    {
        std::filesystem::path path = getRootPath() / std::filesystem::path{bucket};
        Response response{};

        std::ostringstream ss;

        if (std::filesystem::exists(path))
        {
            if (std::filesystem::remove(path))
            {
                ss << Response::NO_CONTENT << "\n";
            }
            else
            {
                ss << Response::SERVER_ERROR << "\n";
            }
        }
        else
        {
            ss << Response::NOT_FOUND << "\n";
        }

        ss << response.headers_str();
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

    void HEAD_OBJECT(Request &request, int client_socket, std::string &bucket, std::list<std::string> &keys)
    {
        Response response{};

        std::filesystem::path path = getRootPath() / std::filesystem::path{bucket};
        for (auto p : keys)
            path = path / p;

        std::ostringstream ss;

        struct stat path_struct;
        if (stat(path.c_str(), &path_struct) == 0)
        {
            ss << Response::OK << "\n";
            response.addFileHeaders(&path_struct, path);
        }
        else
        {
            ss << Response::SERVER_ERROR << "\n";
        }

        ss << response.headers_str();
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

    void HEAD_BUCKET(Request &request, int client_socket, std::string &bucket)
    {
        std::filesystem::path path = getRootPath() / std::filesystem::path{bucket};

        Response response{};

        std::ostringstream ss;

        if (std::filesystem::exists(path))
            ss << Response::OK << "\n";
        else
            ss << Response::NOT_FOUND << "\n";

        ss << response.headers_str();
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

    std::string m_path;
    std::list<std::string> m_path_parts;
};