#ifndef CHORDLIKE_LIVE_DLM_UI_H
#define CHORDLIKE_LIVE_DLM_UI_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Compiled directly from src/live_editor.dlm by DLM v0.133.0. */
void *live_ui_create(size_t parent);
size_t live_ui_handle(void *editor);
void live_ui_resize(void *editor, size_t width, size_t height);
void live_ui_draw(void *editor, double *normalized_values, size_t count);
int32_t live_ui_hit(size_t x, size_t y, size_t width, size_t height);
double live_ui_slider_value(size_t x, size_t width, size_t parameter_index);
void live_ui_destroy(void *editor);
#ifdef __cplusplus
}
#endif
#endif
