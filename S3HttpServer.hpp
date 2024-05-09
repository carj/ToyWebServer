#include <filesystem>
#include <fcntl.h>
#include <sys/xattr.h>

#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/compute/detail/sha1.hpp>

#include "HttpServer.hpp"

struct CustomMetadata
{

    CustomMetadata() : prefix("x-amz-meta-") {}
    bool operator()(const std::pair<std::string, std::string> &p)
    {
        return (p.first.rfind(prefix, 0) == 0);
    }

public:
    std::string prefix;
};

struct PathDetails
{
    PathDetails(std::list<std::string> path_parts, std::filesystem::path object_root) : bucket(""), key(""), hash("")
    {
        if (path_parts.size() == 0)
        {
            type = PathDetails::TYPE::LIST_BUCKET;
        }
        if (path_parts.size() == 1)
        {
            this->bucket = path_parts.front();
            this->bucket_path = object_root / std::filesystem::path{this->bucket};
            type = PathDetails::TYPE::BUCKET;
        }
        if (path_parts.size() > 1)
        {
            this->bucket = path_parts.front();
            this->bucket_path = object_root / std::filesystem::path{this->bucket};
            path_parts.pop_front();
            this->key = boost::algorithm::join(path_parts, "/");
            boost::compute::detail::sha1 sha1{this->key};
            this->hash = sha1;
            this->object_path = this->bucket_path / this->hash;
            type = PathDetails::TYPE::OBJECT;
        }
    }

    std::string bucket;
    std::string key;
    std::string hash;
    std::filesystem::path bucket_path;
    std::filesystem::path object_path;

    enum class TYPE
    {
        BUCKET,
        OBJECT,
        LIST_BUCKET
    };

    PathDetails::TYPE type;

    static constexpr const char *XATT_PREFIX = "user.S3.";
    static constexpr const char *XATT_MIME_TYPE = "user.S3.MimeType";
    static constexpr const char *XATT_KEY_NAME = "user.S3.Key";
};

class S3HttpServer : public HttpServer
{
public:
    S3HttpServer(unsigned short port, const char *storage_root, const char *path) : HttpServer(port, storage_root)
    {
        using namespace boost;

        BOOST_LOG_TRIVIAL(debug) << "S3HttpServer Started";

        urls::url_view u = urls::parse_origin_form(path).value();

        // list of path parts which are fixed and not including the bucket/key
        for (auto seg : u.encoded_segments())
            m_path_parts.push_back(seg.decode());
    }

protected:
    /**
     *   DELETE either a bucket or object
     *
     */
    virtual void DELETE(Request &request, int client_socket)
    {
        PathDetails details = getParts(request);

        switch (details.type)
        {
        case PathDetails::TYPE::BUCKET:
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            DELETE_BUCKET(request, client_socket, details);
            break;
        }
        case PathDetails::TYPE::OBJECT:
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            BOOST_LOG_TRIVIAL(debug) << "KEY: " << details.key;
            DELETE_OBJECT(request, client_socket, details);
            break;
        }
        default:
        {
            BOOST_LOG_TRIVIAL(debug) << "Invalid Path";
            BadRequest(request, client_socket);
            break;
        }
        }
    }

    virtual void HEAD(Request &request, int client_socket)
    {
        PathDetails details = getParts(request);

        switch (details.type)
        {
        case PathDetails::TYPE::BUCKET:
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            HEAD_BUCKET(request, client_socket, details);
            break;
        }
        case PathDetails::TYPE::OBJECT:
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            BOOST_LOG_TRIVIAL(debug) << "KEY: " << details.key;
            HEAD_OBJECT(request, client_socket, details);
            break;
        }
        default:
        {
            BOOST_LOG_TRIVIAL(debug) << "Invalid Path";
            BadRequest(request, client_socket);
            break;
        }
        }
    }

    virtual void GET(Request &request, int client_socket)
    {
        PathDetails details = getParts(request);

        switch (details.type)
        {
        case PathDetails::TYPE::LIST_BUCKET:
        {
            LIST_BUCKET(request, client_socket, details);
            break;
        }
        case PathDetails::TYPE::OBJECT:
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            BOOST_LOG_TRIVIAL(debug) << "KEY: " << details.key;
            GET_OBJECT(request, client_socket, details);
            break;
        }
        case PathDetails::TYPE::BUCKET:
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            LIST_OBJECT(request, client_socket, details);
            break;
        }
        default:
        {
            BOOST_LOG_TRIVIAL(debug) << "Invalid Path";
            BadRequest(request, client_socket);
            break;
        }
        }
    }

    virtual void PUT(Request &request, int client_socket)
    {
        PathDetails details = getParts(request);

        switch (details.type)
        {
        case PathDetails::TYPE::OBJECT:
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            BOOST_LOG_TRIVIAL(debug) << "KEY: " << details.key;
            BOOST_LOG_TRIVIAL(debug) << "HASH: " << details.hash;
            PUT_OBJECT(request, client_socket, details);
            break;
        }
        case PathDetails::TYPE::BUCKET:
        {
            BOOST_LOG_TRIVIAL(debug) << "BUCKET: " << details.bucket;
            PUT_BUCKET(request, client_socket, details);
            break;
        }
        default:
        {
            BOOST_LOG_TRIVIAL(debug) << "Invalid Path";
            BadRequest(request, client_socket);
            break;
        }
        }
    }

    virtual void POST(Request &request, int client_socket)
    {
    }

