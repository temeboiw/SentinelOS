#pragma once
/*
 * SentinelOS I/O Manager (Io).
 * Provides device objects, driver dispatch tables, stack attachment and IRP-like
 * request packets. This is an incremental NT-style foundation; filesystem and
 * hardware drivers must be migrated to it before it is a unified I/O subsystem.
 */
#include <stdint.h>
#include <stddef.h>
#include "obmgr.h"

#define IO_MAX_DEVICES 32
#define IO_MAX_STACK_DEPTH 8
#define IO_DEVICE_NAME_MAX 64
#define IO_DEVICE_EXTENSION_MAX 128
#define IO_IRP_BUFFER_MAX 256

typedef int32_t IO_STATUS;
#define IO_STATUS_SUCCESS             ((IO_STATUS)0)
#define IO_STATUS_PENDING             ((IO_STATUS)0x00000103)
#define IO_STATUS_INVALID_PARAMETER   ((IO_STATUS)0xC000000D)
#define IO_STATUS_INSUFFICIENT_RESOURCES ((IO_STATUS)0xC000009A)
#define IO_STATUS_NOT_SUPPORTED       ((IO_STATUS)0xC00000BB)
#define IO_STATUS_NO_SUCH_DEVICE      ((IO_STATUS)0xC000000E)
#define IO_STATUS_ACCESS_DENIED       ((IO_STATUS)0xC0000022)

typedef enum {
    IRP_MJ_CREATE=0x00, IRP_MJ_CREATE_NAMED_PIPE=0x01, IRP_MJ_CLOSE=0x02,
    IRP_MJ_READ=0x03, IRP_MJ_WRITE=0x04, IRP_MJ_QUERY_INFORMATION=0x05,
    IRP_MJ_SET_INFORMATION=0x06, IRP_MJ_QUERY_EA=0x07, IRP_MJ_SET_EA=0x08,
    IRP_MJ_FLUSH_BUFFERS=0x09, IRP_MJ_QUERY_VOLUME_INFORMATION=0x0A,
    IRP_MJ_SET_VOLUME_INFORMATION=0x0B, IRP_MJ_DIRECTORY_CONTROL=0x0C,
    IRP_MJ_FILE_SYSTEM_CONTROL=0x0D, IRP_MJ_DEVICE_CONTROL=0x0E,
    IRP_MJ_INTERNAL_DEVICE_CONTROL=0x0F, IRP_MJ_SHUTDOWN=0x10,
    IRP_MJ_LOCK_CONTROL=0x11, IRP_MJ_CLEANUP=0x12,
    IRP_MJ_CREATE_MAILSLOT=0x13, IRP_MJ_QUERY_SECURITY=0x14,
    IRP_MJ_SET_SECURITY=0x15, IRP_MJ_POWER=0x16, IRP_MJ_SYSTEM_CONTROL=0x17,
    IRP_MJ_DEVICE_CHANGE=0x18, IRP_MJ_QUERY_QUOTA=0x19, IRP_MJ_SET_QUOTA=0x1A,
    IRP_MJ_PNP=0x1B, IRP_MJ_MAXIMUM_FUNCTION=0x1B
} IO_MAJOR_FUNCTION;

typedef struct _IO_DEVICE_OBJECT IO_DEVICE_OBJECT;
typedef struct _IO_DRIVER_OBJECT IO_DRIVER_OBJECT;
typedef struct _IO_IRP IO_IRP;
typedef IO_STATUS (*IO_DISPATCH_ROUTINE)(IO_DEVICE_OBJECT *device,IO_IRP *irp);
typedef void (*IO_COMPLETION_ROUTINE)(IO_IRP *irp,void *context);

typedef struct {
    uint8_t major_function;
    uint8_t reserved[7];
    IO_DEVICE_OBJECT *device;
    uint64_t argument1,argument2;
} IO_STACK_LOCATION;

struct _IO_DRIVER_OBJECT {
    uint32_t signature;
    char name[IO_DEVICE_NAME_MAX];
    IO_DISPATCH_ROUTINE MajorFunction[IRP_MJ_MAXIMUM_FUNCTION+1];
    uint8_t used;
};

