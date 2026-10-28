// SPDX-License-Identifier: MIT
//
// EXPERIMENTAL v0.6 Linux ublk recovery adapter for the AMD GPU
// backing store. Requires libublksrv's targets/include/ublksrv_tgt.h build
// environment. DO NOT RUN on a normal host; NEVER use as active Linux swap.
// This adapter is not compiled by the regular v0.6 CMake build.
//
// Kernel block IO -> libublksrv callback -> checked Nopen OpenCL backing store
// Unlike old NBD global-swap this backend has no activation mechanism here.
// It is ephemeral volatile storage, GPU resets permanently lose backing data.
// No claim is made that a userspace service can make reclaim progress under OOM.
//
#include "ublksrv_tgt.h"
extern "C" {
#include "nopen_recovery.h"
}
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <unistd.h>

namespace {
constexpr unsigned kMiB = 1024u*1024u;
constexpr unsigned kCapacityMiB = 64u;
constexpr unsigned kPageSize = 4096u;
struct NopenTarget {
    nopen_recovery* backing=nullptr;
};
bool isolated_virtual_machine() {
    FILE* f=fopen("/sys/class/dmi/id/product_name","r");
    if(!f) return false;
    char value[256]={};
    bool ok=fgets(value,sizeof value,f)!=nullptr;
    fclose(f);
    if(!ok)return false;
    return strstr(value,"QEMU") || strstr(value,"KVM") ||
           strstr(value,"Virtual Machine") || strstr(value,"VMware") ||
           strstr(value,"Bochs");
}
int open_target(ublksrv_dev* dev,int,int,char**) {
    const char* gate=getenv("NOPEN_UBLK_ISOLATED_LAB");
    if(!gate || strcmp(gate,"EXPERIMENTAL_BLOCK_IO_NOT_SWAP")!=0 ||
       !isolated_virtual_machine()) {
        fprintf(stderr,"NOPEN_UBLK_REFUSED isolated virtual machine gate absent\n");
        return -EPERM;
    }
    const ublksrv_ctrl_dev* ctl=ublksrv_get_ctrl_dev(dev);
    const ublksrv_ctrl_dev_info* info=ublksrv_ctrl_get_dev_info(ctl);
    if(!info || ublksrv_is_recovering(ctl))return -EOPNOTSUPP;
    // Only a basic volatile block device, not a swap-safe persistent store.
    ublk_params params={};
    params.types=UBLK_PARAM_TYPE_BASIC;
    params.basic.attrs=UBLK_ATTR_VOLATILE_CACHE;
    params.basic.logical_bs_shift=12;  // 4096 bytes
    params.basic.physical_bs_shift=12;
    params.basic.io_opt_shift=12;
    params.basic.io_min_shift=12;
    params.basic.max_sectors=NOPEN_BLK_MAX_BYTES>>9;
    params.basic.dev_sectors=(uint64_t)kCapacityMiB*kMiB>>9;
    if(info->max_io_buf_bytes<NOPEN_BLK_MAX_BYTES)return -EINVAL;
    // No discard, flush durability or recovery advertised.
    ublksrv_tgt_base_json json={};
    strncpy(json.name,"nopen-recovery-lab",sizeof(json.name)-1);
    json.dev_size=(uint64_t)kCapacityMiB*kMiB;
    ublk_json_write_dev_info(ctl);
    ublk_json_write_target_base(ctl,&json);
    ublk_json_write_params(ctl,&params);

    NopenTarget *state=new(std::nothrow) NopenTarget();
    if(!state)return -ENOMEM;
    // NO_MIRROR mode uses real GPU memory economy but fails I/O after loss.
    // An explicit lab-only CPU mirror sacrifices the RAM-saving property
    // in exchange for correctness of an in-process fallback.
    const char* desired=getenv("NOPEN_UBLK_RECOVERY_MODE");
    const nopen_recovery_mode mode=desired &&
        strcmp(desired,"full-cpu-mirror")==0
        ?NOPEN_RECOVERY_HOST_MIRROR:NOPEN_RECOVERY_NO_MIRROR;
    const size_t budget=mode==NOPEN_RECOVERY_HOST_MIRROR
        ?(size_t)72*kMiB:(size_t)1*kMiB;
    // Explicit mock enables deterministic kernel ublk lifecycle tests in an
    // isolated VM without requiring GPU passthrough. No silent fallback.
    // The DMI and environment opt-in above still reject the desktop host.
    const char* requested_backend=getenv("NOPEN_UBLK_BACKEND");
    const nopen_backend backend=requested_backend &&
        strcmp(requested_backend,"mock")==0?NOPEN_STORE_MOCK:NOPEN_STORE_OPENCL;
    if(requested_backend && strcmp(requested_backend,"mock")!=0 &&
       strcmp(requested_backend,"opencl")!=0) {
        delete state;
        return -EINVAL;
    }
    int rc=nopen_recovery_open_bounded(&state->backing,backend,
                                      kCapacityMiB,mode,budget);
    if(rc!=0) {
        delete state;
        return rc;
    }
    dev->tgt.tgt_data=state;
    dev->tgt.dev_size=(uint64_t)kCapacityMiB*kMiB;
    dev->tgt.tgt_ring_depth=info->queue_depth;
    dev->tgt.nr_fds=0;
    ublksrv_tgt_set_io_data_size(&dev->tgt);
    return 0;
}
void close_target(const ublksrv_dev* dev) {
    auto* state=static_cast<NopenTarget*>(dev->tgt.tgt_data);
    if(state) {
        nopen_recovery_close(state->backing);
        delete state;
    }
}
int on_io(const ublksrv_queue* q,const ublk_io_data* io) {
    if(!q || !io || !io->iod) return -EINVAL;
    auto* state=static_cast<NopenTarget*>(q->dev->tgt.tgt_data);
    if(!state || !state->backing) {
        ublksrv_complete_io(q,io->tag,-EIO);
        return 0;
    }
    const auto* iod=io->iod;
    unsigned op=ublksrv_get_op(iod);
    uint64_t offset=static_cast<uint64_t>(iod->start_sector)*512u;
    size_t bytes=static_cast<size_t>(iod->nr_sectors)*512u;
    void* buf=ublksrv_queue_get_io_buf(q,io->tag);
    int rc=-EOPNOTSUPP;
    if(buf && bytes && bytes<=NOPEN_BLK_MAX_BYTES) {
        switch(op) {
            case UBLK_IO_OP_READ:
                rc=nopen_recovery_read(state->backing,offset,buf,bytes);
                break;
            case UBLK_IO_OP_WRITE:
                rc=nopen_recovery_write(state->backing,offset,buf,bytes);
                break;
            // FLUSH is deliberately rejected; this volatile GPU memory has no
            // device-reset durability or stable secondary authoritative copy.
            default:
                rc=-EOPNOTSUPP;
        }
    }
    // A checked successful completion returns number of transferred bytes.
    ublksrv_complete_io(q,io->tag,rc==0?static_cast<int>(bytes):rc);
    return 0;
}
ublksrv_tgt_type target={
    .handle_io_async=on_io,
    .init_tgt=open_target,
    .deinit_tgt=close_target,
    .name="nopen-recovery-lab"
};
}
int main(int argc,char** argv) {
    return ublksrv_main(&target,argc,argv);
}
