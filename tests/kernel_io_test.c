#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Permit the host compiler to exercise the lock-protected data paths. */
#define OB_SPIN_WAIT() ((void)0)
#define IO_SPIN_WAIT() ((void)0)
#define SENTINEL_BLOCK_SPIN_WAIT() ((void)0)
#define IO_CURRENT_THREAD_TOKEN() (test_null_thread_token ? NULL : (const void *)&test_thread_token)
#define IO_CURRENT_CPU_TOKEN() ((const void *)&test_cpu_token)
static _Thread_local int test_thread_token;
static _Thread_local int test_cpu_token;
static int test_null_thread_token;
#include "../kernel/iomgr.h"
#include "../kernel/block.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct {
    const char *name;
    int unregister_during_read;
    int unregister_result;
    int callback_result;
} TEST_BLOCK_CONTEXT;

static int test_block_read(void *opaque, uint64_t offset, void *dst, uint64_t length) {
    TEST_BLOCK_CONTEXT *context=(TEST_BLOCK_CONTEXT *)opaque;
    if(context->unregister_during_read)
        context->unregister_result=SentinelBlockUnregister(context->name,context);
    if(dst&&length)memset(dst,(int)(offset&0xff), (size_t)length);
    return context->callback_result;
}
static int test_block_write(void *opaque,uint64_t offset,const void *src,uint64_t length) {
    (void)opaque;(void)offset;(void)src;(void)length;
    return 0;
}

static IO_STATUS test_io_success(IO_DEVICE_OBJECT *device,IO_IRP *irp) {
    (void)device;
    irp->information=irp->major_function==IRP_MJ_WRITE
        ? irp->input_length : irp->output_length+1u;
    return IO_STATUS_SUCCESS;
}
static IO_STATUS test_io_pending(IO_DEVICE_OBJECT *device,IO_IRP *irp) {
    (void)device;(void)irp;return IO_STATUS_PENDING;
}
static IO_DEVICE_OBJECT *forward_lower,*forward_upper;
static volatile uint32_t forward_dispatch_count;
static int forward_pending;
static IO_STATUS test_io_forward(IO_DEVICE_OBJECT *device,IO_IRP *irp) {
    if(device==forward_upper){
        __atomic_add_fetch(&forward_dispatch_count,1u,__ATOMIC_RELAXED);
        return IoCallDriver(forward_lower,irp);
    }
    if(forward_pending)return IO_STATUS_PENDING;
    irp->information=irp->output_length;
    return IO_STATUS_SUCCESS;
}
static volatile uint32_t completion_count;
static void test_io_completion(IO_IRP *irp,void *context) {
    (void)irp;(void)context;__atomic_add_fetch(&completion_count,1u,__ATOMIC_RELAXED);
}
static void *complete_thread(void *opaque) {
    IO_IRP *irp=(IO_IRP *)opaque;
    (void)IoCompleteRequest(irp,IO_STATUS_SUCCESS,2);
    return NULL;
}

typedef struct {
    unsigned int id;
    int result;
} REGISTER_THREAD_ARGUMENT;
static volatile uint32_t register_ready;
static volatile int register_go;
static TEST_BLOCK_CONTEXT concurrent_contexts[2];
static void *register_thread(void *opaque) {
    REGISTER_THREAD_ARGUMENT *argument=(REGISTER_THREAD_ARGUMENT *)opaque;
    SENTINEL_BLOCK_DEVICE device={0};
    device.size=sizeof(device);device.abi_version=SENTINEL_BLOCK_ABI_VERSION;
    device.name[0]='r';device.name[1]='a';device.name[2]='c';device.name[3]='e';device.name[4]='0';
    device.sector_size=512;device.sector_count=2;
    device.context=&concurrent_contexts[argument->id];device.read=test_block_read;device.write=test_block_write;
    __atomic_add_fetch(&register_ready,1u,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&register_go,__ATOMIC_ACQUIRE))sched_yield();
    argument->result=SentinelBlockRegister(&device);
    return NULL;
}

