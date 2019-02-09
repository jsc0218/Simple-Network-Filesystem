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
#include <vector>

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
using SimpleNetworkFilesystem::WriteReply;
using SimpleNetworkFilesystem::RenameRequest;
using SimpleNetworkFilesystem::UtimensRequest;
using SimpleNetworkFilesystem::CommitReply;
using SimpleNetworkFilesystem::CommitRequest;
using SimpleNetworkFilesystem::ReleaseRequest;

using namespace std;

struct ServerFile {
    uint64_t fh;
    uint64_t sessionId;
    string path;
    int32_t flags;
};

typedef UserFile uint64_t;

/*=======================================================

    gRPC Connections to Server

=========================================================*/

class NFSClient {
    private:
    unique_ptr<NFS::Stub> stub;

    map<UserFile, ServerFile> fileMap;

    uint64_t getNewUserFileHandle() {
        for (int i = 100; i < 1024; i++) {
            if (fileMap.find(i) == fileMap.end()) {
                return i;
            }
        }
        return 0;
    }

    int retryRead(uint64_t userFh, ReadReply* readResonse) {
        ClientContext context;
        FuseFileInfo request, response;

        const int timesToRetry = 100;
        for (int i = 0; i < timesToRetry; i++) {
            usleep(1000);
            request.set_path(path);
            request.set_flags(flags);
            Status status = stub->open(&context, request, &response);
            if (status.ok() && response.err() == 0) {
                fileMap[userFh].fh = response.fh();
                fileMap[userFh].sessionId = response.sessionid();

                ReadRequest readRequest;
                request.set_fh(serverFile.fh);
                request.set_sessionid(servreFile.sessionId)
                request.set_count(count);
                request.set_offset(offset);
                status = stub->read(&context, request, readResponse);

                if (status.ok() && response.err() == 0) {
                    break;
                }
            }
        }
        return 0;
    }


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
        if (response.err() == 0) {
            uint64_t serverFileHandle = response.fh();
            uint64_t serverSession = response.sessionid();
            uint64_t userFileHandle = getNewUserFileHandle();
            fileMap[userFileHandle] = { .fh = serverFileHandle, .sessionId = serverSession, .path = path };
            fh = userFileHandle;
        }
        return 0;
    }

    int open( const string& path, int32_t flags, uint64_t& fh ) {
        ClientContext context;
        FuseFileInfo request, response;
        request.set_path(path);
        request.set_flags(flags);
        Status status = stub->open(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        if (response.err() == 0) {
            uint64_t serverFileHandle = response.fh();
            uint64_t serverSession = response.sessionid();
            uint64_t userFileHandle = getNewUserFileHandle();
            fileMap[userFileHandle] = { .fh = serverFileHandle, .sessionId = serverSession, .path = path };
            fh = userFileHandle;
        }
        return -response.err();
    }

    int read( uint64_t userFh, uint64_t count, int64_t offset, string& buf ) {
        ClientContext context;

        ServerFile serverFile = fileMap[userFh];

        ReadRequest request;
        request.set_fh(serverFile.fh);
        request.set_sessionid(servreFile.sessionId)
        request.set_count(count);
        request.set_offset(offset);
        ReadReply response;

        Status status;
        do {
            status = stub->read(&context, request, &response);
        } while (!status.ok());

        if (response.err() == -1000000) {
            return retryRead(userFh);
        }
        if (response.err() != 0) {
            return -response.err();
        }
        assert(response.bytes_read() >= 0);
        buf = response.buffer();
        return response.bytes_read();
    }

    int write( uint64_t userFh, const string& writeBuf, uint32_t count, int64_t offset ) {
        ClientContext context;
        WriteRequest request;
        request.set_fh(fh);
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

    int commitWrite( uint64_t userFh ) {
        ClientContext context;
        CommitRequest request;
        request.set_fh(fh);
        CommitReply response;
        Status status = stub->commitWrite(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        return -response.err();
    }

    int release( uint64_t userFh ) {
        ClientContext context;
        ReleaseRequest request;
        request.set_fh(fh);
        ErrnoReply response;
        Status status = stub->release(&context, request, &response);
        if (!status.ok()) {
            return -status.error_code();
        }
        return -response.err();
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
    int status = nfsClient->read(fi->fh, size, offset, readBuffer);
    if (status >= 0) {
        memcpy(buf, readBuffer.c_str(), status);
    }
    return status;
}

static int handleWrite( const char* path, const char* buf, size_t size, off_t offset,
                        struct fuse_file_info* fi ) {
    string writeBuf = string(buf, size);
    return nfsClient->write(fi->fh, writeBuf, size, offset);
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

static int handleFsync( const char* path, int i, struct fuse_file_info* fi ) {
    int status = nfsClient->commitWrite(fi->fh);
    return status;
}

static int handleRelease( const char* path, struct fuse_file_info* fi ) {
    int status = nfsClient->release(fi->fh);
    return status;
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
        release = handleRelease;
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
