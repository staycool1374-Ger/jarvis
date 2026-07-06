#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <ipc.h>

#define IOCD_REGISTER_DEVICE 200
#define IOCD_IRQ_EVENT       201
#define IOCD_MMIO_MAP        202
#define IOCD_MMIO_UNMAP      203
#define IOCD_KEYBOARD_READ   204
#define IOCD_SERIAL_READ     205
#define IOCD_SERIAL_WRITE    206

#define MAX_DEVICES 16

struct IocdMsg {
    uint64_t type;
    uint64_t args[7];
};

struct IocdReply {
    int64_t result;
    uint64_t data[4];
};

struct DeviceEntry {
    uint64_t pid;
    char     name[32];
    uint32_t irq_line;
};

static uint64_t g_my_id = 0;
static struct DeviceEntry g_devices[MAX_DEVICES];
static size_t g_device_count = 0;

static int iocd_handle_register_device(struct IocdMsg* msg, struct IocdReply* reply) {
    if (g_device_count >= MAX_DEVICES) {
        reply->result = -1;
        return 0;
    }
    struct DeviceEntry* dev = &g_devices[g_device_count++];
    dev->pid = msg->args[0];
    dev->irq_line = (uint32_t)msg->args[1];
    size_t i = 0;
    const char* src = (const char*)&msg->args[2];
    while (src[i] && i < sizeof(dev->name) - 1) {
        dev->name[i] = src[i];
        ++i;
    }
    dev->name[i] = '\0';
    reply->result = 0;
    return 0;
}

static int iocd_handle_irq_event(struct IocdMsg* msg, struct IocdReply* reply) {
    uint32_t irq = (uint32_t)msg->args[0];
    for (size_t i = 0; i < g_device_count; ++i) {
        if (g_devices[i].irq_line == irq) {
            struct IocdMsg fwd;
            fwd.type = IOCD_IRQ_EVENT;
            fwd.args[0] = irq;
            fwd.args[1] = msg->args[1];
            ipc_send(g_devices[i].pid, IOCD_IRQ_EVENT, &fwd, sizeof(fwd), 0);
            reply->result = 0;
            return 0;
        }
    }
    reply->result = -1;
    return 0;
}

static int iocd_handle_mmio_map(struct IocdMsg* msg, struct IocdReply* reply) {
    (void)msg;
    reply->result = -1;
    return 0;
}

static int iocd_handle_mmio_unmap(struct IocdMsg* msg, struct IocdReply* reply) {
    (void)msg;
    reply->result = -1;
    return 0;
}

static int iocd_handle_keyboard_read(struct IocdMsg* msg, struct IocdReply* reply) {
    (void)msg;
    reply->result = -1;
    return 0;
}

static int iocd_handle_serial_read(struct IocdMsg* msg, struct IocdReply* reply) {
    (void)msg;
    reply->result = -1;
    return 0;
}

static int iocd_handle_serial_write(struct IocdMsg* msg, struct IocdReply* reply) {
    (void)msg;
    reply->result = -1;
    return 0;
}

static int iocd_dispatch(struct IocdMsg* msg, struct IocdReply* reply) {
    switch (msg->type) {
        case IOCD_REGISTER_DEVICE: return iocd_handle_register_device(msg, reply);
        case IOCD_IRQ_EVENT:       return iocd_handle_irq_event(msg, reply);
        case IOCD_MMIO_MAP:        return iocd_handle_mmio_map(msg, reply);
        case IOCD_MMIO_UNMAP:      return iocd_handle_mmio_unmap(msg, reply);
        case IOCD_KEYBOARD_READ:   return iocd_handle_keyboard_read(msg, reply);
        case IOCD_SERIAL_READ:     return iocd_handle_serial_read(msg, reply);
        case IOCD_SERIAL_WRITE:    return iocd_handle_serial_write(msg, reply);
        default:
            reply->result = -1;
            return -1;
    }
}

int main(void) {
    g_my_id = (uint64_t)getpid();
    printf("[iocd] I/O Controller Daemon started (PID=%llu)\n", g_my_id);

    // Signal init (PID 1) that this daemon is ready
#define MSG_DAEMON_READY 0xF0000001
    ipc_send(1, MSG_DAEMON_READY, NULL, 0, 0);

    while (1) {
        struct IocdMsg msg;
        int r = ipc_recv(&msg, sizeof(msg));
        if (r < 0) continue;
        if ((unsigned long)r != msg.type) continue;

        struct IocdReply reply = {0};
        iocd_dispatch(&msg, &reply);

        ipc_send(msg.args[0], 0, &reply, sizeof(reply), 0);
    }

    return 0;
}
