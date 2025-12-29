#include "ui_demo.h"
#include "ui_widgets.h"
#include "ui_window.h"
#include "ui_popup.h"
#include <string.h>
#include <stdio.h>

void ui_render_demo_window(ui_ctx_t *ui)
{
    if (!ui)
        return;

    static int init = 0;
    static int counter = 0;
    static int checkbox = 1;
    static int radio_choice = 0;
    static int toggle = 1;
    static float sliderf = 0.35f;
    static int slideri = 3;
    static float dragf = 0.5f;
    static int dragi = 5;
    static int combo_idx = 0;
    static char input_buf[128] = "Edit me";
    static float progress = 0.0f;
    static int child_selection = -1;
    static int show_pop = 0;
    static float child_height = 160.0f;

    if (!init)
    {
        ui_set_next_window_pos(ui, ui_v2(32.0f, 32.0f));
        ui_set_next_window_size(ui, ui_v2(460.0f, 620.0f));
        init = 1;
    }

    if (!ui_window_begin(ui, "UI Demo", UI_WIN_NONE))
        return;

    const ui_io_t *io = ui_io(ui);

    ui_separator_text(ui, "Basics", 0);
    ui_label(ui, "Buttons", 0);
    if (ui_button(ui, "Button", 0))
        counter++;
    ui_same_line(ui, 6.0f);
    ui_label(ui, "Click count:", 0);
    ui_same_line(ui, 6.0f);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d", counter);
    ui_label(ui, cnt, 0);

    ui_checkbox(ui, "Checkbox", 0, &checkbox);
    ui_toggle(ui, "Toggle", 0, &toggle);
    ui_radio(ui, "Radio A", 0, &radio_choice, 0);
    ui_radio(ui, "Radio B", 0, &radio_choice, 1);

    ui_separator_text(ui, "Sliders & Drags", 0);
    ui_slider_float(ui, "Float slider", 0, &sliderf, 0.0f, 1.0f);
    ui_slider_int(ui, "Int slider", 0, &slideri, 0, 10);
    ui_drag_float(ui, "Drag float (speed 0.01)", 0, &dragf, 0.01f, 0.0f, 1.0f);
    ui_drag_int(ui, "Drag int (speed 1)", 0, &dragi, 1, 0, 20);

    ui_separator_text(ui, "Inputs", 0);
    ui_input_text(ui, "Text", 0, input_buf, (int)sizeof(input_buf));

    const char *combo_items[] = {"Apple", "Banana", "Cherry", "Dragonfruit", "Elderberry"};
    ui_combo(ui, "Combo", 0, combo_items, (int)(sizeof(combo_items) / sizeof(combo_items[0])), &combo_idx);

    progress += ui->delta_time * 0.35f;
    if (progress > 1.0f)
        progress -= 1.0f;
    ui_progress_bar(ui, progress, "Progress", 0);

    ui_separator_text(ui, "Child & List", 0);
    ui_drag_float(ui, "Child height", 0, &child_height, 0.25f, 80.0f, 300.0f);
    ui_set_next_item_height(ui, child_height);
    ui_set_next_item_width(ui, -1.0f);
    if (ui_begin_child(ui, "DemoChild", ui_v2(0.0f, child_height)))
    {
        for (int i = 0; i < 30; ++i)
        {
            char lbl[32];
            snprintf(lbl, sizeof(lbl), "Item %02d", i);
            int sel = (child_selection == i) ? 1 : 0;
            if (ui_selectable(ui, lbl, 0, &sel))
                child_selection = sel ? i : -1;
        }
    }
    ui_end_child(ui);

    ui_separator_text(ui, "Popups", 0);
    if (ui_button(ui, "Open Popup", 0))
    {
        ui_open_popup(ui, "DemoPopup");
        show_pop = 1;
    }
    int popup_open = show_pop && ui_begin_popup(ui, "DemoPopup");
    if (popup_open)
    {
        ui_label(ui, "Right-click context and popups are supported.", 0);
        ui_label(ui, "Click elsewhere to close.", 0);
        if (ui_button(ui, "Close", 0))
        {
            show_pop = 0;
        }
        ui_end_popup(ui);
    }
    else if (show_pop)
    {
        show_pop = 0;
    }

    ui_label(ui, "Right click the line below for a context menu.", 0);
    ui_begin_context_menu(ui, "DemoCtx");
    ui_selectable(ui, "Context Action 1", 0, 0);
    ui_selectable(ui, "Context Action 2", 0, 0);
    ui_end_popup(ui);

    ui_separator_text(ui, "IO / State", 0);
    char buf[256];
    snprintf(buf, sizeof(buf), "Mouse: pos(%.1f, %.1f) delta(%.2f, %.2f) scroll(%.1f, %.1f)",
             (double)io->mouse_pos.x, (double)io->mouse_pos.y,
             (double)io->mouse_delta.x, (double)io->mouse_delta.y,
             (double)io->mouse_scroll.x, (double)io->mouse_scroll.y);
    ui_label(ui, buf, 0);

    snprintf(buf, sizeof(buf), "Buttons down: L=%d M=%d R=%d  durations: L=%.2f R=%.2f",
             io->mouse_down[0], io->mouse_down[2], io->mouse_down[1],
             (double)io->mouse_down_duration[0], (double)io->mouse_down_duration[1]);
    ui_label(ui, buf, 0);

    snprintf(buf, sizeof(buf), "Hot ID: 0x%08X  Active ID: 0x%08X  Hovered Window: 0x%08X",
             (unsigned)ui->hot_id, (unsigned)ui->active_id, (unsigned)ui_window_hovered_id(ui));
    ui_label(ui, buf, 0);

    snprintf(buf, sizeof(buf), "Capture: mouse=%d keyboard=%d text=%d window_accept_input=%d scroll_used=%d",
             io->want_capture_mouse ? 1 : 0,
             io->want_capture_keyboard ? 1 : 0,
             io->want_text_input ? 1 : 0,
             ui->window_accept_input ? 1 : 0,
             ui->scroll_used ? 1 : 0);
    ui_label(ui, buf, 0);

    snprintf(buf, sizeof(buf), "Layout: cursor_y=%.2f max_y=%.2f next_w=%.2f next_h=%.2f spacing=%.2f same_line=%d",
             (double)ui->layout.cursor_y, (double)ui->layout.max_y,
             (double)ui->next_item_w, (double)ui->next_item_h,
             (double)ui->layout.row_spacing, ui->next_same_line ? 1 : 0);
    ui_label(ui, buf, 0);

    snprintf(buf, sizeof(buf), "Char buffer count=%u", ui->char_count);
    ui_label(ui, buf, 0);
    if (ui->char_count > 0)
    {
        char chars[96];
        uint32_t n = ui->char_count;
        if (n > (uint32_t)(sizeof(chars) / 3))
            n = (uint32_t)(sizeof(chars) / 3);
        char *out = chars;
        for (uint32_t i = 0; i < n; ++i)
        {
            uint32_t cp = ui->char_buf[i];
            if (cp < 32 || cp > 126)
                *out++ = '?';
            else
                *out++ = (char)cp;
            *out++ = ' ';
        }
        *out = 0;
        ui_label(ui, chars, 0);
    }

    ui_separator_text(ui, "Layout helpers", 0);
    ui_label(ui, "Same-line demo:", 0);
    ui_button(ui, "A", 0);
    ui_same_line(ui, 4.0f);
    ui_button(ui, "B", 0);
    ui_same_line(ui, 4.0f);
    ui_button(ui, "C", 0);

    ui_separator(ui);

    ui_window_end(ui);
}
