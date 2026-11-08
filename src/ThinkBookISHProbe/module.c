// No custom module-level callbacks: libkmod must not call its own wrappers.
#include <mach/mach_types.h>
#include <libkern/OSKextLib.h>
extern kern_return_t _start(kmod_info_t*, void*);
extern kern_return_t _stop(kmod_info_t*, void*);
KMOD_EXPLICIT_DECL(local.ThinkBookISHProbe,"0.5.3",_start,_stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
