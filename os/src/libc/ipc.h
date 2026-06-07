#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IPC_NONBLOCK 1

int ipc_send(unsigned long dest, unsigned long type, const void* data, unsigned long size, unsigned long flags);
int ipc_recv(void* buf, unsigned long max_size);
int ipc_send_sync(unsigned long dest, unsigned long type, const void* data, unsigned long size, void* reply_buf);

int ipc_notify(unsigned long task_id, unsigned long value);
int ipc_notify_wait(unsigned long* value);

int ipc_event_set(unsigned long task_id, unsigned long bits);
int ipc_event_wait(unsigned long bits, int clear_on_exit);

#ifdef __cplusplus
}
#endif
