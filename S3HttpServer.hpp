#include <filesystem>
#include <fcntl.h>
#include <sys/xattr.h>

#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/compute/detail/sha1.hpp>

#include "HttpServer.hpp"

struct PathDetails
{
    PathDetails() : bucket(""), key(""), hash("") {}
    std::string bucket;
    std::string key;
    std::string hash;
    std::filesystem::path bucket_path;
    std::filesystem::path object_path;

    bool has_key()
    {
        return (!key.empty());
    }

    bool has_bucket()
    {
        return (!bucket.empty());
    }
};

class S3HttpServer : public HttpServer
{
public:
    S3HttpServer(unsigned short port, const char *storage_root, const char *path) : HttpServer(port, storage_root), m_path(path)
    {
        using namespace boost;

        BOOST_LOG_TRIVIAL(debug) << "S3HttpServer Started";

        urls::url_view u = urls::parse_origin_form(path).value();

        // list of path parts which are fixed and not including the bucket/key
        for (auto seg : u.encoded_segments())
            m_path_parts.push_back(seg.decode());
    }

private:
    /**
     *  Strip the fixed url parts from the full url
     *  to leave the bucket and key
     */
    PathDetails getParts(Request &request)
    {

        std::list<std::string> path_segments = request.segments();

        path_segments.remove_if([this](std::string s)
                                { return count(m_path_parts.begin(), m_path_parts.end(), s); });

        PathDetails details;

        if (path_segments.size() > 0)
        {
            details.bucket = path_segments.front();
            details.bucket_path = getRootPath() / std::filesystem::path{details.bucket};
        }
        if (path_segments.size() > 1)
        {
            path_segments.pop_front();
            details.key = boost::algorithm::join(path_segments, "/");
            boost::compute::detail::sha1 sha1{details.key};
            details.hash = sha1;
            details.object_path = details.bucket_path / details.hash;
        }

        return details;
    }

protected:
    virtual void DELETE(Request &request, int client_socket)
    {
        PathDetails details = getParts(request);

        if (details.has_bucket() && !details.has_key())
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;

            DELETE_BUCKET(request, client_socket, details);
        }
        else if (details.has_key())
        {

            BOOST_LOG_TRIVIAL(debug) << "KEY: " << details.key;

            DELETE_OBJECT(request, client_socket, details);
        }
        else
        {
            BOOST_LOG_TRIVIAL(debug) << "Invalid Path";
        }
    }

    virtual void HEAD(Request &request, int client_socket)
    {
        PathDetails details = getParts(request);

        if (details.has_bucket() && !details.has_key())
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;

            HEAD_BUCKET(request, client_socket, details);
        }
        else if (details.has_key())
        {
            BOOST_LOG_TRIVIAL(debug) << "KEY: " << details.key;

            HEAD_OBJECT(request, client_socket, details);
        }
        else
        {
            BOOST_LOG_TRIVIAL(debug) << "Invalid Path";
        }
    }

    virtual void GET(Request &request, int client_socket)
    {
        PathDetails details = getParts(request);

        if (details.has_bucket() == false)
        {
            LIST_BUCKET(request, client_socket);
        }
        else if (details.has_bucket() && !details.has_key())
        {
            GET_BUCKET(request, client_socket, details);
        }
        else if (details.has_key())
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            BOOST_LOG_TRIVIAL(debug) << "KEY: " << details.key;

            GET_OBJECT(request, client_socket, details);
        }
    }

    virtual void PUT(Request &request, int client_socket)
    {
        PathDetails details = getParts(request);

        if (details.has_key() == false)
        {
            PUT_BUCKET(request, client_socket, details);
        }
        else
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            BOOST_LOG_TRIVIAL(debug) << "KEY: " << details.key;
            BOOST_LOG_TRIVIAL(debug) << "HASH: " << details.hash;

            PUT_OBJECT(request, client_socket, details);
        }
    }

    virtual void POST(Request &request, int client_socket)
    {
    }

private:
    void DELETE_OBJECT(Request &request, int client_socket, PathDetails &details)
    {
    }

    void PUT_OBJECT(Request &request, int client_socket, PathDetails &details)
    {
        Response response{};
        std::ostringstream ss_cont;

        // Send the 100 Contine message back to the client
        ss_cont << Response::CONTINUE << "\r\n\r\n";
        std::string response_cont(ss_cont.str());
        write(client_socket, response_cont.c_str(), response_cont.size());
        fsync(client_socket);
        BOOST_LOG_TRIVIAL(info) << response_cont;

        // Get the expected message length
        long int length = atol(request.getHeader("content-length").c_str());

        BOOST_LOG_TRIVIAL(info) << "HEADER Length: " << length;

        // Check for a reponse from the client
        char sock_buff[1024];
        recv(client_socket, sock_buff, 128, MSG_PEEK);

        std::ofstream object_file;
        object_file.open(details.object_path);
        ssize_t nread = -1;
        do
        {
            nread = recv(client_socket, sock_buff, 1024, MSG_DONTWAIT);
            if (nread > 0)
                object_file.write(sock_buff, nread);
        } while (nread > 0);
        object_file.close();

        struct stat struct_stat;
        stat(details.object_path.c_str(), &struct_stat);

        BOOST_LOG_TRIVIAL(info) << "Object Size: " << struct_stat.st_size;

        std::ostringstream ss_ok;

        if (struct_stat.st_size == length)
        {
            ss_ok << Response::CREATED << "\n";
        }
        else
        {
            ss_ok << Response::BAD_REQUEST << "\n";
        }

        ss_ok << response.headers_str();
        std::string response_ok(ss_ok.str());
        write(client_socket, response_ok.c_str(), response_ok.size());
        fsync(client_socket);
    }

    void GET_OBJECT(Request &request, int client_socket, PathDetails &details)
    {
    }

    void GET_BUCKET(Request &request, int client_socket, PathDetails &details)
    {
    }

    void LIST_BUCKET(Request &request, int client_socket)
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

    void PUT_BUCKET(Request &request, int client_socket, PathDetails &details)
    {
        Response response{};

        std::ostringstream ss;

        if (std::filesystem::exists(details.bucket_path))
        {
            ss << Response::EXISTS << "\n";
        }
        else
        {
            if (std::filesystem::create_directory(details.bucket_path))
            {
                details.bucket.insert(0, 1, '/');
                response.addHeader("Location", details.bucket);
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

    void DELETE_BUCKET(Request &request, int client_socket, PathDetails &details)
    {
        Response response{};

        std::ostringstream ss;

        if (std::filesystem::exists(details.bucket_path))
        {
            if (std::filesystem::remove(details.bucket_path))
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

    void HEAD_OBJECT(Request &request, int client_socket, PathDetails &details)
    {
        Response response{};

        std::ostringstream ss;

        struct stat path_struct;
        if (stat(details.object_path.c_str(), &path_struct) == 0)
        {
            ss << Response::OK << "\n";
            response.addFileHeaders(&path_struct, details.object_path);
        }
        else
        {
            ss << Response::SERVER_ERROR << "\n";
        }

        ss << response.headers_str();
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

    void HEAD_BUCKET(Request &request, int client_socket, PathDetails &details)
    {
        Response response{};

        std::ostringstream ss;

        if (std::filesystem::exists(details.bucket_path))
            ss << Response::OK << "\n";
        else
            ss << Response::NOT_FOUND << "\n";

        ss << response.headers_str();
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

private:
    std::string m_path;
    std::vector<std::string> m_path_parts;
};