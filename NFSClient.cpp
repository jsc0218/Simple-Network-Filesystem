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
#include <map>
#include <queue>

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
using SimpleNetworkFilesystem::Dirent;
using SimpleNetworkFilesystem::MkdirRequest;
using SimpleNetworkFilesystem::ErrnoReply;
using SimpleNetworkFilesystem::CreateRequest;
using SimpleNetworkFilesystem::FuseFileInfo;
using SimpleNetworkFilesystem::ReadReply;
using SimpleNetworkFilesystem::ReadRequest;
using SimpleNetworkFilesystem::WriteRequest;
using SimpleNetworkFilesystem::WriteOptRequest;
using SimpleNetworkFilesystem::WriteReply;
using SimpleNetworkFilesystem::RenameRequest;
using SimpleNetworkFilesystem::UtimensRequest;
using SimpleNetworkFilesystem::CommitRequest;
using SimpleNetworkFilesystem::CommitReply;

using namespace std;

/*=======================================================

    gRPC Connections to Server

=========================================================*/

class NFSClient {
    private:
    unique_ptr<NFS::Stub> stub;
    map<string, queue<WriteRequest>> writeRequests;
    mutex writeRequestsLock;

    public:
    NFSClient(shared_ptr<Channel> channel) : stub(NFS::NewStub(channel)) {}

    int getAttr( const string& path, Stat* stat ) {
        ClientContext context;
        Path pathMessage;
        pathMessage.set_path(path);
        Status status = stub->getattr(&context, pathMessage, stat);
        if (!status.ok()) {
            return -status.error_code();
        }
        return -stat->err();
    }

    int readdir( const string& path, vector<Dirent>& entries ) {
        ClientContext context;
        Path pathMessage;
        pathMessage.set_path(path);
        unique_ptr<ClientReader<Dirent>> reader(stub->readdir(&context, pathMessage));

        Dirent entry;
        while(reader->Read(&entry)){
            if (entry.err() != 0) {
                break;
            }
            entries.push_back(entry);
        }
        Status status = reader->Finish();
        if (!status.ok()) {
            return -status.error_code();
        }
        entries.pop_back();
        return -entry.err();
    }

