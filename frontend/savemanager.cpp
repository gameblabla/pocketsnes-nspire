#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "gui.h"
#include "icons.h"
#include "platform.h"
#include "savemanager.h"
#include "states.h"
#include "ui.h"

#define MAX_FILES (MAX_STATE_SLOTS + 2)

static struct SaveFile files[MAX_FILES];
static unsigned char marked[MAX_FILES];
static int file_count, selected, top;
static char status[96];

static void refresh(void)
{
    file_count = states_list(files, MAX_FILES);
    memset(marked, 0, sizeof(marked));
    if (selected >= file_count)
        selected = file_count ? file_count - 1 : 0;
}

static int marked_count(void)
{
    int n = 0;
    for (int i = 0; i < file_count; i++)
        n += marked[i];
    return n;
}

static void size_text(long bytes, char *text, size_t size)
{
    if (bytes < 1024)
        snprintf(text, size, "%ld B", bytes);
    else
        snprintf(text, size, "%ld KB", (bytes + 1023) / 1024);
}

static void draw(const char *title)
{
    char right[48], label[32], value[64], size[24];
    long total = 0;
    for (int i = 0; i < file_count; i++)
        total += files[i].bytes;
    size_text(total, size, sizeof(size));
    snprintf(right, sizeof(right), "%d files, %s", file_count, size);
    ui_frame(title, right);

    if (!file_count)
        draw_string(FONT_MEDIUM, "No saves for this game yet.", 16, UI_LIST_Y + 8, COLOR_TEXT_DIM);
    for (int row = 0; row < UI_LIST_ROWS && top + row < file_count; row++)
    {
        int i = top + row;
        const struct SaveFile *f = &files[i];
        if (f->slot == 0)
            snprintf(label, sizeof(label), "In-game save");
        else
            states_slot_name_cap(f->slot, label, sizeof(label));
        size_text(f->bytes, size, sizeof(size));
        snprintf(value, sizeof(value), "%s, %s", size, f->compressed ? "compressed" : "not compressed");
        int icon = marked[i] ? ICON_STAR : f->slot == 0 ? ICON_DISK : ICON_SAVE;
        uint16_t color = marked[i] ? COLOR_VALUE : f->compressed ? COLOR_GOOD : COLOR_TEXT_DIM;
        ui_row(UI_LIST_Y + row * UI_ROW_H, icon, color, label, value, i == selected ? ROW_SELECTED : 0);
    }
    ui_scrollbar(top, UI_LIST_ROWS, file_count, UI_LIST_Y, UI_LIST_ROWS * UI_ROW_H);

    if (status[0])
        ui_help(status);
    else
        ui_help("Enter marks (A: all). C, U and del compress, uncompress or delete them, or this one.");
    static const struct UiHint hints[] = {
        { "enter", "Mark" }, { "C", "Compress" }, { "U", "Uncompress" }, { "del", "Delete" } };
    ui_footer(hints, 4);
}

/* Runs an action on the marked files, or the selected one. */
static void act(int what)   /* 'c' compress, 'u' uncompress, 'd' delete */
{
    int targets = marked_count();
    if (!file_count)
        return;
    if (what == 'd')
    {
        char line[64];
        if (targets > 1)
            snprintf(line, sizeof(line), "Delete these %d saves?", targets);
        else
        {
            char name[32];
            int i = targets ? 0 : selected;
            while (targets && !marked[i])
                i++;
            if (files[i].slot == 0)
                snprintf(name, sizeof(name), "the in-game save");
            else
                states_slot_name(files[i].slot, name, sizeof(name));
            snprintf(line, sizeof(line), "Delete %s?", name);
        }
        if (!gui_confirm(line, "It can't be undone."))
            return;
    }

    static int slots[MAX_FILES];
    int slot_count = 0;
    long before = 0, after = 0;
    int done = 0, failed = 0;
    for (int i = 0; i < file_count; i++)
    {
        if (targets ? !marked[i] : i != selected)
            continue;
        before += files[i].bytes;
        int ok = what == 'd' ? states_delete(files[i].slot) : states_set_compressed(files[i].slot, what == 'c');
        if (ok)
            done++;
        else
            failed++;
        slots[slot_count++] = files[i].slot;
    }
    refresh();
    for (int i = 0; i < file_count; i++)
        for (int k = 0; k < slot_count; k++)
            if (files[i].slot == slots[k])
                after += files[i].bytes;

    char was[24], now[24];
    size_text(before, was, sizeof(was));
    size_text(after, now, sizeof(now));
    const char *verb = what == 'c' ? "Compressed" : what == 'u' ? "Uncompressed" : "Deleted";
    if (failed)
        snprintf(status, sizeof(status), "%s %d; %d couldn't be (no space or memory?).", verb, done, failed);
    else if (what == 'd')
        snprintf(status, sizeof(status), "Deleted %d %s (%s).", done, done == 1 ? "file" : "files", was);
    else
        snprintf(status, sizeof(status), "%s %d %s: %s before, %s now.", verb, done, done == 1 ? "file" : "files",
                 was, now);
}

void save_manager_run(const char *title)
{
    selected = top = 0;
    status[0] = 0;
    refresh();
    gui_wait_release();
    for (;;)
    {
        if (selected < top)
            top = selected;
        if (selected >= top + UI_LIST_ROWS)
            top = selected - UI_LIST_ROWS + 1;
        draw(title);
        gui_present();

        enum GuiAction action = gui_input();
        if (action == GUI_BACK || action == GUI_QUIT)
            break;
        if (action == GUI_UP && file_count)
            selected = (selected + file_count - 1) % file_count;
        else if (action == GUI_DOWN && file_count)
            selected = (selected + 1) % file_count;
        else if (action == GUI_SELECT && file_count)
        {
            marked[selected] = !marked[selected];
            if (selected + 1 < file_count)
                selected++;
            status[0] = 0;
        }
        else if (action == GUI_CLEAR)
            act('d');
        else if (platform_key_pressed(KEY_C))
            act('c');
        else if (platform_key_pressed(KEY_U))
            act('u');
        else if (platform_key_pressed(KEY_A))
        {
            int all = marked_count() == file_count;
            memset(marked, all ? 0 : 1, sizeof(marked));
            status[0] = 0;
        }
    }
    gui_wait_release();
}