struct _IO_DEVICE_OBJECT {
    uint32_t signature;
    char name[IO_DEVICE_NAME_MAX];
    uint32_t device_type, characteristics, flags;
    IO_DRIVER_OBJECT *driver;
    IO_DEVICE_OBJECT *attached_device;
    IO_DEVICE_OBJECT *lower_device;
    uint16_t extension_size;
    uint8_t used;
    uint8_t reserved;
    uint8_t extension[IO_DEVICE_EXTENSION_MAX];
};

struct _IO_IRP {
    uint32_t signature;
    uint8_t major_function, stack_count, current_location, completed;
    uint8_t in_flight, pending, completion_active, dispatch_depth;
    const void *dispatch_owner;
    IO_STATUS status;
    uint64_t information;
    void *system_buffer;
    uint32_t input_length, output_length;
    IO_STACK_LOCATION stack[IO_MAX_STACK_DEPTH];
    IO_COMPLETION_ROUTINE completion;
    void *completion_context;
};

#define IO_DRIVER_SIGNATURE 0x44524956u
#define IO_DEVICE_SIGNATURE 0x44455649u
#define IO_IRP_SIGNATURE    0x49525053u

#ifndef IO_SPIN_WAIT
#define IO_SPIN_WAIT() __asm__ volatile("pause")
#endif
#ifndef IO_CURRENT_THREAD_TOKEN
#include "cpu.h"
#define IO_CURRENT_THREAD_TOKEN() (cpu_local_current()->scheduler_current)
#define IO_CURRENT_CPU_TOKEN() ((const void *)cpu_local_current())
#elif !defined(IO_CURRENT_CPU_TOKEN)
#define IO_CURRENT_CPU_TOKEN() ((const void *)0)
#endif
static const void *IoDispatchOwnerToken(void) {
    const void *owner=IO_CURRENT_THREAD_TOKEN();
    return owner?owner:IO_CURRENT_CPU_TOKEN();
}

static IO_DRIVER_OBJECT IoDrivers[IO_MAX_DEVICES];
static IO_DEVICE_OBJECT IoDevices[IO_MAX_DEVICES];
static IO_IRP IoIrpPool[IO_MAX_DEVICES];
static uint8_t IoIrpUsed[IO_MAX_DEVICES];
static volatile uint32_t IoLockWord;
static uint32_t IoDeviceCount,IoDriverCount,IoIrpCount;