int main(void) {
    IoInitialize();
    CHECK(!IoSetMajorFunction((IO_DRIVER_OBJECT *)(uintptr_t)1,IRP_MJ_READ,test_io_success));
    char too_long_driver[IO_DEVICE_NAME_MAX+1];memset(too_long_driver,'d',sizeof(too_long_driver));
    CHECK(!IoCreateDriverObject(too_long_driver));
    IO_DRIVER_OBJECT *driver=IoCreateDriverObject("test-driver");
    CHECK(driver!=NULL);
    CHECK(IoCreateDriverObject("test-driver")==NULL);
    CHECK(IoSetMajorFunction(driver,IRP_MJ_READ,test_io_success));
    CHECK(IoSetMajorFunction(driver,IRP_MJ_WRITE,test_io_success));
    CHECK(IoSetMajorFunction(driver,IRP_MJ_DEVICE_CONTROL,test_io_pending));
    IO_DEVICE_OBJECT *device=NULL,*upper=NULL;
    CHECK(IoCreateDevice(driver,"\\Device\\TestIo",1,0,0,&device)==IO_STATUS_SUCCESS);
    CHECK(IoCreateDevice(driver,"\\Device\\TestIo",1,0,0,&upper)==IO_STATUS_ACCESS_DENIED);
    CHECK(upper==NULL);
    CHECK(IoCreateDevice(driver,"\\Device\\UpperIo",1,0,0,&upper)==IO_STATUS_SUCCESS);
    CHECK(IoAttachDeviceToDeviceStack(upper,device)==device);
    CHECK(IoAttachDeviceToDeviceStack(device,upper)==NULL);
    CHECK(IoGetDeviceByName("\\Device\\TestIo")==device);
    forward_lower=device;forward_upper=upper;
    CHECK(IoSetMajorFunction(driver,IRP_MJ_QUERY_INFORMATION,test_io_forward));
    uint8_t forward_buffer[4]={0};
    IO_IRP *forward_irp=IoAllocateIrp(IRP_MJ_QUERY_INFORMATION,forward_buffer,0,4);
    test_null_thread_token=1; /* boot-time dispatch before the scheduler has a current thread */
    CHECK(forward_irp!=NULL&&IoCallDriver(upper,forward_irp)==IO_STATUS_SUCCESS);
    test_null_thread_token=0;
    CHECK(forward_irp->completed&&forward_irp->status==IO_STATUS_SUCCESS&&forward_irp->information==4);
    CHECK(__atomic_load_n(&forward_dispatch_count,__ATOMIC_RELAXED)==1);
    CHECK(forward_irp->dispatch_depth==0&&!forward_irp->in_flight&&!forward_irp->dispatch_owner);
    CHECK(IoFreeIrp(forward_irp));
    IO_IRP *forward_pending_irp=IoAllocateIrp(IRP_MJ_QUERY_INFORMATION,forward_buffer,0,4);
    CHECK(forward_pending_irp!=NULL);
    forward_pending_irp->completion=test_io_completion;
    forward_pending=1;
    CHECK(IoCallDriver(upper,forward_pending_irp)==IO_STATUS_PENDING);
    forward_pending=0;
    CHECK(forward_pending_irp->pending&&!forward_pending_irp->in_flight&&!forward_pending_irp->completed);
    CHECK(!IoFreeIrp(forward_pending_irp));
    CHECK(IoCompleteRequest(forward_pending_irp,IO_STATUS_SUCCESS,4));
    CHECK(!IoCompleteRequest(forward_pending_irp,IO_STATUS_SUCCESS,4));
    CHECK(__atomic_load_n(&completion_count,__ATOMIC_RELAXED)==1);
    CHECK(IoFreeIrp(forward_pending_irp));
    IO_IRP *invalid_device_irp=IoAllocateIrp(IRP_MJ_READ,NULL,0,0);
    CHECK(invalid_device_irp!=NULL);
    CHECK(IoCallDriver((IO_DEVICE_OBJECT *)(uintptr_t)1,invalid_device_irp)==IO_STATUS_INVALID_PARAMETER);
    CHECK(IoFreeIrp(invalid_device_irp));
    CHECK(IoAllocateIrp((uint8_t)0xff,NULL,0,0)==NULL);

    uint8_t buffer[IO_IRP_BUFFER_MAX]={0};
    CHECK(IoAllocateIrp(IRP_MJ_READ,buffer,200,100)==NULL);
    IO_IRP *read_irp=IoAllocateIrp(IRP_MJ_READ,buffer,2,4);
    CHECK(read_irp!=NULL);
    CHECK(IoCallDriver(device,read_irp)==IO_STATUS_SUCCESS);
    CHECK(read_irp->completed&&read_irp->status==IO_STATUS_INVALID_PARAMETER&&read_irp->information==0);
    CHECK(IoFreeIrp(read_irp));

    IO_IRP *write_irp=IoAllocateIrp(IRP_MJ_WRITE,buffer,4,0);
    CHECK(write_irp!=NULL&&IoCallDriver(device,write_irp)==IO_STATUS_SUCCESS);
    CHECK(write_irp->completed&&write_irp->status==IO_STATUS_SUCCESS&&write_irp->information==4);
    CHECK(IoFreeIrp(write_irp));

    IO_IRP *pending_irp=IoAllocateIrp(IRP_MJ_DEVICE_CONTROL,buffer,0,4);
    CHECK(pending_irp!=NULL);
    pending_irp->completion=test_io_completion;
    CHECK(IoCallDriver(device,pending_irp)==IO_STATUS_PENDING);
    CHECK(pending_irp->pending&&!pending_irp->completed);
    CHECK(!IoFreeIrp(pending_irp));
    pthread_t completions[2];
    CHECK(pthread_create(&completions[0],NULL,complete_thread,pending_irp)==0);
    CHECK(pthread_create(&completions[1],NULL,complete_thread,pending_irp)==0);
    CHECK(pthread_join(completions[0],NULL)==0&&pthread_join(completions[1],NULL)==0);
    CHECK(pending_irp->completed&&!pending_irp->pending&&pending_irp->status==IO_STATUS_SUCCESS);
    CHECK(__atomic_load_n(&completion_count,__ATOMIC_RELAXED)==2);
    CHECK(IoFreeIrp(pending_irp));
    CHECK(!IoFreeIrp(pending_irp));

    TEST_BLOCK_CONTEXT context={"disk-test",1,0,0};
    SENTINEL_BLOCK_DEVICE block={0};
    block.size=sizeof(block);block.abi_version=SENTINEL_BLOCK_ABI_VERSION;
    memcpy(block.name,"disk-test",10);block.sector_size=512;block.sector_count=4;
    block.context=&context;block.read=test_block_read;block.write=test_block_write;
    SENTINEL_BLOCK_DEVICE invalid=block;
    invalid.sector_count=UINT64_MAX;
    CHECK(SentinelBlockRegister(&invalid)==-1);
    invalid=block;
    memset(invalid.name,'x',sizeof(invalid.name));
    CHECK(SentinelBlockRegister(&invalid)==-1);
    CHECK(SentinelBlockRegister(&block)==0);
    CHECK(SentinelBlockRegister(&block)==-2);
    SENTINEL_BLOCK_DEVICE *registered=SentinelBlockFind("disk-test");
    CHECK(registered!=NULL);
    uint8_t data[4]={0};
    CHECK(SentinelBlockReadDevice(registered,2044,data,sizeof(data))==0);
    CHECK(data[0]==0xfc&&data[3]==0xfc);
    CHECK(SentinelBlockReadDevice(registered,2047,data,2)<0);
    CHECK(SentinelBlockReadDevice(registered,UINT64_MAX,data,1)<0);
    context.callback_result=-7;
    CHECK(SentinelBlockReadDevice(registered,0,data,1)==-7);
    context.callback_result=0;
    CHECK(SentinelBlockUnregister("disk-test",&invalid)==-2);
    CHECK(SentinelBlockReadDevice(registered,0,data,1)==0);
    CHECK(context.unregister_result==-3);
    CHECK(SentinelBlockUnregister("disk-test",&context)==0);
    CHECK(SentinelBlockReadDevice(registered,0,data,1)<0);
    CHECK(SentinelBlockFind("disk-test")==NULL);

    pthread_t registrars[2];REGISTER_THREAD_ARGUMENT arguments[2]={{0,-99},{1,-99}};
    concurrent_contexts[0]=(TEST_BLOCK_CONTEXT){"race0",0,0,0};
    concurrent_contexts[1]=(TEST_BLOCK_CONTEXT){"race0",0,0,0};
    CHECK(pthread_create(&registrars[0],NULL,register_thread,&arguments[0])==0);
    CHECK(pthread_create(&registrars[1],NULL,register_thread,&arguments[1])==0);
    while(__atomic_load_n(&register_ready,__ATOMIC_ACQUIRE)!=2)sched_yield();
    __atomic_store_n(&register_go,1,__ATOMIC_RELEASE);
    CHECK(pthread_join(registrars[0],NULL)==0&&pthread_join(registrars[1],NULL)==0);
    CHECK((arguments[0].result==0&&arguments[1].result==-2)||
          (arguments[1].result==0&&arguments[0].result==-2));
    unsigned int winner=arguments[0].result==0?0:1;
    CHECK(SentinelBlockUnregister("race0",&concurrent_contexts[winner])==0);

    puts("kernel I/O manager and block-device contracts: PASS");
    return 0;
}
