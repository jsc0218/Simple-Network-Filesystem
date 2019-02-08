// Some macros to make fuse work properly
#define FUSE_USE_VERSION 29

#include <fuse.h>
#include <string.h>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include "NFS.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using SimpleNetworkFilesystem::Path;
using SimpleNetworkFilesystem::Stat;
using SimpleNetworkFilesystem::NFS;

using namespace std;

/*=======================================================

    gRPC Connections to Server

=========================================================*/

class NFSClient {
    private:
    unique_ptr<NFS::Stub> stub;

    public:
    NFSClient(shared_ptr<Channel> channel) : stub(NFS::NewStub(channel)) {}

    int getAttr( const string& path, Stat* stat ) {
        Path pathMessage;
        pathMessage.set_path(path);
        ClientContext context;
        Status status = stub->getattr(&context, pathMessage, stat);
        if (!status.ok()) {
            return -(status.error_code());
        }
        return 0;
    }

    int readdir() {
        return 0;
    }

    int rmdir( const string& path ) {
        return 0;
    }

    int mkdir() {
        return 0;
    }

    int create() {
        return 0;
    }

    int mkdnod() {
        return 0;
    }

    int open() {
        return 0;
    }

    int read() {
        return 0;
    }

    int write() {
        return 0;
    }

    int unlink( const string& path ) {
        return 0;
    }

    int rename( const string& oldName , const string& newName ) {
        return 0;
    }

    int utimens( const string& path, uint64_t seconds, uint64_t nanoseconds ) {
        return 0;
    }
};

shared_ptr<NFSClient> nfsClient;


/*=======================================================

    Fuse operation handlers

=========================================================*/

static int handleGetattr( const char* path, struct stat* st ) {
    string pathStr(path);
    Stat stat;
    int status = nfsClient->getAttr(pathStr, &stat);
    if (status == 0 && stat.err() == 0) {
        st->st_mode = stat.mode();
        st->st_dev = stat.dev();
        st->st_ino = stat.ino();
        st->st_nlink = stat.nlink();
        st->st_uid = getuid();
        st->st_gid = getegid();
        st->st_rdev = stat.rdev();
        st->st_blksize = stat.blksize();
        st->st_blocks = stat.blocks();
        st->st_size = stat.size();
        st->st_atim.tv_sec = stat.atime();
        st->st_mtim.tv_sec = stat.mtime();
        st->st_ctim.tv_sec = stat.ctime();
        return 0;
    }
    return -ENOENT;
}

static int handleReaddir( const char* path, void* buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info* fi ) {

    int status = nfsClient->readdir();
    if (status != 0) {

    }





    return 0;
}

static int handleRmdir( const char* path ) {
    return 0;
}

static int handleMkdir( const char* path, mode_t mode ) {
    return 0;
}

static int handleCreate( const char* path, mode_t mode,
                         struct fuse_file_info* fi ) {
    return 0;
}

static int handleMknod( const char* path, mode_t mode, dev_t dev ) {
    return 0;
}

static int handleOpen( const char* path, struct fuse_file_info* fi) {
    return 0;
}

static int handleRead( const char* path, char* buf, size_t size, off_t offset,
                       struct fuse_file_info* fi) {
    
    const char* contents = "asdf";
    
    if (strcmp(path, "/blah") == 0) {
        size_t len = strlen(contents);
        if (offset >= len) {
            return 0;
        }
        else if (offset + size > len) {
            memcpy(buf, contents + offset, len - offset);
            return len - offset;
        }
        memcpy(buf, contents + offset, size );
        return size;
    }

    return -ENOENT;
}

static int handleWrite( const char* path, const char* buf, size_t size, off_t offset,
                        struct fuse_file_info* fi ) {
    return 0;
}

static int handleUnlink( const char* path ) {
    string pathStr(path);
    int status = nfsClient->unlink(pathStr);
    if (status != 0) {

    }
    return 0;
}

static int handleRename( const char* oldName, const char* newName ) {
    string oldPathStr(oldName), newPathStr(newName);
    int status = nfsClient->rename(oldPathStr, newPathStr);
    if (status != 0) {

    }
    return 0;
}

static int handleUtimens( const char* path, const struct timespec* tv ) {
    string pathStr(path);
    uint64_t seconds = tv->tv_sec, nanoseconds = tv->tv_nsec;
    int status = nfsClient->utimens(pathStr, seconds, nanoseconds);
    if (status != 0) {

    }
    return 0;
}

static struct fsOperations : fuse_operations {
    fsOperations() {
        getattr = handleGetattr;
        readdir = handleReaddir;
        rmdir   = handleRmdir;
        mkdir   = handleMkdir;
        create  = handleCreate;
        mknod   = handleMknod;
        open    = handleOpen;
        read    = handleRead;
        write   = handleWrite;
        unlink  = handleUnlink;
        rename  = handleRename;
        utimens = handleUtimens;
    }
} fsOps;

/*=======================================================

    Main

=========================================================*/

int main(int argc, char** argv) {

    // Parse the arg for the address to the remote filesystem, and the
    // local dir we wish to mount on
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    fuse_opt_add_arg(&args, argv[0]);
    fuse_opt_add_arg(&args, "-f"); // Run client in the foreground, to prevent a weird gRPC race condition
    string remoteMount, localMount, port = "8080";
    string remoteAddress, remoteDir;
    int c;
    while ((c = getopt(argc, argv, "r:l:p:")) != -1) {
        switch (c) {
            case 'r':
                remoteMount.assign(optarg);
                break;
            case 'l':
                localMount.assign(optarg);
                fuse_opt_add_arg(&args, localMount.c_str());
                break;
            case 'p':
                port.assign(optarg);
                break;
        }
    }

    replace(remoteMount.begin(), remoteMount.end(), ':', ' ');
    cout << remoteMount << endl;
    stringstream ss(remoteMount);

    if ( remoteMount.size() == 0 || localMount.size() == 0 ||
         !(ss >> remoteAddress && ss >> remoteDir) ) {
        cout << remoteAddress << "    " << remoteDir;
        cerr << "usage: " << argv[0] << " -r remote_address:remote_dir [-p port] -l local_mountpoint\n";
        return 1;
    }

    remoteAddress += ":" + port;
    cout << "Mounting to " << remoteDir << " at " << remoteAddress << endl;

    shared_ptr<Channel> channel = grpc::CreateChannel(remoteAddress, grpc::InsecureChannelCredentials());
    nfsClient.reset(new NFSClient(channel));

    return fuse_main(args.argc, args.argv, &fsOps, NULL);
}
