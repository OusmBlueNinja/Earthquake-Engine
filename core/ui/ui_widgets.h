#pragma once
#include "ui.h"

typedef struct ui_widget_result_t
{
    uint8_t hovered;
    uint8_t held;
    uint8_t pressed;
    uint8_t released;
} ui_widget_result_t;

ui_vec4_t ui_next_rect(ui_ctx_t *ui);

ui_widget_result_t ui_button_ex(ui_ctx_t *ui, uint32_t id, ui_vec4_t rect);
int ui_button(ui_ctx_t *ui, const char *label, uint32_t font_id);

void ui_label(ui_ctx_t *ui, const char *text, uint32_t font_id);

int ui_checkbox(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value);

int ui_slider_float(ui_ctx_t *ui, const char *label, uint32_t font_id, float *value, float minv, float maxv);
