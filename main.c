#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <vterm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include "pty.h"

int font_size = 14;
int padding_x = 12;
int padding_y = 12;
int cursor_trail = 1;
int cursor_trail_length = 6;
double bg_r = 0.05, bg_g = 0.05, bg_b = 0.05;
double cursor_r = 1.0, cursor_g = 1.0, cursor_b = 1.0;

int rows = 24;
int cols = 80;
VTerm *vt;
VTermScreen *vts;

// new layer caching variables
cairo_surface_t *text_surface = NULL;
cairo_t *text_cr = NULL;

typedef struct { int row, col; double alpha; } TrailNode;
TrailNode trail[20] = {0};
int trail_idx = 0;
VTermPos last_cursor = {-1, -1};

void load_config() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dofile(L, "config.lua") == LUA_OK) {
        lua_getglobal(L, "config");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "font_size");
            if (lua_isinteger(L, -1)) font_size = lua_tointeger(L, -1);
            lua_pop(L, 1);
            
            lua_getfield(L, -1, "cursor_trail_length");
            if (lua_isinteger(L, -1)) {
                cursor_trail_length = lua_tointeger(L, -1);
                if (cursor_trail_length > 20) cursor_trail_length = 20;
            }
            lua_pop(L, 1);
            
            lua_getfield(L, -1, "bg_r"); if (lua_isnumber(L, -1)) bg_r = lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "bg_g"); if (lua_isnumber(L, -1)) bg_g = lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "bg_b"); if (lua_isnumber(L, -1)) bg_b = lua_tonumber(L, -1); lua_pop(L, 1);
            
            lua_getfield(L, -1, "cursor_r"); if (lua_isnumber(L, -1)) cursor_r = lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "cursor_g"); if (lua_isnumber(L, -1)) cursor_g = lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "cursor_b"); if (lua_isnumber(L, -1)) cursor_b = lua_tonumber(L, -1); lua_pop(L, 1);
        }
    }
    lua_close(L);
}

