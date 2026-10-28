#pragma once
/*
 * SentinelOS Object Manager (Ob).
 * Fixed-capacity kernel namespace and per-owner handle tables; no dynamic allocation
 * is required during early boot. This is an NT-inspired foundation, not an NT clone.
 */
#include <stdint.h>
#include <stddef.h>

#define OB_MAX_OBJECTS 64
#define OB_MAX_HANDLES 256
#define OB_NAME_MAX 64
#define OB_BODY_MAX 128

#define OB_ACCESS_READ       0x00000001u
#define OB_ACCESS_WRITE      0x00000002u
#define OB_ACCESS_EXECUTE    0x00000004u
#define OB_ACCESS_DUP_HANDLE 0x00000008u
#define OB_ACCESS_ALL        0x0000FFFFu

typedef enum {
    ObTypeNone = 0, ObTypeDirectory, ObTypeFile, ObTypeDevice, ObTypeEvent,
    ObTypeMutant, ObTypeSemaphore, ObTypeProcess, ObTypeThread, ObTypeSection,
    ObTypeSymbolicLink, ObTypeKey, ObTypeTimer
} OB_TYPE;

typedef uintptr_t OB_HANDLE;
typedef struct _OB_OBJECT {
    uint32_t signature;
    uint16_t type;
    uint16_t flags;
    volatile uint32_t references;
    uint32_t granted_access;
    char name[OB_NAME_MAX];
    uint8_t body[OB_BODY_MAX];
    uint8_t used;
    uint8_t named;
    uint16_t reserved;
} OB_OBJECT;

typedef struct {
    uintptr_t owner;
    OB_OBJECT *object;
    uint32_t access;
    uint32_t generation;
    uint8_t used;
} OB_HANDLE_ENTRY;

#define OB_SIGNATURE 0x4F424A53u /* SJBO */
#define OB_HANDLE_INDEX_MASK 0xFFFFu

static OB_OBJECT ObObjects[OB_MAX_OBJECTS];
static OB_HANDLE_ENTRY ObHandles[OB_MAX_HANDLES];
static volatile uint32_t ObLockWord;
static uint32_t ObNextGeneration = 1;
static uint32_t ObObjectCount, ObHandleCount;

