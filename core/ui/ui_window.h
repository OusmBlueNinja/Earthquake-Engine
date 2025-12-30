#pragma once
#include "ui.h"

typedef enum ui_window_flags_t
{
    UI_WIN_NONE = 0,
    UI_WIN_NO_TITLEBAR = 1 << 0,
    UI_WIN_NO_MOVE = 1 << 1,
    UI_WIN_NO_RESIZE = 1 << 2,
    UI_WIN_NO_AUTO_LAYOUT = 1 << 3,
    UI_WIN_NO_CLAMP = 1 << 4
} ui_window_flags_t;

typedef enum ui_dock_dir_t
{
    UI_DOCK_NONE = 0,
    UI_DOCK_LEFT,
    UI_DOCK_RIGHT,
    UI_DOCK_TOP,
    UI_DOCK_BOTTOM,
    UI_DOCK_CENTER
} ui_dock_dir_t;

void ui_window_begin_frame(struct ui_ctx_t *ui);
void ui_window_end_frame(struct ui_ctx_t *ui);

void ui_dockspace(struct ui_ctx_t *ui, const char *id, ui_vec4_t rect);

// Docking/Window layout persistence (simple text format).
int ui_dock_load_layout_file(struct ui_ctx_t *ui, const char *path);
int ui_dock_save_layout_file(struct ui_ctx_t *ui, const char *path);

void ui_window_set_next_pos(struct ui_ctx_t *ui, ui_vec2_t pos);
void ui_window_set_next_size(struct ui_ctx_t *ui, ui_vec2_t size);
/* ImGui-friendly aliases */
void ui_set_next_window_pos(struct ui_ctx_t *ui, ui_vec2_t pos);
void ui_set_next_window_size(struct ui_ctx_t *ui, ui_vec2_t size);

int ui_window_begin(struct ui_ctx_t *ui, const char *title, ui_window_flags_t flags);
void ui_window_end(struct ui_ctx_t *ui);
int ui_begin_window(struct ui_ctx_t *ui, const char *title, ui_window_flags_t flags);
void ui_end_window(struct ui_ctx_t *ui);

int ui_begin_child(struct ui_ctx_t *ui, const char *id, ui_vec2_t size);
void ui_end_child(struct ui_ctx_t *ui);

ui_vec4_t ui_window_rect(struct ui_ctx_t *ui);
ui_vec4_t ui_window_content_rect(struct ui_ctx_t *ui);
uint32_t ui_window_hovered_id(struct ui_ctx_t *ui);
