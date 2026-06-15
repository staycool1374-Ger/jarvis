#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <ipc.h>
#include <sys/stat.h>

#define VFS_OPEN     100
#define VFS_CLOSE    101
#define VFS_READ     102
#define VFS_WRITE    103
#define VFS_STAT     104
#define VFS_FSTAT    105
#define VFS_MOUNT    106
#define VFS_CHDIR    107
#define VFS_GETCWD   108
#define VFS_READDIR  109

struct VfsdMsg {
    uint64_t sender_id;
    uint64_t type;
    uint64_t arg0;
    uint64_t arg1;
    char     path[32];
};

struct VfsdReply {
    int64_t result;
    uint64_t data0;
    uint64_t data1;
    uint64_t data2;
    uint64_t data3;
};

static uint64_t g_my_id = 0;

static int vfsd_handle_open(struct VfsdMsg* msg, struct VfsdReply* reply) {
    int fd = open(msg->path, (int)msg->arg0);
    reply->result = fd >= 0 ? fd : -1;
    if (fd >= 0) close(fd);
    return 0;
}

static int vfsd_handle_close(struct VfsdMsg* msg, struct VfsdReply* reply) {
    int fd = (int)msg->arg0;
    reply->result = close(fd) >= 0 ? 0 : -1;
    return 0;
}

static int vfsd_handle_stat(struct VfsdMsg* msg, struct VfsdReply* reply) {
    struct stat st;
    int r = stat(msg->path, &st);
    reply->result = r >= 0 ? 0 : -1;
    if (r >= 0) {
        reply->data0 = st.st_size;
        reply->data1 = st.st_mode;
    }
    return 0;
}

static int vfsd_handle_fstat(struct VfsdMsg* msg, struct VfsdReply* reply) {
    int fd = (int)msg->arg0;
    struct stat st;
    int r = fstat(fd, &st);
    reply->result = r >= 0 ? 0 : -1;
    if (r >= 0) {
        reply->data0 = st.st_size;
        reply->data1 = st.st_mode;
    }
    return 0;
}

static int vfsd_handle_read(struct VfsdMsg* msg, struct VfsdReply* reply) {
    (void)msg;
    // Authorization-only: kernel does the actual read.
    // Future: validate that sender is allowed to read from fd msg->arg0.
    reply->result = 0;
    return 0;
}

static int vfsd_handle_write(struct VfsdMsg* msg, struct VfsdReply* reply) {
    (void)msg;
    // Authorization-only: kernel does the actual write.
    reply->result = 0;
    return 0;
}

static int vfsd_handle_mount(struct VfsdMsg* msg, struct VfsdReply* reply) {
    (void)msg;
    reply->result = -1;
    return 0;
}

static int vfsd_handle_chdir(struct VfsdMsg* msg, struct VfsdReply* reply) {
    struct stat st;
    int r = stat(msg->path, &st);
    reply->result = (r >= 0 && (st.st_mode & 0xF000) == S_IFDIR) ? 0 : -1;
    return 0;
}

static int vfsd_handle_getcwd(struct VfsdMsg* msg, struct VfsdReply* reply) {
    (void)msg;
    reply->result = -1;
    return 0;
}

static int vfsd_handle_readdir(struct VfsdMsg* msg, struct VfsdReply* reply) {
    (void)msg;
    reply->result = -1;
    return 0;
}

static int vfsd_dispatch(struct VfsdMsg* msg, struct VfsdReply* reply) {
    switch (msg->type) {
        case VFS_OPEN:    return vfsd_handle_open(msg, reply);
        case VFS_CLOSE:   return vfsd_handle_close(msg, reply);
        case VFS_READ:    return vfsd_handle_read(msg, reply);
        case VFS_WRITE:   return vfsd_handle_write(msg, reply);
        case VFS_STAT:    return vfsd_handle_stat(msg, reply);
        case VFS_FSTAT:   return vfsd_handle_fstat(msg, reply);
        case VFS_MOUNT:   return vfsd_handle_mount(msg, reply);
        case VFS_CHDIR:   return vfsd_handle_chdir(msg, reply);
        case VFS_GETCWD:  return vfsd_handle_getcwd(msg, reply);
        case VFS_READDIR: return vfsd_handle_readdir(msg, reply);
        default:
            reply->result = -1;
            return -1;
    }
}

int main(void) {
    g_my_id = (uint64_t)getpid();
    printf("[vfsd] VFS Daemon started (PID=%llu)\n", g_my_id);

    while (1) {
        struct VfsdMsg msg = {0};
        int r = ipc_recv(&msg, sizeof(msg));
        if (r < 0) continue;
        if ((unsigned long)r != msg.type) continue;

        struct VfsdReply reply = {0};
        vfsd_dispatch(&msg, &reply);

        ipc_send(msg.sender_id, 0, &reply, sizeof(reply), 0);
    }

    return 0;
}