#pragma once
#include "ui.h"

/* Standalone popup handling (floating, layout-isolated). */
void ui_open_popup(struct ui_ctx_t *ui, const char *id);
void ui_close_popup(struct ui_ctx_t *ui);
int ui_begin_popup(struct ui_ctx_t *ui, const char *id);
void ui_end_popup(struct ui_ctx_t *ui);
