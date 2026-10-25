/* Nopen VRAM-SWAP v0.10.0-alpha.1 - Vulkan BAR/VRAM probe.
 * Copyright (c) Nopen Studio contributors. SPDX-License-Identifier: MIT.
 * No compute backend, no block device, no swap, no privilege escalation.
 */
#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>

#define MIB (1024ULL * 1024ULL)
#define PAGE 4096ULL
#define MAX_SAMPLES 512U

static const char *vk_name(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        default: return "VK_ERROR_OTHER";
    }
}

static uint64_t monotonic_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static uint8_t pattern(uint64_t page_idx, unsigned off, unsigned pass) {
    return (uint8_t)mix64(page_idx * UINT64_C(0x9e3779b97f4a7c15)
                         ^ (uint64_t)off ^ ((uint64_t)pass << 48));
}
static void usage(const char *arg0) {
    printf("Nopen Vulkan VRAM probe v0.10.0-alpha.1 (NO SWAP / NO NBD)\n");
    printf("Usage: %s [--list] [--mib 64] [--gpu-index N] [--samples 256]\n", arg0);
    puts("--mib is limited to 2048 MiB and defaults to 64 MiB. No automatic GPU RAM fallback.");
}
static int parse_u32(const char *s, uint32_t *out) {
    if (!s || !*s || *s == '-') return -1;
    errno = 0; char *end = NULL; unsigned long v = strtoul(s, &end, 10);
    if (errno || !end || *end || v > UINT32_MAX) return -1;
    *out = (uint32_t)v; return 0;
}
static void dump_heap(const VkPhysicalDeviceMemoryProperties *mem) {
    for (uint32_t h=0;h<mem->memoryHeapCount;h++)
        printf("  heap[%-2u] size=%" PRIu64 " MiB device_local=%d\n",h,
            (uint64_t)(mem->memoryHeaps[h].size/MIB),
            !!(mem->memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT));
    for (uint32_t t=0;t<mem->memoryTypeCount;t++) {
        VkMemoryPropertyFlags f=mem->memoryTypes[t].propertyFlags;
        printf("  type[%-2u] heap=%u DEVICE_LOCAL=%d HOST_VISIBLE=%d COHERENT=%d CACHED=%d\n",
            t,mem->memoryTypes[t].heapIndex,!!(f&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
            !!(f&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),!!(f&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            !!(f&VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
    }
}
static int has_budget_ext(VkPhysicalDevice gpu) {
    uint32_t n=0;
    if (vkEnumerateDeviceExtensionProperties(gpu,NULL,&n,NULL)!=VK_SUCCESS || !n) return 0;
    VkExtensionProperties *ext=calloc(n,sizeof(*ext));
    if (!ext) return 0;
    int found=0;
    if(vkEnumerateDeviceExtensionProperties(gpu,NULL,&n,ext)==VK_SUCCESS)
        for(uint32_t i=0;i<n;i++)
            if(strcmp(ext[i].extensionName,VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)==0) found=1;
    free(ext); return found;
}
static VkPhysicalDevice *list_gpus(VkInstance instance, uint32_t *count) {
    if(vkEnumeratePhysicalDevices(instance,count,NULL)!=VK_SUCCESS || !*count) return NULL;
    VkPhysicalDevice *gpus=calloc(*count,sizeof(*gpus));
    if(!gpus)return NULL;
    if(vkEnumeratePhysicalDevices(instance,count,gpus)!=VK_SUCCESS){free(gpus);return NULL;}
    for(uint32_t i=0;i<*count;i++) {
        VkPhysicalDeviceProperties p; VkPhysicalDeviceMemoryProperties m;
        vkGetPhysicalDeviceProperties(gpus[i],&p);
        vkGetPhysicalDeviceMemoryProperties(gpus[i],&m);
        printf("GPU[%u]: %s | vendor=%04x device=%04x type=%u driver=%u\n",i,p.deviceName,
               p.vendorID,p.deviceID,p.deviceType,p.driverVersion);
        dump_heap(&m);
    }
    return gpus;
}

/* Non-overlapping page-aligned samples; always include both ends. */
static uint64_t choose_page(uint64_t i, uint64_t total_pages, uint32_t samples) {
    if(i==0)return 0;
    if(i==1)return total_pages-1;
    return ((i-1)*(total_pages-1))/(samples-1);
}

int main(int argc,char **argv) {
    uint32_t mib=64, samples=256,gpu_index=UINT32_MAX;
    int list_only=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){usage(argv[0]);return 0;}
        if(!strcmp(argv[i],"--list")){list_only=1;continue;}
        if(i+1>=argc){usage(argv[0]);return 2;}
        if(!strcmp(argv[i],"--mib")){if(parse_u32(argv[++i],&mib))return 2;continue;}
        if(!strcmp(argv[i],"--samples")){if(parse_u32(argv[++i],&samples))return 2;continue;}
        if(!strcmp(argv[i],"--gpu-index")){if(parse_u32(argv[++i],&gpu_index))return 2;continue;}
        usage(argv[0]);return 2;
    }
    if(mib==0||mib>2048||samples==0||samples>MAX_SAMPLES){
        fprintf(stderr,"ERROR: --mib must be 1..2048; --samples must be 1..512\n");return 2;
    }
    VkInstance inst=VK_NULL_HANDLE; VkDevice dev=VK_NULL_HANDLE;
    VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory allocation=VK_NULL_HANDLE;
    void *mapping=NULL; VkResult r; int status=1;
    VkApplicationInfo app={0};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName="Nopen Vulkan VRAM Probe";app.applicationVersion=VK_MAKE_VERSION(0,10,0);
    app.pEngineName="Nopen";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici={0};ici.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ici.pApplicationInfo=&app;
    r=vkCreateInstance(&ici,NULL,&inst);
    if(r!=VK_SUCCESS){fprintf(stderr,"vkCreateInstance: %s (%d)\n",vk_name(r),r);return 1;}
    uint32_t gpu_count=0;
    VkPhysicalDevice *gpus=list_gpus(inst,&gpu_count);
    if(!gpus){fprintf(stderr,"No Vulkan GPU found. Check mesa-vulkan-drivers / vulkan-radeon.\n");goto cleanup;}
    if(list_only){status=0;free(gpus);goto cleanup;}
    if(gpu_index==UINT32_MAX){
        for(uint32_t i=0;i<gpu_count;i++) {
            VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(gpus[i],&p);
            if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){gpu_index=i;break;}
        }
    }
    if(gpu_index>=gpu_count){fprintf(stderr,"No discrete Vulkan GPU selected; use --gpu-index deliberately.\n");free(gpus);goto cleanup;}
    VkPhysicalDevice gpu=gpus[gpu_index];
    VkPhysicalDeviceProperties props;VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceProperties(gpu,&props);
    vkGetPhysicalDeviceMemoryProperties(gpu,&mem);
    free(gpus);
    if(props.deviceType!=VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){
        fprintf(stderr,"Refusing non-discrete Vulkan device (CPU/iGPU may report shared RAM).\n");goto cleanup;
    }
    uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,NULL);
    VkQueueFamilyProperties *qprops=calloc(nq?nq:1,sizeof(*qprops));
    if(!qprops)goto cleanup;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,qprops);
    uint32_t family=UINT32_MAX;
    for(uint32_t j=0;j<nq;j++)if(qprops[j].queueCount>0){family=j;break;}
    free(qprops);
    if(family==UINT32_MAX){fprintf(stderr,"GPU has no queue family\n");goto cleanup;}
    const float priority=1.f;
    VkDeviceQueueCreateInfo qci={0};qci.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
    VkDeviceCreateInfo dci={0};dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;
    r=vkCreateDevice(gpu,&dci,NULL,&dev);
    if(r!=VK_SUCCESS){fprintf(stderr,"vkCreateDevice: %s\n",vk_name(r));goto cleanup;}
    const VkDeviceSize bytes=(VkDeviceSize)mib*MIB;
    VkBufferCreateInfo bci={0};bci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size=bytes;bci.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    r=vkCreateBuffer(dev,&bci,NULL,&buffer);
    if(r!=VK_SUCCESS){fprintf(stderr,"vkCreateBuffer: %s (%d)\n",vk_name(r),r);goto cleanup;}
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(dev,buffer,&req);
    int choice=-1,score=-999;
    for(uint32_t t=0;t<mem.memoryTypeCount;t++) {
        if(!(req.memoryTypeBits&(1u<<t)))continue;
        VkMemoryPropertyFlags f=mem.memoryTypes[t].propertyFlags;
        if((f&(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) !=
             (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))continue;
        uint32_t h=mem.memoryTypes[t].heapIndex;
        if(mem.memoryHeaps[h].size < req.size)continue;
        int s=((f&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?2:0)+
              ((f&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?4:0);
        if(s>score){score=s;choice=(int)t;}
    }
    if(choice<0){
        fprintf(stderr,"UNSUPPORTED: no buffer-compatible DEVICE_LOCAL|HOST_VISIBLE memory type.\n");
        fprintf(stderr,"Vulkan cannot safely map this VRAM with this approach. No RAM fallback performed.\n");
        goto cleanup;
    }
    uint32_t heap=mem.memoryTypes[choice].heapIndex;
    VkMemoryPropertyFlags flags=mem.memoryTypes[choice].propertyFlags;
    printf("SELECTED gpu=%u name=%s memory_type=%d heap=%u bytes=%"PRIu64" coherent=%d cached=%d\n",
       gpu_index,props.deviceName,choice,heap,(uint64_t)req.size,
       !!(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),!!(flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
    if(has_budget_ext(gpu)) {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget={0};
        budget.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        VkPhysicalDeviceMemoryProperties2 props2={0};
        props2.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        props2.pNext=&budget;
        vkGetPhysicalDeviceMemoryProperties2(gpu,&props2);
        VkDeviceSize remain=budget.heapBudget[heap]>budget.heapUsage[heap]?
            budget.heapBudget[heap]-budget.heapUsage[heap]:0;
        printf("BUDGET heap=%u budget_mib=%"PRIu64" usage_mib=%"PRIu64" available_mib=%"PRIu64"\n",
           heap,(uint64_t)(budget.heapBudget[heap]/MIB),(uint64_t)(budget.heapUsage[heap]/MIB),
           (uint64_t)(remain/MIB));
        if(remain<req.size+(32*MIB)) {
            fprintf(stderr,"REFUSED: insufficient estimated Vulkan heap budget (+32 MiB margin).\n");goto cleanup;
        }
    } else puts("WARN: VK_EXT_memory_budget unavailable; heap budget not known.");
    VkMemoryAllocateInfo mai={0};mai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize=req.size;mai.memoryTypeIndex=(uint32_t)choice;
    r=vkAllocateMemory(dev,&mai,NULL,&allocation);
    if(r!=VK_SUCCESS){fprintf(stderr,"vkAllocateMemory: %s (%d)\n",vk_name(r),r);goto cleanup;}
    r=vkBindBufferMemory(dev,buffer,allocation,0);
    if(r!=VK_SUCCESS){fprintf(stderr,"vkBindBufferMemory: %s\n",vk_name(r));goto cleanup;}
    r=vkMapMemory(dev,allocation,0,req.size,0,&mapping);
    if(r!=VK_SUCCESS){fprintf(stderr,"vkMapMemory: %s (%d)\n",vk_name(r),r);goto cleanup;}
    uint64_t total_pages=(uint64_t)bytes/PAGE;
    uint32_t nsamples=(uint32_t)(total_pages<samples?total_pages:samples);
    if(nsamples==1){puts("INFO: a single test page is available");}
    volatile uint8_t *ptr=(volatile uint8_t *)mapping;
    for(unsigned pass=0;pass<2;pass++){
        uint64_t start=monotonic_ns();
        for(uint32_t i=0;i<nsamples;i++){
            uint64_t idx=choose_page(i,total_pages,nsamples);
            uint64_t base=idx*PAGE;
            for(unsigned o=0;o<PAGE;o++)ptr[base+o]=pattern(idx,o,pass);
        }
        if(!(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){
            VkMappedMemoryRange range={0};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory=allocation;range.offset=0;range.size=VK_WHOLE_SIZE;
            r=vkFlushMappedMemoryRanges(dev,1,&range);
            if(r!=VK_SUCCESS){fprintf(stderr,"vkFlushMappedMemoryRanges: %s\n",vk_name(r));goto cleanup;}
        }
        for(uint32_t i=0;i<nsamples;i++){
            uint64_t idx=choose_page(i,total_pages,nsamples);
            uint64_t base=idx*PAGE;
            for(unsigned o=0;o<PAGE;o++){
                uint8_t got=ptr[base+o],expected=pattern(idx,o,pass);
                if(got!=expected){fprintf(stderr,"DATA ERROR pass=%u page=%"PRIu64" offset=%u expected=%u got=%u\n",
                           pass,idx,o,(unsigned)expected,(unsigned)got);goto cleanup;}
            }
        }
        uint64_t elapsed=monotonic_ns()-start;
        printf("PASS pass=%u tested_pages=%u verified_bytes=%"PRIu64" elapsed_ms=%.3f\n",
            pass+1,nsamples,(uint64_t)(nsamples*PAGE),(double)elapsed/1000000.);
    }
    puts("PASS: host-mapped Vulkan DEVICE_LOCAL VRAM page patterns verified (NO SWAP).");
    status=0;
cleanup:
    if(mapping)vkUnmapMemory(dev,allocation);
    if(buffer)vkDestroyBuffer(dev,buffer,NULL);
    if(allocation)vkFreeMemory(dev,allocation,NULL);
    if(dev)vkDestroyDevice(dev,NULL);
    if(inst)vkDestroyInstance(inst,NULL);
    return status;
}