private:
    void DELETE_OBJECT(Request &request, int client_socket, PathDetails &details)
    {
        Response response{};

        std::ostringstream ss;

        if (std::filesystem::exists(details.object_path))
        {
            if (std::filesystem::remove(details.object_path))
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

    void PUT_OBJECT(Request &request, int client_socket, PathDetails &details)
    {
        Response response{};
        std::ostringstream ss_cont;

        // NoSuchBucket
        if (!std::filesystem::exists(details.bucket_path))
        {
            BOOST_LOG_TRIVIAL(info) << "NoSuchBucket";
            ss_cont << Response::NOT_FOUND << "\r\n\r\n";
            std::string response_cont(ss_cont.str());
            write(client_socket, response_cont.c_str(), response_cont.size());
            fsync(client_socket);
            return;
        }

        // Send the 100 Contine message back to the client
        ss_cont << Response::CONTINUE << "\r\n\r\n";
        std::string response_cont(ss_cont.str());
        write(client_socket, response_cont.c_str(), response_cont.size());
        fsync(client_socket);
        BOOST_LOG_TRIVIAL(info) << Response::CONTINUE;

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

        setAttributes(details.object_path, details, response, request);

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
        Response response{};

        // Check file can be read and exists
        if (access(details.object_path.c_str(), R_OK) != 0)
        {
            std::ostringstream ss;
            ss << Response::NOT_FOUND << "\n";
            ss << response.headers_str();
            std::string response_buff(ss.str());
            write(client_socket, response_buff.c_str(), response_buff.size());
            return;
        }

        struct stat file_details;
        if (stat(details.object_path.c_str(), &file_details) == 0)
        {
            response.addFileHeaders(&file_details, details.object_path);

            // check for etag match
            if (request.hasHeader("If-None-Match"))
            {
                if (request.getHeader("If-None-Match") == response.getHeader("Etag"))
                {
                    std::ostringstream ss;
                    ss << Response::NOT_MODIFIED << "\n";
                    ss << response.headers_str();
                    std::string response_buff(ss.str());
                    write(client_socket, response_buff.c_str(), response_buff.size());
                    return;
                }
            }

            if (request.hasHeader("If-Match"))
            {
                if (request.getHeader("If-Match") != response.getHeader("Etag"))
                {
                    std::ostringstream ss;
                    ss << Response::PRE_FAILED << "\n";
                    ss << response.headers_str();
                    std::string response_buff(ss.str());
                    write(client_socket, response_buff.c_str(), response_buff.size());
                    return;
                }
            }

            if (request.hasHeader("If-Modified-Since"))
            {
                // TODO
            }

            getAttributes(details.object_path, response.headers());

            // does request contain range request
            if (request.hasHeader("Range"))
            {
                std::string range_request = request.getHeader("Range");
                std::vector<std::string> parts;
                boost::split(parts, range_request, boost::is_any_of("="));
                if ((parts.size() == 2) && (parts[0] == "bytes"))
                {
                    std::string range = parts[1];
                    BOOST_LOG_TRIVIAL(info) << "ByteRange: " << range;

                    std::vector<std::string> range_values;
                    boost::split(range_values, range, boost::is_any_of("-"));
                    std::string start = range_values[0];
                    std::string end = range_values[1];
                    if ((!start.empty()) && (!end.empty()))
                    {
                        ssize_t start_byte = atol(start.c_str());
                        ssize_t end_byte = atol(start.c_str());
                        ssize_t content_length = (end_byte - start_byte) + 1;
                        if (content_length > 0)
                        {

                            std::ostringstream ss;
                            ss << Response::PARTIAL << "\n";
                            response.setContentLength(content_length);
                            ss << response.headers_str();
                            std::string response_buff(ss.str());
                            write(client_socket, response_buff.c_str(), response_buff.size());

                            off_t offset = start_byte;
                            int in_fd = open(details.object_path.c_str(), O_RDONLY);
                            ssize_t sent = sendfile(client_socket, in_fd, &offset, content_length);
                            close(in_fd);

                            if (sent != content_length)
                                BOOST_LOG_TRIVIAL(error) << "Error sending file contents";

                            return;
                        }
                    }
                }
            }

            // send the full content
            std::ostringstream ss;
            ss << Response::OK << "\n";
            ss << response.headers_str();
            std::string response_buff(ss.str());
            write(client_socket, response_buff.c_str(), response_buff.size());

            // send content
            off_t off = 0;
            int in_fd = open(details.object_path.c_str(), O_RDONLY);
            ssize_t sentbytes = sendfile(client_socket, in_fd, &off, file_details.st_size);
            close(in_fd);

            if (sentbytes != file_details.st_size)
                BOOST_LOG_TRIVIAL(error) << "Error sending file contents";
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

    void LIST_OBJECT(Request &request, int client_socket, PathDetails &details)
    {
        Response response{};

        std::ostringstream mesg;
        mesg << "<ListBucketResult>\n";
        mesg << "\t<Name>" << details.bucket << "</Name>\n";
        mesg << "\t<IsTruncated>false</IsTruncated>\n";

        for (const auto &entry : std::filesystem::directory_iterator(details.bucket_path))
        {

            Headers attributes;
            getAttributes(entry.path(), attributes);

            struct stat struct_stat;
            stat(entry.path().c_str(), &struct_stat);
            std::string last_mod{std::ctime(&(struct_stat.st_mtim).tv_sec)};
            last_mod.pop_back();

            mesg << "\t\t<Contents>\n";
            mesg << "\t\t<Key>" << entry.path().filename().c_str() << "</Key>\n";
            mesg << "\t\t<LastModified>" << last_mod << "</LastModified>\n";
            mesg << "<ETag>" << struct_stat.st_ino << "-" << struct_stat.st_size << "-" << struct_stat.st_mtim.tv_sec << "</ETag>";
            mesg << "<Size>" << struct_stat.st_size << "</Size>\n";
            mesg << "\t\t</Contents>\n";
        }

        mesg << "</ListBucketResult>\n";

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

    void LIST_BUCKET(Request &request, int client_socket, PathDetails &details)
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
            auto it = std::filesystem::directory_iterator(details.bucket_path);
            if (std::distance(std::filesystem::begin(it), std::filesystem::end(it)) > 0)
            {
                BOOST_LOG_TRIVIAL(error) << "Found File in Bucket";
                ss << Response::CONFLICT << "\n";
                ss << response.headers_str();
                std::string response_buff(ss.str());
                write(client_socket, response_buff.c_str(), response_buff.size());
                return;
            }
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
            if ((path_struct.st_mode & S_IFMT) == S_IFREG)
            {
                ss << Response::OK << "\n";
                response.addHeader("Content-Length", std::to_string(path_struct.st_size));
            }
            else
            {
                BadRequest(request, client_socket);
            }
        }
        else
        {
            ss << Response::NOT_FOUND << "\n";
        }

        getAttributes(details.object_path, response.headers());

        ss << response.headers_str();
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

    /**
     *  Bucket Details
     *
     */
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
    void setAttributes(const std::filesystem::path &path, PathDetails &details, Response &response, Request &request)
    {

        std::string mime_type = response.mime_type(details.key);
        if (setxattr(details.object_path.c_str(), PathDetails::XATT_MIME_TYPE, mime_type.c_str(), mime_type.size(), 0) < 0)
        {
            perror(PathDetails::XATT_MIME_TYPE);
            BOOST_LOG_TRIVIAL(error) << "FS Extended Attribute Not Set";
        }

        if (setxattr(details.object_path.c_str(), PathDetails::XATT_KEY_NAME, details.key.c_str(), details.key.size(), 0) < 0)
        {
            perror(PathDetails::XATT_KEY_NAME);
            BOOST_LOG_TRIVIAL(error) << "FS Extended Attribute Not Set";
        }

        CustomMetadata amzMetadata;
        Headers custom = request.getCustomHeaders<CustomMetadata>(amzMetadata);
        for (auto &h : custom)
        {
            std::string k = (h.first);
            k.erase(0, amzMetadata.prefix.size());
            std::string custom_name = std::string(PathDetails::XATT_PREFIX) + k;
            setxattr(details.object_path.c_str(), custom_name.c_str(), h.second.c_str(), h.second.size(), 0);
        }
    }

    /**
     *  Read the file system attrubutes back into the response Object
     *
     */
    void getAttributes(const std::filesystem::path &path, Headers &headers)
    {
        ssize_t sz = getxattr(path.c_str(), PathDetails::XATT_MIME_TYPE, NULL, 0);
        if (sz > 0)
        {
            char attr[sz + 1];
            sz = getxattr(path.c_str(), PathDetails::XATT_MIME_TYPE, attr, sz);
            attr[sz] = '\0';
            headers.emplace("Content-Type", std::string(attr));
        }

        ssize_t attr_len = listxattr(path.c_str(), NULL, 0);
        if (attr_len > 0)
        {
            char attr_buf[attr_len + 1];
            attr_len = listxattr(path.c_str(), attr_buf, attr_len);
            attr_buf[attr_len] = '\0';
            char *key = attr_buf;
            size_t keylen = 0;
            while (attr_len > 0)
            {
                ssize_t val_len = getxattr(path.c_str(), key, NULL, 0);
                if (val_len > 0)
                {
                    char attr_key_buf[val_len + 1];
                    val_len = getxattr(path.c_str(), key, attr_key_buf, val_len);
                    attr_key_buf[val_len] = 0;
                    std::string hkey = "x-amz-meta-" + std::string(key).erase(0, strlen(PathDetails::XATT_PREFIX));
                    headers.emplace(hkey, std::string(attr_key_buf));
                    keylen = strlen(key) + 1;
                    key += keylen;
                    attr_len -= keylen;
                }
            }
        }
    }

    /**
     *  Strip the fixed url parts from the full url
     *  to leave the bucket and key
     *
     *  Return object containing details of the request.
     */
    PathDetails getParts(Request &request)
    {
        std::list<std::string> path_segments = request.segments();

        path_segments.remove_if([this](std::string s)
                                { return count(m_path_parts.begin(), m_path_parts.end(), s); });

        PathDetails details(path_segments, getRootPath());

        return details;
    }

    /**
     *  Return 400 BAD REQUEST
     *
     */
    void BadRequest(Request &request, int client_socket)
    {
        std::ostringstream ss;
        ss << Response::BAD_REQUEST << "\n";
        std::string response_buff(ss.str());
        write(client_socket, response_buff.c_str(), response_buff.size());
    }

private:
    std::vector<std::string> m_path_parts;
};