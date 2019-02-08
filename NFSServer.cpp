#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <grpcpp/grpcpp.h>
#include "NFS.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerWriter;
using namespace SimpleNetworkFilesystem;
using namespace std;

class NFSServiceImpl final : public NFS::Service {
    string translatePath(const string& clientPath) {
        return "/tmp/nfs" + clientPath;
    }

    Status getattr(ServerContext* context, const Path* path, Stat* reply) override {
        string serverPath = translatePath(path->path());
        struct stat st;
        int res = stat(serverPath.c_str(), &st);
        if (res == -1) {
            cout << "getattr errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_dev(st.st_dev);
            reply->set_ino(st.st_ino);
            reply->set_nlink(st.st_nlink);
            reply->set_mode(st.st_mode);
            reply->set_uid(st.st_uid);
            reply->set_gid(st.st_gid);
            reply->set_rdev(st.st_rdev);
            reply->set_size(st.st_size);
            reply->set_blksize(st.st_blksize);
            reply->set_blocks(st.st_blocks);
            reply->set_atime(st.st_atim.tv_sec);
            reply->set_mtime(st.st_mtim.tv_sec);
            reply->set_ctime(st.st_ctim.tv_sec);
            reply->set_err(0);
        }
        return Status::OK;
    }

    Status readdir(ServerContext* context, const Path* path,
                   ServerWriter<Dirent>* writer) override {
    	// the last entry of return result indicates the errno
        string serverPath = translatePath(path->path());
        Dirent dirent;
        DIR* dp = opendir(serverPath.c_str());
        if (dp == nullptr) {
            cout << "readdir errno:" << errno << endl;
            dirent.set_err(errno);
        } else {
            struct dirent* de;
            while (de = ::readdir(dp)) {
            	dirent.set_ino(de->d_ino);
            	dirent.set_off(de->d_off);
            	dirent.set_reclen(de->d_reclen);
            	dirent.set_type(string(1, de->d_type));
            	dirent.set_name(de->d_name);
                writer->Write(dirent);
            }
            dirent.set_err(0);
        }
    	closedir(dp);
    	writer->Write(dirent);
        return Status::OK;
    }

    Status open(ServerContext* context, const FuseFileInfo* request,
                FuseFileInfo* reply) override {
    	// where to get writepage and lock_owner?
        string serverPath = translatePath(request->path());
        int fh = ::open(serverPath.c_str(), request->flags());
        if (fh == -1) {
            cout << "open errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_fh(fh);
            reply->set_err(0);
        }
        close(fh);
        return Status::OK;
    }

    Status read(ServerContext* context, const ReadRequest* request,
    		    ReadReply* reply) override {
        char* buf = new char[request->count()];
        string serverPath = translatePath(request->path());
        int fh = ::open(serverPath.c_str(), O_RDONLY);
        if (fh == -1) {
            cout << "read errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            int bytes_read = pread(fh, buf, request->count(), request->offset());
            if (bytes_read == -1) {
            	cout << "read errno:" << errno << endl;
                reply->set_err(errno);
            } else {
                reply->set_bytes_read(bytes_read);
                reply->set_buffer(buf);
                reply->set_err(0);
            }
        }
        close(fh);
        delete[] buf;

        return Status::OK;
    }

    Status write(ServerContext* context, const WriteRequest* request,
                 WriteReply* reply) override {
        string serverPath = translatePath(request->path());
        int fh = ::open(serverPath.c_str(), O_WRONLY);
        if (fh == -1) {
        	cout << "write errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            int bytes_write = pwrite(fh, request->buffer().c_str(), request->count(), request->offset());
            fsync(fh);
            if (bytes_write == -1) {
                cout << "write errno:" << errno << endl;
                reply->set_err(errno);
            } else {
                reply->set_bytes_write(bytes_write);
                reply->set_err(0);
            }
        }
        close(fh);

        return Status::OK;
    }

    Status create(ServerContext* context, const CreateRequest* request,
    		      FuseFileInfo* reply) override {
    	string serverPath = translatePath(request->path());
        int fh = ::open(serverPath.c_str(), request->flags(), request->mode());
        if (fh == -1) {
            cout << "create errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_fh(fh);
            reply->set_err(0);
        }
        close(fh);
        return Status::OK;
    }

    Status mkdir(ServerContext* context, const MkdirRequest* request,
    		     DirReply* reply) override {
        string serverPath = translatePath(request->path());
        int res = ::mkdir(serverPath.c_str(), request->mode());
        if (res == -1) {
            cout << "mkdir errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_err(0);
        }
        return Status::OK;
    }

    Status rmdir(ServerContext* context, const Path* path,
    	         DirReply* reply) override {
        string serverPath = translatePath(path->path());
        int res = ::rmdir(serverPath.c_str());
        if (res == -1) {
            cout << "rmdir errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_err(0);
        }
        return Status::OK;
    }
};

void RunServer() {
    string server_address("127.0.0.1:8080");
    NFSServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    unique_ptr<Server> server(builder.BuildAndStart());
    cout << "Server listening on " << server_address << endl;

    server->Wait();
}

int main() {
    RunServer();
    return 0;
}
