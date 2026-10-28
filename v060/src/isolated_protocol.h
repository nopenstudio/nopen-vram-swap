#ifndef NOPEN_ISOLATED_PROTOCOL_H
#define NOPEN_ISOLATED_PROTOCOL_H
#include <stdint.h>
#define NOPEN_IPC_MAGIC UINT32_C(0x4e363049)
enum { NOPEN_IPC_READY=0, NOPEN_IPC_READ=1, NOPEN_IPC_WRITE=2,
       NOPEN_IPC_HANG_TEST_ONLY=3 };
typedef struct {
    uint32_t magic;
    uint32_t op;
    uint64_t offset;
    uint32_t bytes;
    int32_t status;
    uint64_t checksum;
} nopen_ipc_msg;
#endif