static void IoLock(void) {
    while(__atomic_exchange_n(&IoLockWord,1u,__ATOMIC_ACQUIRE))
        while(__atomic_load_n(&IoLockWord,__ATOMIC_RELAXED))IO_SPIN_WAIT();
}
static void IoUnlock(void){__atomic_store_n(&IoLockWord,0u,__ATOMIC_RELEASE);}
static int IoNameLength(const char *src,size_t cap,size_t *length) {
    if(!src||!length)return 0;
    size_t i=0;while(i<cap&&src[i])i++;
    if(i==cap)return 0;
    *length=i;return 1;
}
static void IoCopyName(char *dst,const char *src,size_t length) {
    for(size_t i=0;i<length;i++)dst[i]=src[i];
    dst[length]=0;
}
static int IoNameEqual(const char *a,const char *b) {
    if(!a||!b)return 0;
    while(*a&&*b&&*a==*b){a++;b++;}
    return *a==*b;
}
static int IoPoolContains(const void *pointer,const void *base,size_t count,size_t stride) {
    uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)base;
    if(p<b)return 0;
    uintptr_t delta=p-b;
    return delta<(uintptr_t)(count*stride)&&delta%stride==0;
}
static int IoDriverValidLocked(const IO_DRIVER_OBJECT *driver) {
    return IoPoolContains(driver,IoDrivers,IO_MAX_DEVICES,sizeof(IoDrivers[0]))&&
        driver->used&&driver->signature==IO_DRIVER_SIGNATURE;
}
static int IoDeviceValidLocked(const IO_DEVICE_OBJECT *device) {
    return IoPoolContains(device,IoDevices,IO_MAX_DEVICES,sizeof(IoDevices[0]))&&
        device->used&&device->signature==IO_DEVICE_SIGNATURE;
}
static int IoIrpValidLocked(const IO_IRP *irp) {
    if(!IoPoolContains(irp,IoIrpPool,IO_MAX_DEVICES,sizeof(IoIrpPool[0])))return 0;
    size_t index=((uintptr_t)irp-(uintptr_t)IoIrpPool)/sizeof(IoIrpPool[0]);
    return IoIrpUsed[index]&&irp->signature==IO_IRP_SIGNATURE;
}
static void IoInitialize(void) {
    IoLock();
    for(unsigned i=0;i<IO_MAX_DEVICES;i++){
        IoDrivers[i].used=0;IoDrivers[i].signature=0;IoDrivers[i].name[0]=0;
        for(unsigned j=0;j<=IRP_MJ_MAXIMUM_FUNCTION;j++)IoDrivers[i].MajorFunction[j]=NULL;
        IoDevices[i].used=0;IoDevices[i].signature=0;IoDevices[i].name[0]=0;
        IoDevices[i].driver=NULL;IoDevices[i].attached_device=NULL;IoDevices[i].lower_device=NULL;
    }
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)IoIrpUsed[i]=0;
    IoDeviceCount=IoDriverCount=IoIrpCount=0;
    IoUnlock();
}
static IO_DRIVER_OBJECT *IoCreateDriverObject(const char *name) {
    size_t name_length;
    if(!IoNameLength(name,IO_DEVICE_NAME_MAX,&name_length)||!name_length)return NULL;
    IoLock();
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(IoDrivers[i].used&&IoNameEqual(IoDrivers[i].name,name)){IoUnlock();return NULL;}
    IO_DRIVER_OBJECT *d=NULL;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(!IoDrivers[i].used){d=&IoDrivers[i];break;}
    if(d){d->signature=IO_DRIVER_SIGNATURE;IoCopyName(d->name,name,name_length);d->used=1;IoDriverCount++;}
    IoUnlock();return d;
}
static int IoSetMajorFunction(IO_DRIVER_OBJECT *driver,uint8_t major,IO_DISPATCH_ROUTINE dispatch) {
    if(!driver||major>IRP_MJ_MAXIMUM_FUNCTION)return 0;
    IoLock();
    if(!IoDriverValidLocked(driver)){IoUnlock();return 0;}
    driver->MajorFunction[major]=dispatch;
    IoUnlock();return 1;
}
static IO_STATUS IoCreateDevice(IO_DRIVER_OBJECT *driver,const char *name,uint32_t type,
                                uint32_t characteristics,uint16_t extension_size,
                                IO_DEVICE_OBJECT **out_device) {
    size_t name_length;
    if(!driver||!IoNameLength(name,IO_DEVICE_NAME_MAX,&name_length)||!name_length||
       !out_device||extension_size>IO_DEVICE_EXTENSION_MAX)return IO_STATUS_INVALID_PARAMETER;
    *out_device=NULL;
    IoLock();
    if(!IoDriverValidLocked(driver)){IoUnlock();return IO_STATUS_NO_SUCH_DEVICE;}
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(IoDevices[i].used&&IoNameEqual(IoDevices[i].name,name)){IoUnlock();return IO_STATUS_ACCESS_DENIED;}
    IO_DEVICE_OBJECT *d=NULL;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(!IoDevices[i].used){d=&IoDevices[i];break;}
    if(!d){IoUnlock();return IO_STATUS_INSUFFICIENT_RESOURCES;}
    d->signature=IO_DEVICE_SIGNATURE;IoCopyName(d->name,name,name_length);
    d->device_type=type;d->characteristics=characteristics;d->flags=0;d->driver=driver;
    d->attached_device=NULL;d->lower_device=NULL;d->extension_size=extension_size;d->used=1;
    for(unsigned i=0;i<IO_DEVICE_EXTENSION_MAX;i++)d->extension[i]=0;
    IoDeviceCount++;*out_device=d;IoUnlock();return IO_STATUS_SUCCESS;
}
static IO_DEVICE_OBJECT *IoGetDeviceByName(const char *name) {
    size_t name_length;
    if(!IoNameLength(name,IO_DEVICE_NAME_MAX,&name_length)||!name_length)return NULL;
    IoLock();IO_DEVICE_OBJECT *found=NULL;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(IoDevices[i].used&&IoNameEqual(IoDevices[i].name,name)){found=&IoDevices[i];break;}
    IoUnlock();return found;
}
static IO_DEVICE_OBJECT *IoAttachDeviceToDeviceStack(IO_DEVICE_OBJECT *source,IO_DEVICE_OBJECT *target) {
    if(!source||!target||source==target)return NULL;
    IoLock();
    if(!IoDeviceValidLocked(source)||!IoDeviceValidLocked(target)||source->lower_device){IoUnlock();return NULL;}
    IO_DEVICE_OBJECT *lower=target;
    unsigned lower_depth=0;
    while(lower){
        if(!IoDeviceValidLocked(lower)||lower==source||lower_depth++>=IO_MAX_STACK_DEPTH){IoUnlock();return NULL;}
        lower=lower->lower_device;
    }
    IO_DEVICE_OBJECT *top=target;
    unsigned depth=1;
    while(top->attached_device) {
        if(top==source||depth>=IO_MAX_STACK_DEPTH||
           !IoDeviceValidLocked(top->attached_device)){IoUnlock();return NULL;}
        top=top->attached_device;
        ++depth;
    }
    if(top==source){IoUnlock();return NULL;}
    source->lower_device=top;top->attached_device=source;
    IoUnlock();return top;
}
static IO_IRP *IoAllocateIrp(uint8_t major,void *buffer,uint32_t input_length,uint32_t output_length) {
    if(major>IRP_MJ_MAXIMUM_FUNCTION||input_length>IO_IRP_BUFFER_MAX||
       output_length>IO_IRP_BUFFER_MAX-input_length||
       (input_length&&!buffer)||(output_length&&!buffer))return NULL;
    IoLock();IO_IRP *irp=NULL;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(!IoIrpUsed[i]){IoIrpUsed[i]=1;irp=&IoIrpPool[i];break;}
    if(irp){irp->signature=IO_IRP_SIGNATURE;irp->major_function=major;irp->stack_count=1;
        irp->current_location=0;irp->completed=0;irp->in_flight=0;irp->pending=0;irp->completion_active=0;
        irp->dispatch_depth=0;irp->dispatch_owner=NULL;
        irp->status=IO_STATUS_PENDING;irp->information=0;
        irp->system_buffer=buffer;irp->input_length=input_length;irp->output_length=output_length;
        irp->completion=NULL;irp->completion_context=NULL;
        for(unsigned i=0;i<IO_MAX_STACK_DEPTH;i++){irp->stack[i].major_function=major;irp->stack[i].device=NULL;irp->stack[i].argument1=0;irp->stack[i].argument2=0;}
        IoIrpCount++;}
    IoUnlock();return irp;
}
static int IoCompleteRequest(IO_IRP *irp,IO_STATUS status,uint64_t information) {
    IO_COMPLETION_ROUTINE completion=NULL;void *context=NULL;int took_flight=0;
    if(!irp)return 0;
    IoLock();
    if(!IoIrpValidLocked(irp)||irp->completed){IoUnlock();return 0;}
    uint32_t information_limit =
        (irp->major_function==IRP_MJ_WRITE||irp->major_function==IRP_MJ_SET_INFORMATION)
            ? irp->input_length : irp->output_length;
    if(information>information_limit){status=IO_STATUS_INVALID_PARAMETER;information=0;}
    irp->status=status;irp->information=information;irp->completed=1;irp->pending=0;
    completion=irp->completion;context=irp->completion_context;
    if(completion){
        if(!irp->in_flight){irp->in_flight=1;took_flight=1;}
        irp->completion_active=1;
    }
    IoUnlock();
    if(completion)completion(irp,context);
    if(completion){
        IoLock();
        if(IoIrpValidLocked(irp)){
            irp->completion_active=0;
            if(took_flight)irp->in_flight=0;
        }
        IoUnlock();
    }
    return 1;
}
static IO_STATUS IoCallDriver(IO_DEVICE_OBJECT *device,IO_IRP *irp) {
    if(!device||!irp)return IO_STATUS_INVALID_PARAMETER;
    const void *owner=IoDispatchOwnerToken();
    if(!owner)return IO_STATUS_INVALID_PARAMETER;
    IoLock();
    if(!IoDeviceValidLocked(device)||!IoIrpValidLocked(irp)||irp->completed||
       (irp->in_flight&&irp->dispatch_owner!=owner)||irp->dispatch_depth>=IO_MAX_STACK_DEPTH){
        IoUnlock();return IO_STATUS_INVALID_PARAMETER;
    }
    /* Dispatch to exactly the device supplied by the caller. A filter driver
       explicitly forwards to its lower_device by calling IoCallDriver again. */
    IO_DEVICE_OBJECT *target=device;
    if(!IoDriverValidLocked(target->driver)){IoUnlock();return IO_STATUS_NO_SUCH_DEVICE;}
    IO_DISPATCH_ROUTINE dispatch=target->driver->MajorFunction[irp->major_function];
    if(!dispatch){IoUnlock();return IO_STATUS_NOT_SUPPORTED;}
    if(!irp->in_flight){irp->in_flight=1;irp->dispatch_owner=owner;}
    unsigned frame=irp->dispatch_depth++;
    irp->stack[frame].device=target;irp->stack[frame].major_function=irp->major_function;
    IoUnlock();
    IO_STATUS status=dispatch(target,irp);
    int should_complete=0;
    IoLock();
    if(IoIrpValidLocked(irp)&&!irp->completed&&!irp->pending&&status!=IO_STATUS_PENDING)
        should_complete=1;
    IoUnlock();
    if(should_complete)IoCompleteRequest(irp,status,irp->information);
    IoLock();
    if(IoIrpValidLocked(irp)){
        if(irp->dispatch_depth)irp->dispatch_depth--;
        if(status==IO_STATUS_PENDING&&!irp->completed)irp->pending=1;
        if(!irp->dispatch_depth){
            if(irp->pending&&!irp->completed)status=IO_STATUS_PENDING;
            irp->dispatch_owner=NULL;irp->in_flight=0;
        }
    }
    IoUnlock();
    return status;
}
static int IoFreeIrp(IO_IRP *irp) {
    if(!irp)return 0;
    IoLock();
    int found=0;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(irp==&IoIrpPool[i]&&IoIrpUsed[i]&&
        irp->signature==IO_IRP_SIGNATURE&&!irp->in_flight&&!irp->pending&&!irp->completion_active) {
        IoIrpUsed[i]=0;irp->signature=0;found=1;if(IoIrpCount)IoIrpCount--;break;
    }
    IoUnlock();return found;
}
/* A tiny boot-time contract test validates real dispatch and completion flow. */
static IO_STATUS IoSelfTestDispatch(IO_DEVICE_OBJECT *device,IO_IRP *irp) {
    (void)device;
    if(!irp||irp->major_function!=IRP_MJ_DEVICE_CONTROL)return IO_STATUS_INVALID_PARAMETER;
    return IO_STATUS_SUCCESS;
}
static int IoSelfTest(void) {
    IO_DRIVER_OBJECT *driver=IoCreateDriverObject("SentinelIoSelfTest");
    if(!driver)return 0;
    if(!IoSetMajorFunction(driver,IRP_MJ_DEVICE_CONTROL,IoSelfTestDispatch))return 0;
    IO_DEVICE_OBJECT *device=NULL;
    if(IoCreateDevice(driver,"\\Device\\SentinelIoSelfTest",1,0,0,&device)!=IO_STATUS_SUCCESS)return 0;
    IO_IRP *irp=IoAllocateIrp(IRP_MJ_DEVICE_CONTROL,NULL,0,0);
    if(!irp)return 0;
    IO_STATUS status=IoCallDriver(device,irp);
    int ok=status==IO_STATUS_SUCCESS&&irp->completed&&irp->status==IO_STATUS_SUCCESS;
    IoFreeIrp(irp);
    return ok;
}
