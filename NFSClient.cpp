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
#include <NFS.grpc.pb.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using SimpleNetworkFilesystem::Path;
using SimpleNetworkFilesystem::Stat;
using SimpleNetworkFilesystem::NFS;

#include "NFS.grpc.pb.h"

using namespace std;

/*=======================================================

    Fuse operation handlers

=========================================================*/

static int handle_getattr( const char* path, struct stat* st ) {
    string pathStr = path;

    if (pathStr == "/") {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }
    else {
        Stat stat;
        int status = nfs_client->getAttr(pathStr, &stat);
        if (status == 0 && stat.err() == 0) {
            st->st_mode = stat.mode() | 0777;
            st->st_dev = stat.dev();
            st->st_ino = stat.ino();
            st->st_nlink = stat.nlink();
            st->st_uid = getuid();
            st->st_gid = getpid();
            st->st_rdev = stat.rdev();
            st->st_blksize = stat.blksize();
            st->st_blocks = stat.blocks();
            st->st_atim.tv_sec = stat.atime();
            st->st_mtim.tv_sec = stat.mtime();
            st->st_ctim.tv_sec = stat.ctime();
            return 0;
        }
    }
    return -ENOENT;
}

static int handle_readdir( const char* path, void* buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info* fi ) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    //filler(buf, "blah", NULL, 0);
    //filler(buf, "asdf", NULL, 0);
    return 0;
}

static int handle_rmdir( const char* path ) {
    return 0;
}

static int handle_open( const char* path, struct fuse_file_info* fi) {
    return 0;
}

static int handle_read( const char* path, char* buf, size_t size, off_t offset,
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

static int handle_write( const char* path, const char* buf, size_t size, off_t offset,
                         struct fuse_file_info* fi ) {
    return 0;
}

static int handle_unlink( const char* path ) {
    return 0;
}

static int handle_create( const char* path, mode_t mode,
                          struct fuse_file_info* fi ) {
    return 0;
}

static struct fs_operations : fuse_operations {
    fs_operations() {
        getattr  = handle_getattr;
        
        // Directory operations
        readdir  = handle_readdir;
        rmdir    = handle_rmdir;
        
        // File operations
        read     = handle_read;
        write    = handle_write;
        open     = handle_open;
        create   = handle_create;
        unlink   = handle_unlink;   
    }
} fs_ops;


/*=======================================================

    gRPC Connections to Server

=========================================================*/

class NFSClient {

    private:

    unique_ptr<NFS::Stub> stub_;

    public:

    NFSClient(shared_ptr<Channel> chan) : stub_(NFS::NewStub(chan)) {
    }

    int getAttr( const string& path, Stat* stat ) {
        Path path_message;
        path_message.set_path(path);
        ClientContext context;
        Status status = stub_->getattr(&context, path_message, stat);
        if (!status.ok()) {
            return -status.error_code;
        }
        return 0;
    }
};

shared_ptr<NFSClient> nfs_client;

int main(int argc, char** argv) {

    // Parse the arg for the address to the remote filesystem, and the
    // local dir we wish to mount on
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    fuse_opt_add_arg(&args, argv[0]);
    string remote_mount, local_mount, port = "8080";
    string remote_address, remote_dir;
    int c;
    while ((c = getopt(argc, argv, "r:l:p:")) != -1) {
        switch (c) {
            case 'r':
                remote_mount.assign(optarg);
                break;
            case 'l':
                local_mount.assign(optarg);
                fuse_opt_add_arg(&args, local_mount.c_str());
                break;
            case 'p':
                port.assign(optarg);
                break;
        }
    }

    replace(remote_mount.begin(), remote_mount.end(), ':', ' ');
    cout << remote_mount << endl;
    stringstream ss(remote_mount);

    if ( remote_mount.size() == 0 || local_mount.size() == 0 ||
         !(ss >> remote_address && ss >> remote_dir) ) {
        cout << remote_address << "    " << remote_dir;
        cerr << "usage: " << argv[0] << " -r remote_address:remote_dir [-p port] -l local_mountpoint\n";
        return 1;
    }

    remote_address += ":" + port;
    cout << "Mounting to " << remote_dir << " at " << remote_address << endl;

    shared_ptr<Channel> channel = grpc::CreateChannel(remote_address,
        grpc::InsecureChannelCredentials());
    nfs_client.reset(new NFSClient(channel));

    return fuse_main(args.argc, args.argv, &fs_ops, NULL);
}
