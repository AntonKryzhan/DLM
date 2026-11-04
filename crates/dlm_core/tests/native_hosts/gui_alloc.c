#include <stdlib.h>
#include <stdatomic.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
static _Atomic int fail_next;
void *dn_test_gui_malloc(size_t size){if(atomic_exchange_explicit(&fail_next,0,memory_order_relaxed))return NULL;return malloc(size);}
EXPORT void dlm_test_fail_next_gui_allocation(void){atomic_store_explicit(&fail_next,1,memory_order_relaxed);}
