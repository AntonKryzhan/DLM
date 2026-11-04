#ifndef DLM_GUI_ALLOCATION_FIXTURE_H
#define DLM_GUI_ALLOCATION_FIXTURE_H
#include <stdlib.h>
void *dn_test_gui_malloc(size_t);
#define malloc dn_test_gui_malloc
#endif
