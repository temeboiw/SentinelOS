/* Fallback personality for images using the newer MSVC C++ EH ABI.
 * Returning ExceptionContinueSearch (1) leaves the exception for the next
 * registered handler; this does not decode C++ EH4 metadata. */
__attribute__((dllexport)) int __CxxFrameHandler4(void *record,void *frame,void *context,void *dispatcher) {
 (void)record;(void)frame;(void)context;(void)dispatcher;return 1;
}
