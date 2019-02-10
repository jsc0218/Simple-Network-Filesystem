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

const int SERVER_CRASHED = 1000000;

uint64_t sessionId = 0;

string serverMount = "/tmp/nfs";

class NFSServiceImpl final : public NFS::Service {
    string translatePath(const string& clientPath) {
        return serverMount + clientPath;
    }

    unordered_set<string> ignoreList = {"/.Trash", "/.Trash-1000", "/.xdg-volume-info", "/autorun.inf"};

    Status getattr(ServerContext* context, const Path* path, Stat* reply) override {
    	string clientPath = path->path();
    	if (ignoreList.find(clientPath) != ignoreList.end()) {
    		reply->set_err(0);
    		return Status::OK;
    	}
        string serverPath = translatePath(clientPath);
        struct stat st;
        int res = stat(serverPath.c_str(), &st);
        if (res == -1) {
        	cout << serverPath << endl;
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
    	           Dirents* dirents) override {
//    	the last entry of return result indicates the errno
        string serverPath = translatePath(path->path());
        Dirent dirent;
        vector<Dirent> dirs;
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
                dirs.push_back(dirent);
            }
            dirent.set_err(0);
            closedir(dp);
        }
        dirs.push_back(dirent);
        *(dirents->mutable_dirent()) = {dirs.begin(), dirs.end()};
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
            reply->set_sessionid(sessionId);
            reply->set_err(0);
        }
        return Status::OK;
    }

    Status read(ServerContext* context, const ReadRequest* request,
        		    ReadReply* reply) override {
        if (request->sessionid() != sessionId) {
            reply->set_err(SERVER_CRASHED);
            reply->set_newsessionid(sessionId);
            return Status::OK;
        }
        char* buf = new char[request->count()];
        int bytes_read = pread(request->fh(), buf, request->count(), request->offset());
        if (bytes_read == -1) {
            cout << "read errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_bytes_read(bytes_read);
            reply->set_buffer(buf);
            reply->set_err(0);
        }
        delete[] buf;
        return Status::OK;
    }

    Status write(ServerContext* context, const WriteRequest* request,
                     WriteReply* reply) override {
        if (request->sessionid() != sessionId) {
            reply->set_err(SERVER_CRASHED);
            reply->set_newsessionid(sessionId);
            return Status::OK;
        }
        int bytes_write = pwrite(request->fh(), request->buffer().c_str(), request->count(), request->offset());
        ::fsync(request->fh());
        if (bytes_write == -1) {
            cout << "write errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_bytes_write(bytes_write);
            reply->set_err(0);
        }
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
            reply->set_sessionid(sessionId);
            reply->set_err(0);
        }
        return Status::OK;
    }

    Status unlink(ServerContext* context, const Path* path,
                  ErrnoReply* reply) override {
    	string serverPath = translatePath(path->path());
        int res = ::unlink(serverPath.c_str());
        if (res == -1) {
            cout << "unlink errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_err(0);
        }
        return Status::OK;
    }

    Status mkdir(ServerContext* context, const MkdirRequest* request,
    		     ErrnoReply* reply) override {
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
    		     ErrnoReply* reply) override {
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

    Status rename(ServerContext* context, const RenameRequest* request,
                  ErrnoReply* reply) override {
     	string fromPath = translatePath(request->from_path());
     	string toPath = translatePath(request->to_path());
        int res = ::rename(fromPath.c_str(), toPath.c_str());
        if (res == -1) {
            cout << "rename errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_err(0);
        }
        return Status::OK;
    }

    Status utimens(ServerContext* context, const UtimensRequest* request,
                   ErrnoReply* reply) override {
        struct timespec ts[2];
        ts[0].tv_sec = request->access_sec();
        ts[0].tv_nsec = request->access_nsec();
        ts[1].tv_sec = request->modify_sec();
        ts[1].tv_nsec = request->modify_nsec();
        string serverPath = translatePath(request->path());
        int res = utimensat(AT_FDCWD, serverPath.c_str(), ts, AT_SYMLINK_NOFOLLOW);
        if (res == -1) {
            cout << "utimens errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_err(0);
        }
        return Status::OK;
    }

    Status commitWrite(ServerContext* context, const CommitRequest* request,
    	               CommitReply* reply) override {
        if (request->sessionid() != sessionId) {
            reply->set_err(SERVER_CRASHED);
            reply->set_newsessionid(sessionId);
            return Status::OK;
        }
        int res = fsync(request->fh());
        if (res == -1) {
            cout << "commitWrite errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_err(0);
        }
        return Status::OK;
    }

    Status release(ServerContext* context, const ReleaseRequest* request,
                     CommitReply* reply) override {
        if (request->sessionid() != sessionId) {
            reply->set_err(SERVER_CRASHED);
            reply->set_newsessionid(sessionId);
            return Status::OK;
        }
        int res = ::close(request->fh());
        if (res == -1) {
            cout << "release errno:" << errno << endl;
            reply->set_err(errno);
        } else {
            reply->set_err(0);
        }
        return Status::OK;
    }
};

void RunServer() {
    sessionId = time(0);
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
