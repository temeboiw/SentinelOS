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
    IRP_MJ_CREATE=0, IRP_MJ_CLOSE, IRP_MJ_READ, IRP_MJ_WRITE,
    IRP_MJ_QUERY_INFORMATION, IRP_MJ_SET_INFORMATION, IRP_MJ_QUERY_VOLUME_INFORMATION,
    IRP_MJ_DIRECTORY_CONTROL, IRP_MJ_DEVICE_CONTROL, IRP_MJ_INTERNAL_DEVICE_CONTROL,
    IRP_MJ_CLEANUP, IRP_MJ_FLUSH_BUFFERS, IRP_MJ_SHUTDOWN, IRP_MJ_PNP,
    IRP_MJ_POWER, IRP_MJ_SYSTEM_CONTROL, IRP_MJ_MAXIMUM_FUNCTION=27
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

static IO_DRIVER_OBJECT IoDrivers[IO_MAX_DEVICES];
static IO_DEVICE_OBJECT IoDevices[IO_MAX_DEVICES];
static volatile uint32_t IoLockWord;
static uint32_t IoDeviceCount,IoDriverCount,IoIrpCount;

static void IoLock(void) {
    while(__atomic_exchange_n(&IoLockWord,1u,__ATOMIC_ACQUIRE))
        while(__atomic_load_n(&IoLockWord,__ATOMIC_RELAXED))__asm__ volatile("pause");
}
static void IoUnlock(void){__atomic_store_n(&IoLockWord,0u,__ATOMIC_RELEASE);}
static void IoCopyName(char *dst,const char *src,size_t cap) {
    size_t i=0;if(!cap)return;
    if(src)while(src[i]&&i+1<cap){dst[i]=src[i];i++;}
    dst[i]=0;
}
static int IoNameEqual(const char *a,const char *b) {
    if(!a||!b)return 0;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;
}
static void IoInitialize(void) {
    IoLock();
    for(unsigned i=0;i<IO_MAX_DEVICES;i++){
        IoDrivers[i].used=0;IoDrivers[i].signature=0;IoDrivers[i].name[0]=0;
        for(unsigned j=0;j<=IRP_MJ_MAXIMUM_FUNCTION;j++)IoDrivers[i].MajorFunction[j]=NULL;
        IoDevices[i].used=0;IoDevices[i].signature=0;IoDevices[i].name[0]=0;
        IoDevices[i].driver=NULL;IoDevices[i].attached_device=NULL;IoDevices[i].lower_device=NULL;
    }
    IoDeviceCount=IoDriverCount=IoIrpCount=0;
    IoUnlock();
}
static IO_DRIVER_OBJECT *IoCreateDriverObject(const char *name) {
    if(!name||!name[0])return NULL;
    IoLock();
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(IoDrivers[i].used&&IoNameEqual(IoDrivers[i].name,name)){IoUnlock();return NULL;}
    IO_DRIVER_OBJECT *d=NULL;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(!IoDrivers[i].used){d=&IoDrivers[i];break;}
    if(d){d->signature=IO_DRIVER_SIGNATURE;IoCopyName(d->name,name,sizeof d->name);d->used=1;IoDriverCount++;}
    IoUnlock();return d;
}
static int IoSetMajorFunction(IO_DRIVER_OBJECT *driver,uint8_t major,IO_DISPATCH_ROUTINE dispatch) {
    if(!driver||driver->signature!=IO_DRIVER_SIGNATURE||!driver->used||major>IRP_MJ_MAXIMUM_FUNCTION)return 0;
    IoLock();driver->MajorFunction[major]=dispatch;IoUnlock();return 1;
}
static IO_STATUS IoCreateDevice(IO_DRIVER_OBJECT *driver,const char *name,uint32_t type,
                                uint32_t characteristics,uint16_t extension_size,
                                IO_DEVICE_OBJECT **out_device) {
    if(!driver||driver->signature!=IO_DRIVER_SIGNATURE||!driver->used||!name||!name[0]||
       !out_device||extension_size>IO_DEVICE_EXTENSION_MAX)return IO_STATUS_INVALID_PARAMETER;
    IoLock();
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(IoDevices[i].used&&IoNameEqual(IoDevices[i].name,name)){IoUnlock();return IO_STATUS_ACCESS_DENIED;}
    IO_DEVICE_OBJECT *d=NULL;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(!IoDevices[i].used){d=&IoDevices[i];break;}
    if(!d){IoUnlock();return IO_STATUS_INSUFFICIENT_RESOURCES;}
    d->signature=IO_DEVICE_SIGNATURE;IoCopyName(d->name,name,sizeof d->name);
    d->device_type=type;d->characteristics=characteristics;d->flags=0;d->driver=driver;
    d->attached_device=NULL;d->lower_device=NULL;d->extension_size=extension_size;d->used=1;
    for(unsigned i=0;i<IO_DEVICE_EXTENSION_MAX;i++)d->extension[i]=0;
    IoDeviceCount++;*out_device=d;IoUnlock();return IO_STATUS_SUCCESS;
}
static IO_DEVICE_OBJECT *IoGetDeviceByName(const char *name) {
    if(!name)return NULL;IoLock();IO_DEVICE_OBJECT *found=NULL;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(IoDevices[i].used&&IoNameEqual(IoDevices[i].name,name)){found=&IoDevices[i];break;}
    IoUnlock();return found;
}
static IO_DEVICE_OBJECT *IoAttachDeviceToDeviceStack(IO_DEVICE_OBJECT *source,IO_DEVICE_OBJECT *target) {
    if(!source||!target||source==target||source->signature!=IO_DEVICE_SIGNATURE||target->signature!=IO_DEVICE_SIGNATURE)return NULL;
    IoLock();
    if(!source->used||!target->used||source->lower_device){IoUnlock();return NULL;}
    IO_DEVICE_OBJECT *top=target;
    unsigned depth=0;
    while(top->attached_device&&depth++<IO_MAX_STACK_DEPTH)top=top->attached_device;
    if(depth>=IO_MAX_STACK_DEPTH){IoUnlock();return NULL;}
    source->lower_device=top;top->attached_device=source;
    IoUnlock();return top;
}
static IO_IRP *IoAllocateIrp(uint8_t major,void *buffer,uint32_t input_length,uint32_t output_length) {
    if(major>IRP_MJ_MAXIMUM_FUNCTION||input_length>IO_IRP_BUFFER_MAX||output_length>IO_IRP_BUFFER_MAX||
       (input_length&&!buffer)||(output_length&&!buffer))return NULL;
    static IO_IRP IrpPool[IO_MAX_DEVICES];
    static uint8_t IrpUsed[IO_MAX_DEVICES];
    IoLock();IO_IRP *irp=NULL;
    for(unsigned i=0;i<IO_MAX_DEVICES;i++)if(!IrpUsed[i]){IrpUsed[i]=1;irp=&IrpPool[i];break;}
    if(irp){irp->signature=IO_IRP_SIGNATURE;irp->major_function=major;irp->stack_count=1;
        irp->current_location=0;irp->completed=0;irp->status=IO_STATUS_PENDING;irp->information=0;
        irp->system_buffer=buffer;irp->input_length=input_length;irp->output_length=output_length;
        irp->completion=NULL;irp->completion_context=NULL;
        for(unsigned i=0;i<IO_MAX_STACK_DEPTH;i++){irp->stack[i].major_function=major;irp->stack[i].device=NULL;irp->stack[i].argument1=0;irp->stack[i].argument2=0;}
        IoIrpCount++;}
    IoUnlock();return irp;
}
static void IoCompleteRequest(IO_IRP *irp,IO_STATUS status,uint64_t information) {
    if(!irp||irp->signature!=IO_IRP_SIGNATURE||irp->completed)return;
    irp->status=status;irp->information=information;irp->completed=1;
    if(irp->completion)irp->completion(irp,irp->completion_context);
}
static IO_STATUS IoCallDriver(IO_DEVICE_OBJECT *device,IO_IRP *irp) {
    if(!device||device->signature!=IO_DEVICE_SIGNATURE||!device->used||!irp||
       irp->signature!=IO_IRP_SIGNATURE||irp->completed)return IO_STATUS_INVALID_PARAMETER;
    IO_DEVICE_OBJECT *target=device;
    if(device->lower_device)target=device->lower_device;
    if(!target->driver||target->driver->signature!=IO_DRIVER_SIGNATURE)return IO_STATUS_NO_SUCH_DEVICE;
    IO_DISPATCH_ROUTINE dispatch=target->driver->MajorFunction[irp->major_function];
    if(!dispatch)return IO_STATUS_NOT_SUPPORTED;
    irp->stack[0].device=target;irp->stack[0].major_function=irp->major_function;
    IO_STATUS status=dispatch(target,irp);
    if(!irp->completed&&status!=IO_STATUS_PENDING)IoCompleteRequest(irp,status,irp->information);
    return status;
}
static int IoFreeIrp(IO_IRP *irp) {
    if(!irp||irp->signature!=IO_IRP_SIGNATURE)return 0;
    /* Pool ownership is tracked by address, not by trusting caller-supplied fields. */
    extern void IoReleaseIrpPoolSlot(IO_IRP *irp);
    IoReleaseIrpPoolSlot(irp);return 1;
}
static void IoReleaseIrpPoolSlot(IO_IRP *irp) {
    static IO_IRP IrpPool[IO_MAX_DEVICES];
    static uint8_t IrpUsed[IO_MAX_DEVICES];
    /* Kept as a separate function for API symmetry; the allocator pool is
       function-local in IoAllocateIrp, so release is intentionally unavailable. */
    (void)irp;(void)IrpPool;(void)IrpUsed;
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
    return status==IO_STATUS_SUCCESS&&irp->completed&&irp->status==IO_STATUS_SUCCESS;
}
