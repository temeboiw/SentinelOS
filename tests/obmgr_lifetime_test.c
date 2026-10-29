#include <stdio.h>

/* The host test is single-threaded; production keeps the x86 PAUSE hint. */
#define OB_SPIN_WAIT() ((void)0)
#include "../kernel/obmgr.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void) {
    ObInitialize();
    CHECK(ObGetObjectCount() == 0);
    CHECK(ObGetHandleCount() == 0);
    CHECK(ObSelfTest());
    CHECK(ObGetObjectCount() == 0);
    CHECK(ObGetHandleCount() == 0);

    /* A handle-only create owns exactly the handle reference, not a hidden
       creator reference that the caller cannot release. */
    OB_HANDLE handle = 0;
    CHECK(ObCreateObject(ObTypeEvent, NULL, 0, 17, OB_ACCESS_READ,
                         &handle, NULL));
    CHECK(ObGetObjectCount() == 1 && ObGetHandleCount() == 1);
    OB_OBJECT *object = ObReferenceObjectByHandle(17, handle,
                                                   ObTypeEvent, OB_ACCESS_READ);
    CHECK(object != NULL && object->references == 2);
    ObDereferenceObject(object);
    CHECK(!ObReferenceObjectByHandle(18, handle, ObTypeEvent, OB_ACCESS_READ));
    CHECK(!ObReferenceObjectByHandle(17, handle, ObTypeEvent, OB_ACCESS_WRITE));
    CHECK(!ObCloseHandle(18, handle));
    CHECK(ObCloseHandle(17, handle));
    CHECK(!ObCloseHandle(17, handle));
    CHECK(!ObReferenceObjectByHandle(17, handle, ObTypeEvent, OB_ACCESS_READ));
    CHECK(ObGetObjectCount() == 0 && ObGetHandleCount() == 0);

    /* Body-only creation returns one caller reference; a repeated dereference
       after destruction must not underflow the reference count. */
    void *body = NULL;
    CHECK(ObCreateObject(ObTypeFile, NULL, 0, 0, OB_ACCESS_READ, NULL, &body));
    object = ObObjectFromBody(body);
    CHECK(object != NULL && object->references == 1);
    ObDereferenceObject(object);
    ObDereferenceObject(object);
    CHECK(ObGetObjectCount() == 0 && ObGetHandleCount() == 0);

    /* Fill the handle table on one named object so a second named,
       handle-only create fails after taking only its namespace reference. */
    OB_HANDLE root_handle = 0;
    void *root_body = NULL;
    const char *root_name = "\\BaseNamedObjects\\ObHandleCapacity";
    CHECK(ObCreateObject(ObTypeEvent, root_name, 0, 23, OB_ACCESS_READ,
                         &root_handle, &root_body));
    OB_HANDLE extra_handles[OB_MAX_HANDLES - 1];
    for (unsigned i = 0; i < OB_MAX_HANDLES - 1; ++i)
        CHECK(ObOpenObjectByName(23, root_name, ObTypeEvent,
                                 OB_ACCESS_READ, &extra_handles[i]));
    CHECK(ObGetHandleCount() == OB_MAX_HANDLES);
    CHECK(!ObCreateObject(ObTypeEvent,
                          "\\BaseNamedObjects\\ObFailedHandleCapacity", 0,
                          24, OB_ACCESS_READ, &handle, NULL));
    CHECK(ObGetObjectCount() == 1 && ObGetHandleCount() == OB_MAX_HANDLES);
    for (unsigned i = 0; i < OB_MAX_HANDLES - 1; ++i)
        CHECK(ObCloseHandle(23, extra_handles[i]));
    CHECK(ObCloseHandle(23, root_handle));
    CHECK(ObDeleteNamedObject(root_name, ObTypeEvent));
    ObDereferenceObject(ObObjectFromBody(root_body));
    CHECK(ObGetObjectCount() == 0 && ObGetHandleCount() == 0);

    puts("Object Manager ownership, access, stale-handle, and lifetime checks: PASS");
    return 0;
}
