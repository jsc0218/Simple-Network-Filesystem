#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
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
