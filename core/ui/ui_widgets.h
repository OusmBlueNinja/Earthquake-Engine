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
void ui_text(ui_ctx_t *ui, const char *text, uint32_t font_id);

int ui_checkbox(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value);

int ui_slider_float(ui_ctx_t *ui, const char *label, uint32_t font_id, float *value, float minv, float maxv);
int ui_slider_int(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value, int minv, int maxv);

int ui_toggle(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value);
int ui_radio(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value, int my_value);

void ui_progress_bar(ui_ctx_t *ui, float t01, const char *label, uint32_t font_id);

void ui_separator(ui_ctx_t *ui);
void ui_separator_text(ui_ctx_t *ui, const char *text, uint32_t font_id);

int ui_input_text(ui_ctx_t *ui, const char *label, uint32_t font_id, char *buf, int buf_cap);

int ui_selectable(ui_ctx_t *ui, const char *label, uint32_t font_id, int *selected);

int ui_combo(ui_ctx_t *ui, const char *label, uint32_t font_id, const char **items, int items_count, int *current);

int ui_collapsing_header(ui_ctx_t *ui, const char *label, uint32_t font_id, int default_open);

int ui_begin_child(ui_ctx_t *ui, const char *id, ui_vec2_t size);
void ui_end_child(ui_ctx_t *ui);

void ui_open_popup(ui_ctx_t *ui, const char *id);
int ui_begin_popup(ui_ctx_t *ui, const char *id);
void ui_end_popup(ui_ctx_t *ui);

int ui_begin_context_menu(ui_ctx_t *ui, const char *id);