// this function only runs when the text actually changes
void render_text_layer() {
    if (!text_cr) return;
    
    cairo_set_source_rgb(text_cr, bg_r, bg_g, bg_b);
    cairo_paint(text_cr);
    
    cairo_select_font_face(text_cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(text_cr, font_size);

    VTermPos pos;
    for (pos.row = 0; pos.row < rows; pos.row++) {
        for (pos.col = 0; pos.col < cols; pos.col++) {
            VTermScreenCell cell;
            vterm_screen_get_cell(vts, pos, &cell);
            
            if (cell.chars[0] == 0) continue;
            
            int x = padding_x + (pos.col * (font_size * 0.6)); 
            int y = padding_y + ((pos.row + 1) * font_size);
            
            cairo_set_source_rgb(text_cr, cell.fg.rgb.red / 255.0, 
                                          cell.fg.rgb.green / 255.0, 
                                          cell.fg.rgb.blue / 255.0);
            
            char text[7] = {0};
            int i = 0;
            while (cell.chars[i] && i < VTERM_MAX_CHARS_PER_CELL) {
                text[i] = cell.chars[i];
                i++;
            }
            
            cairo_move_to(text_cr, x, y);
            cairo_show_text(text_cr, text);
        }
    }
}

// this runs at 60fps and only layers images, making it super fast
void draw(cairo_t *cr) {
    cairo_push_group(cr);

    // paste the cached text picture
    if (text_surface) {
        cairo_set_source_surface(cr, text_surface, 0, 0);
        cairo_paint(cr);
    }
    
    VTermState *state = vterm_obtain_state(vt);
    VTermPos cursor_pos;
    vterm_state_get_cursorpos(state, &cursor_pos);
    
    if (cursor_pos.row != last_cursor.row || cursor_pos.col != last_cursor.col) {
        if (last_cursor.row != -1 && cursor_trail) {
            trail[trail_idx].row = last_cursor.row;
            trail[trail_idx].col = last_cursor.col;
            trail[trail_idx].alpha = 0.4;
            trail_idx = (trail_idx + 1) % cursor_trail_length;
        }
        last_cursor = cursor_pos;
    }
    
    for (int i = 0; i < cursor_trail_length; i++) {
        if (trail[i].alpha > 0.01) {
            int tx = padding_x + (trail[i].col * (font_size * 0.6));
            int ty = padding_y + (trail[i].row * font_size);
            cairo_set_source_rgba(cr, cursor_r, cursor_g, cursor_b, trail[i].alpha);
            cairo_rectangle(cr, tx, ty + (font_size * 0.2), font_size * 0.6, font_size);
            cairo_fill(cr);
        }
    }
    
    int cx = padding_x + (cursor_pos.col * (font_size * 0.6));
    int cy = padding_y + (cursor_pos.row * font_size);
    cairo_set_source_rgba(cr, cursor_r, cursor_g, cursor_b, 0.6);
    cairo_rectangle(cr, cx, cy + (font_size * 0.2), font_size * 0.6, font_size);
    cairo_fill(cr);
    
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
}

int main() {
    Display *display;
    Window window;
    cairo_surface_t *surface;
    cairo_t *cr;

    load_config();
    if (pty_spawn("/bin/bash") != 0) exit(1);

    vt = vterm_new(rows, cols);
    vterm_set_utf8(vt, 1);
    vts = vterm_obtain_screen(vt);
    vterm_screen_enable_altscreen(vts, 1);
    vterm_screen_reset(vts, 1);

    display = XOpenDisplay(NULL);
    if (!display) exit(1);

    int screen = DefaultScreen(display);
    int current_width = 800;
    int current_height = 600;
    
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 
                                 10, 10, current_width, current_height, 0, 0, 0);

    XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(display, window);

    surface = cairo_xlib_surface_create(display, window, DefaultVisual(display, screen), current_width, current_height);
    cr = cairo_create(surface);
    
    // create the initial text cache layer
    text_surface = cairo_surface_create_similar(surface, CAIRO_CONTENT_COLOR, current_width, current_height);
    text_cr = cairo_create(text_surface);
    render_text_layer();

    int x11_fd = ConnectionNumber(display);
    int max_fd = (x11_fd > pty_master_fd) ? x11_fd : pty_master_fd;

    while (1) {
        fd_set in_fds;
        FD_ZERO(&in_fds);
        FD_SET(x11_fd, &in_fds);
        FD_SET(pty_master_fd, &in_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 16000; 

        int ret = select(max_fd + 1, &in_fds, NULL, NULL, &tv);
        if (ret < 0) break;

        if (ret == 0) {
            int needs_redraw = 0;
            for (int i = 0; i < cursor_trail_length; i++) {
                if (trail[i].alpha > 0) {
                    trail[i].alpha -= 0.04;
                    if (trail[i].alpha < 0) trail[i].alpha = 0;
                    needs_redraw = 1;
                }
            }
            if (needs_redraw) {
                draw(cr);
                cairo_surface_flush(surface);
                XFlush(display);
            }
        }

        if (FD_ISSET(pty_master_fd, &in_fds)) {
            char buf[1024]; // bumped buffer size for faster reading
            ssize_t bytes = read(pty_master_fd, buf, sizeof(buf) - 1);
            if (bytes > 0) {
                vterm_input_write(vt, buf, bytes);
                // only rebuild the text cache when new text arrives
                render_text_layer();
                draw(cr);
                cairo_surface_flush(surface);
                XFlush(display);
            } else {
                break;
            }
        }

        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            
            if (event.type == Expose) {
                draw(cr);
                cairo_surface_flush(surface);
            }
            
            if (event.type == ConfigureNotify) {
                if (event.xconfigure.width != current_width || event.xconfigure.height != current_height) {
                    current_width = event.xconfigure.width;
                    current_height = event.xconfigure.height;
                    
                    int new_cols = (current_width - (padding_x * 2)) / (font_size * 0.6);
                    int new_rows = (current_height - (padding_y * 2)) / font_size;
                    
                    if (new_cols > 0 && new_rows > 0 && (new_cols != cols || new_rows != rows)) {
                        cols = new_cols;
                        rows = new_rows;
                        vterm_set_size(vt, rows, cols);
                        pty_resize(rows, cols);
                    }
                    
                    cairo_destroy(cr);
                    cairo_surface_destroy(surface);
                    surface = cairo_xlib_surface_create(display, window, DefaultVisual(display, screen), current_width, current_height);
                    cr = cairo_create(surface);
                    
                    // rebuild the cache for the new window size
                    if (text_cr) cairo_destroy(text_cr);
                    if (text_surface) cairo_surface_destroy(text_surface);
                    text_surface = cairo_surface_create_similar(surface, CAIRO_CONTENT_COLOR, current_width, current_height);
                    text_cr = cairo_create(text_surface);
                    
                    render_text_layer();
                    draw(cr);
                    cairo_surface_flush(surface);
                }
            }
            
            if (event.type == KeyPress) {
                char keybuf[32];
                KeySym keysym;
                int len = XLookupString(&event.xkey, keybuf, sizeof(keybuf), &keysym, NULL);
                
                if (keysym == XK_Escape) write(pty_master_fd, "\x1b", 1);
                else if (keysym == XK_Up) write(pty_master_fd, "\x1b[A", 3);
                else if (keysym == XK_Down) write(pty_master_fd, "\x1b[B", 3);
                else if (keysym == XK_Right) write(pty_master_fd, "\x1b[C", 3);
                else if (keysym == XK_Left) write(pty_master_fd, "\x1b[D", 3);
                else if (keysym == XK_BackSpace) write(pty_master_fd, "\x7f", 1);
                else if (keysym == XK_Tab) write(pty_master_fd, "\t", 1);
                else if (keysym == XK_Return) write(pty_master_fd, "\r", 1);
                else if (len > 0) write(pty_master_fd, keybuf, len);
            }
        }
    }

    if (text_cr) cairo_destroy(text_cr);
    if (text_surface) cairo_surface_destroy(text_surface);
    vterm_free(vt);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    XCloseDisplay(display);
    return 0;
}