static void ObLock(void) {
    while (__atomic_exchange_n(&ObLockWord, 1u, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(&ObLockWord, __ATOMIC_RELAXED)) __asm__ volatile("pause");
}
static void ObUnlock(void) { __atomic_store_n(&ObLockWord, 0u, __ATOMIC_RELEASE); }
static size_t ObStringLength(const char *s) { size_t n=0; if(!s)return 0; while(s[n])n++; return n; }
static int ObStringEqual(const char *a,const char *b) {
    if(!a||!b)return 0;
    while(*a&&*b&&*a==*b){a++;b++;}
    return *a==*b;
}
static void ObStringCopy(char *d,const char *s,size_t cap) {
    size_t i=0;
    if(!cap)return;
    if(s)while(s[i]&&i+1<cap){d[i]=s[i];i++;}
    d[i]=0;
}
static void ObReferenceObject(OB_OBJECT *object) {
    if(object&&object->used&&object->signature==OB_SIGNATURE)
        __atomic_add_fetch(&object->references,1u,__ATOMIC_RELAXED);
}
static void ObDereferenceObject(OB_OBJECT *object) {
    if(!object||!object->used||object->signature!=OB_SIGNATURE)return;
    if(__atomic_sub_fetch(&object->references,1u,__ATOMIC_ACQ_REL)==0) {
        ObLock();
        if(object->used&&object->references==0) {
            object->used=0; object->signature=0; object->name[0]=0;
            if(ObObjectCount)ObObjectCount--;
        }
        ObUnlock();
    }
}
static OB_OBJECT *ObFindObjectLocked(const char *name,OB_TYPE type) {
    for(unsigned i=0;i<OB_MAX_OBJECTS;i++)
        if(ObObjects[i].used&&ObObjects[i].named&&
           (!type||ObObjects[i].type==(uint16_t)type)&&ObStringEqual(ObObjects[i].name,name))
            return &ObObjects[i];
    return NULL;
}
static int ObAllocateHandleLocked(uintptr_t owner,OB_OBJECT *object,uint32_t access,OB_HANDLE *out) {
    if(!out||!object)return 0;
    for(uint32_t i=0;i<OB_MAX_HANDLES;i++)if(!ObHandles[i].used) {
        uint32_t generation=++ObNextGeneration;
        if(!generation)generation=++ObNextGeneration;
        ObHandles[i].owner=owner; ObHandles[i].object=object; ObHandles[i].access=access;
        ObHandles[i].generation=generation; ObHandles[i].used=1;
        ObReferenceObject(object); ObHandleCount++;
        *out=((uintptr_t)generation<<16)|(uintptr_t)(i+1u);
        return 1;
    }
    return 0;
}
static OB_HANDLE_ENTRY *ObFindHandleLocked(uintptr_t owner,OB_HANDLE handle) {
    uint32_t slot=(uint32_t)(handle&OB_HANDLE_INDEX_MASK);
    uint32_t generation=(uint32_t)(handle>>16);
    if(!slot||slot>OB_MAX_HANDLES||!generation)return NULL;
    OB_HANDLE_ENTRY *e=&ObHandles[slot-1u];
    return e->used&&e->owner==owner&&e->generation==generation?e:NULL;
}
static void ObInitialize(void) {
    ObLock();
    for(unsigned i=0;i<OB_MAX_OBJECTS;i++) {
        ObObjects[i].signature=0; ObObjects[i].used=0; ObObjects[i].named=0;
        ObObjects[i].references=0; ObObjects[i].name[0]=0;
    }
    for(unsigned i=0;i<OB_MAX_HANDLES;i++) { ObHandles[i].used=0; ObHandles[i].object=NULL; }
    ObObjectCount=0; ObHandleCount=0; ObNextGeneration=1;
    ObUnlock();
}
/* Create returns one caller reference and one handle reference. Named objects
 * also hold a namespace reference until ObDeleteNamedObject is called. */
static int ObCreateObject(OB_TYPE type,const char *name,size_t body_size,
                          uintptr_t owner,uint32_t access,OB_HANDLE *handle,void **body_out) {
    if(type==ObTypeNone||body_size>OB_BODY_MAX||(!handle&&!body_out))return 0;
    size_t name_len=ObStringLength(name);
    if(name&&name_len>=OB_NAME_MAX)return 0;
    ObLock();
    if(name&&name[0]&&ObFindObjectLocked(name,0)){ObUnlock();return 0;}
    OB_OBJECT *o=NULL;
    for(unsigned i=0;i<OB_MAX_OBJECTS;i++)if(!ObObjects[i].used){o=&ObObjects[i];break;}
    if(!o){ObUnlock();return 0;}
    o->signature=OB_SIGNATURE; o->type=(uint16_t)type; o->flags=0;
    o->references=1; o->granted_access=access; o->used=1; o->named=(name&&name[0])?1:0;
    ObStringCopy(o->name,name,OB_NAME_MAX);
    for(unsigned i=0;i<OB_BODY_MAX;i++)o->body[i]=0;
    ObObjectCount++;
    if(o->named)ObReferenceObject(o); /* namespace ownership */
    int ok=1;
    if(handle)ok=ObAllocateHandleLocked(owner,o,access,handle);
    if(body_out)*body_out=o->body;
    if(!ok) {
        if(o->named){o->named=0;__atomic_sub_fetch(&o->references,1u,__ATOMIC_RELAXED);}
        __atomic_sub_fetch(&o->references,1u,__ATOMIC_RELAXED);
        if(o->references==0){o->used=0;o->signature=0;ObObjectCount--;}
        if(body_out)*body_out=NULL;
    }
    ObUnlock();
    return ok;
}
static int ObOpenObjectByName(uintptr_t owner,const char *name,OB_TYPE type,
                              uint32_t desired_access,OB_HANDLE *handle) {
    if(!name||!handle)return 0;
    ObLock();
    OB_OBJECT *o=ObFindObjectLocked(name,type);
    if(!o||(desired_access&~o->granted_access)){ObUnlock();return 0;}
    int ok=ObAllocateHandleLocked(owner,o,desired_access,handle);
    ObUnlock(); return ok;
}
static OB_OBJECT *ObReferenceObjectByHandle(uintptr_t owner,OB_HANDLE handle,
                                            OB_TYPE expected_type,uint32_t desired_access) {
    ObLock();
    OB_HANDLE_ENTRY *e=ObFindHandleLocked(owner,handle);
    OB_OBJECT *o=NULL;
    if(e&&e->object&&(!expected_type||e->object->type==(uint16_t)expected_type)&&
       !(desired_access&~e->access)&&e->object->used) {
        o=e->object; ObReferenceObject(o);
    }
    ObUnlock(); return o;
}
static int ObCloseHandle(uintptr_t owner,OB_HANDLE handle) {
    OB_OBJECT *release=NULL;
    ObLock();
    OB_HANDLE_ENTRY *e=ObFindHandleLocked(owner,handle);
    if(e){release=e->object;e->used=0;e->object=NULL;e->owner=0;e->access=0;
        if(ObHandleCount)ObHandleCount--;}
    ObUnlock();
    if(release)ObDereferenceObject(release);
    return release!=NULL;
}
static int ObDeleteNamedObject(const char *name,OB_TYPE type) {
    OB_OBJECT *o;
    ObLock(); o=ObFindObjectLocked(name,type);
    if(!o){ObUnlock();return 0;}
    o->named=0;o->name[0]=0;
    ObUnlock(); ObDereferenceObject(o); return 1;
}
static uint32_t ObGetObjectCount(void){return ObObjectCount;}
static uint32_t ObGetHandleCount(void){return ObHandleCount;}

/* Boot-time invariant test: namespace reference, access validation, per-owner
 * handles, explicit references, close, and namespace deletion. */
static int ObSelfTest(void) {
    OB_HANDLE kernel_handle=0,user_handle=0;
    void *body=NULL;
    if(!ObCreateObject(ObTypeEvent,"\\BaseNamedObjects\\ObSelfTest",sizeof(uint64_t),
                       0,OB_ACCESS_READ|OB_ACCESS_WRITE,&kernel_handle,&body))return 0;
    if(!body||!ObStringEqual(ObFindObjectLocked("\\BaseNamedObjects\\ObSelfTest",ObTypeEvent)->name,
                              "\\BaseNamedObjects\\ObSelfTest")) {
        ObCloseHandle(0,kernel_handle);ObDeleteNamedObject("\\BaseNamedObjects\\ObSelfTest",ObTypeEvent);return 0;
    }
    OB_OBJECT *object=ObReferenceObjectByHandle(0,kernel_handle,ObTypeEvent,OB_ACCESS_READ);
    int ok=object!=NULL;
    if(object)ObDereferenceObject(object);
    if(ObReferenceObjectByHandle(0,kernel_handle,ObTypeEvent,OB_ACCESS_EXECUTE)!=NULL)ok=0;
    if(!ObOpenObjectByName(7,"\\BaseNamedObjects\\ObSelfTest",ObTypeEvent,OB_ACCESS_READ,&user_handle))ok=0;
    if(ObReferenceObjectByHandle(8,user_handle,ObTypeEvent,OB_ACCESS_READ)!=NULL)ok=0;
    if(user_handle)ObCloseHandle(7,user_handle);
    ObDeleteNamedObject("\\BaseNamedObjects\\ObSelfTest",ObTypeEvent);
    if(!ObCloseHandle(0,kernel_handle))ok=0;
    return ok;
}