    int rmdir( const string& path ) {
        ClientContext context;
        Path pathMessage;
        pathMessage.set_path(path);
        ErrnoReply response;
        Status status = stub->rmdir(&context, pathMessage, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        return -response.err();
    }

    int mkdir( const string& path, uint32_t mode ) {
        ClientContext context;
        MkdirRequest request;
        request.set_path(path);
        request.set_mode(mode);
        ErrnoReply response;
        Status status = stub->mkdir(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        return -response.err();
    }

    int create( const string& path, uint32_t mode, int32_t flags, uint64_t& fh ) {
        ClientContext context;
        CreateRequest request;
        request.set_path(path);
        request.set_mode(mode);
        request.set_flags(flags);
        FuseFileInfo response;
        Status status = stub->create(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        if (response.err() != 0) {
            return -response.err();
        }
        fh = response.fh();
        return 0;
    }

    int mkdnod( const string& path ) {
        ClientContext context;

    }

    int open( const string& path, int32_t flags, uint64_t& fileHandle ) {
        ClientContext context;
        FuseFileInfo request, response;
        request.set_path(path);
        request.set_flags(flags);
        Status status = stub->open(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        fileHandle = response.fh();
        return -response.err();
    }

    int read( const string& path, uint64_t count, int64_t offset, string& buf ) {
        ClientContext context;
        ReadRequest request;
        request.set_path(path);
        request.set_count(count);
        request.set_offset(offset);
        ReadReply response;
        Status status = stub->read(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        if (response.err() != 0) {
            return -response.err();
        }
        assert(response.bytes_read() >= 0);
        buf = response.buffer();
        return response.bytes_read();
    }

    int write( const string& path, const string& writeBuf, uint32_t count, int64_t offset ) {
        ClientContext context;
        WriteRequest request;
        request.set_path(path);
        request.set_buffer(writeBuf);
        request.set_count(count);
        request.set_offset(offset);
        WriteReply response;
        Status status = stub->write(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        if (response.err() != 0) {
            return -response.err();
        }
        assert(response.bytes_write() >= 0);
        return response.bytes_write();
    }

    int writeOptimized( const string& path, const string& writeBuf, uint32_t count, int64_t offset ) {
        ClientContext context;
        WriteOptRequest request;
        request.set_path(path);
        request.set_buffer(writeBuf);
        request.set_count(count);
        request.set_offset(offset);
        WriteReply response;
        Status status = stub->writeOptimized(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        if (response.err() != 0) {
            return -response.err();
        }
        assert(response.bytes_write() >= 0);
        writeRequests[path].push(request);
        return response.bytes_write();
    }

    int unlink( const string& path ) {
        ClientContext context;
        Path request;
        request.set_path(path);
        ErrnoReply response;
        Status status = stub->unlink(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        return -response.err();
    }

    int rename( const string& oldName, const string& newName ) {
        ClientContext context;
        RenameRequest request;
        request.set_from_path(oldName);
        request.set_to_path(newName);
        ErrnoReply response;
        Status status = stub->rename(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        return -response.err();
    }

    int utimens( const string& path, uint64_t accessedSec, uint64_t accessedNano, uint64_t modifiedSec, uint64_t modifiedNano ) {
        ClientContext context;
        UtimensRequest request;
        request.set_access_sec(accessedSec);
        request.set_access_nsec(accessedNano);
        request.set_modify_sec(modifiedSec);
        request.set_modify_nsec(modifiedNano);
        ErrnoReply response;
        Status status = stub->utimens(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        return -response.err();
    }

    int commitWrite( const string& path ) {
        ClientContext context;
        CommitRequest request;
        request.set_path(path);
        CommitReply response;
        Status status = stub->commitWrite(&context, request, &response);
        if (!status.ok()) {
            // If this fails, we retry transmitting the previous write requests
            //
            // We should also check if server crashed
            //
            // TODO
            //
        }
        writeRequests.erase(path);
        return 0;
    }
};

shared_ptr<NFSClient> nfsClient;


/*=======================================================

    Fuse operation handlers

=========================================================*/

static int handleGetattr( const char* path, struct stat* st ) {
    Stat stat;
    int status = nfsClient->getAttr(path, &stat);
    if (status == 0) {
        st->st_mode = stat.mode();
        st->st_dev = stat.dev();
        st->st_ino = stat.ino();
        st->st_nlink = stat.nlink();
        st->st_uid = stat.uid();
        st->st_gid = stat.gid();
        st->st_rdev = stat.rdev();
        st->st_blksize = stat.blksize();
        st->st_blocks = stat.blocks();
        st->st_size = stat.size();
        st->st_atim.tv_sec = stat.atime();
        st->st_mtim.tv_sec = stat.mtime();
        st->st_ctim.tv_sec = stat.ctime();
    }
    return status;
}

static int handleReaddir( const char* path, void* buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info* fi ) {
    vector<Dirent> entries;
    int status = nfsClient->readdir(path, entries);
    if (status == 0) {
        for (vector<Dirent>::iterator it = entries.begin(); it != entries.end(); ++it) {
            struct stat stat{};
            stat.st_ino = it->ino();
            stat.st_mode = (it->type().length() > 0 ? it->type()[0] : 0) << 12;
            filler(buf, it->name().c_str(), &stat, 0);
        }
    }
    return status;
}

static int handleRmdir( const char* path ) {
    return nfsClient->rmdir(path);
}

static int handleMkdir( const char* path, mode_t mode ) {
    return nfsClient->mkdir(path, mode);
}

static int handleCreate( const char* path, mode_t mode,
                         struct fuse_file_info* fi ) {
    return nfsClient->create(path, mode, fi->flags, fi->fh);
}

static int handleMknod( const char* path, mode_t mode, dev_t dev ) {
    return 0;
}

static int handleOpen( const char* path, struct fuse_file_info* fi) {
    uint64_t fileHandle;
    int status = nfsClient->open(path, fi->flags, fileHandle);
    if (status == 0) {
        fi->fh = fileHandle;
    }
    return status;
}

static int handleRead( const char* path, char* buf, size_t size, off_t offset,
                       struct fuse_file_info* fi) {
    string readBuffer;
    int status = nfsClient->read(path, size, offset, readBuffer);
    if (status >= 0) {
        memcpy(buf, readBuffer.c_str(), status);
    }
    return status;
}

static int handleWrite( const char* path, const char* buf, size_t size, off_t offset,
                        struct fuse_file_info* fi ) {
    string writeBuf = string(buf, size);
    return nfsClient->write(path, writeBuf, size, offset);
}

static int handleUnlink( const char* path ) {
    return nfsClient->unlink(path);
}

static int handleRename( const char* oldName, const char* newName ) {
    return nfsClient->rename(oldName, newName);
}

static int handleUtimens( const char* path, const struct timespec* tv ) {
    uint64_t accessedSec = tv[0].tv_sec, accessedNano = tv[0].tv_nsec;
    uint64_t modifiedSec = tv[1].tv_sec, modifiedNano = tv[1].tv_nsec;
    return nfsClient->utimens(path, accessedSec, accessedNano, modifiedSec, modifiedNano);
}

static int handleFsync( const char* path, int isDataSync, struct fuse_file_info* fi ) {
    return nfsClient->commitWrite(path);
}

static int handleFlush( const char* path, struct fuse_file_info* fi ) {
    return nfsClient->commitWrite(path);
}

static struct fsOperations : fuse_operations {
    fsOperations() {
        getattr = handleGetattr;
        readdir = handleReaddir;
        rmdir   = handleRmdir;
        mkdir   = handleMkdir;
        create  = handleCreate;
        //mknod   = handleMknod;
        open    = handleOpen;
        read    = handleRead;
        write   = handleWrite;
        unlink  = handleUnlink;
        rename  = handleRename;
        utimens = handleUtimens;
        fsync   = handleFsync;
        flush   = handleFlush;
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
