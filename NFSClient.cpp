// Some macros to make fuse work properly
#define FUSE_USE_VERSION 29
#define _FILE_OFFSET_BITS 64

#include <fuse.h>
#include <string.h>
#include <getopt.h>
#include <iostream>

using namespace std;

/*=======================================================

    Fuse operation handlers

=========================================================*/

static int handle_getattr( const char* path, struct stat* st ) {
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }
    else {
        st->st_mode = S_IFREG | 0777;
        st->st_nlink = 1;
        st->st_size = 4;
        return 0;
    }
    return -ENOENT;
}

static int handle_readdir( const char* path, void* buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info* fi ) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, "blah", NULL, 0);
    filler(buf, "asdf", NULL, 0);
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

// TODO

int main(int argc, char** argv) {

    // Parse the arg for the address to the remote filesystem, and the
    // local dir we wish to mount on
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    fuse_opt_add_arg(&args, argv[0]);
    char* remote_mount = NULL, * local_mount = NULL;
    int c;
    while ((c = getopt(argc, argv, "r:l:")) != -1) {
        switch (c) {
            case 'r':
                remote_mount = strdup(optarg);
                break;
            case 'l':
                local_mount = strdup(optarg);
                fuse_opt_add_arg(&args, local_mount);
                break;
        }
    }
    if (remote_mount == NULL || local_mount == NULL) {
        cerr << "usage: " << argv[0] << " -r remote_address -l local_mountpoint\n";
        return 1;
    }
    
    return fuse_main(args.argc, args.argv, &fs_ops, NULL);
}
