/*
 * MiniOS XGUI Flappy Cat — Flappy Bird clone
 *
 * Controls: Space/Click to flap. Press Space to start, R to restart.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/display.h"
#include "xgui/theme.h"
#include "string.h"
#include "keyboard.h"
#include "stdio.h"
#include "timer.h"

static xgui_window_t* fc_window = NULL;

#define FC_W    240
#define FC_H    380
#define GRAVITY         1
#define FLAP_STRENGTH  -6
#define MAX_FALL        8
#define PIPE_SPEED      2
#define PIPE_GAP        70
#define PIPE_W          28
#define PIPE_SPACING    100
#define MAX_PIPES       4
#define CAT_W   18
#define CAT_H   14
#define CAT_X   40
#define GROUND_H 30

typedef enum { FC_MENU, FC_PLAYING, FC_GAMEOVER } fc_state_t;

typedef struct {
    int x, gap_y;
    bool scored, active;
} pipe_t;

static fc_state_t fc_state = FC_MENU;
static int cat_y, cat_vy, fc_score;
static uint32_t fc_frames;
static pipe_t pipes[MAX_PIPES];
static uint32_t fc_rng;
static int ground_off;

static uint32_t fc_rand(void) {
    fc_rng ^= fc_rng << 13;
    fc_rng ^= fc_rng >> 17;
    fc_rng ^= fc_rng << 5;
    return fc_rng;
}

static void fc_reset(void) {
    cat_y = FC_H / 2 - 30;
    cat_vy = 0;
    fc_score = 0;
    fc_frames = 0;
    ground_off = 0;
    fc_rng = timer_get_ticks() ^ 0xCAFEBEEF;
    if (fc_rng == 0) fc_rng = 54321;
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].x = FC_W + 40 + i * PIPE_SPACING;
        pipes[i].gap_y = 50 + (int)(fc_rand() % 140);
        pipes[i].scored = false;
        pipes[i].active = true;
    }
}

/* Draw cat */
static void draw_cat(xgui_window_t* w, int cy, int vy) {
    int bx = CAT_X - CAT_W / 2, by = cy - CAT_H / 2;
    uint32_t body = XGUI_RGB(240, 160, 50);
    uint32_t dark = XGUI_RGB(200, 120, 30);
    uint32_t belly = XGUI_RGB(255, 220, 170);

    /* Body */
    xgui_win_rect_filled(w, bx + 2, by + 3, CAT_W - 4, CAT_H - 5, body);
    xgui_win_rect_filled(w, bx + 1, by + 5, CAT_W - 2, CAT_H - 8, body);
    xgui_win_rect_filled(w, bx, by + 6, CAT_W, CAT_H - 10, body);
    xgui_win_rect_filled(w, bx + 4, by + 7, CAT_W - 8, 4, belly);

    /* Ears */
    xgui_win_rect_filled(w, bx + 2, by, 3, 3, body);
    xgui_win_rect_filled(w, bx + CAT_W - 5, by, 3, 3, body);
    xgui_win_rect_filled(w, bx + 3, by + 1, 1, 1, dark);
    xgui_win_rect_filled(w, bx + CAT_W - 4, by + 1, 1, 1, dark);

    /* Eyes */
    xgui_win_rect_filled(w, bx + 3, by + 4, 3, 3, XGUI_WHITE);
    xgui_win_rect_filled(w, bx + CAT_W - 6, by + 4, 3, 3, XGUI_WHITE);
    xgui_win_rect_filled(w, bx + 5, by + 5, 1, 2, XGUI_BLACK);
    xgui_win_rect_filled(w, bx + CAT_W - 4, by + 5, 1, 2, XGUI_BLACK);

    /* Nose + whiskers */
    xgui_win_pixel(w, bx + CAT_W / 2, by + 8, XGUI_RGB(255, 120, 130));
    xgui_win_hline(w, bx - 2, by + 8, 3, XGUI_RGB(80, 80, 80));
    xgui_win_hline(w, bx + CAT_W - 1, by + 8, 3, XGUI_RGB(80, 80, 80));

    /* Flap wings */
    if (vy < -2) {
        xgui_win_hline(w, bx - 2, by + 7, 3, dark);
        xgui_win_hline(w, bx + CAT_W - 1, by + 7, 3, dark);
    }

    /* Tail */
    xgui_win_rect_filled(w, bx - 3, by + 5, 3, 2, dark);
    xgui_win_pixel(w, bx - 4, by + 4, dark);
}

/* Draw a pipe pair */
static void draw_pipe(xgui_window_t* w, pipe_t* p) {
    if (!p->active) return;
    uint32_t pb = XGUI_RGB(80, 180, 60);
    uint32_t pd = XGUI_RGB(50, 140, 35);
    uint32_t pl = XGUI_RGB(110, 210, 90);
    int cap_x = p->x - 3, cap_w = PIPE_W + 6;

    /* Top pipe */
    if (p->gap_y > 0) {
        xgui_win_rect_filled(w, p->x, 0, PIPE_W, p->gap_y, pb);
        xgui_win_vline(w, p->x, 0, p->gap_y, pl);
        xgui_win_vline(w, p->x + PIPE_W - 1, 0, p->gap_y, pd);
        /* Cap */
        xgui_win_rect_filled(w, cap_x, p->gap_y - 8, cap_w, 8, pb);
        xgui_win_rect(w, cap_x, p->gap_y - 8, cap_w, 8, pd);
        xgui_win_hline(w, cap_x + 1, p->gap_y - 8, cap_w - 2, pl);
    }

    /* Bottom pipe */
    int bot_top = p->gap_y + PIPE_GAP;
    int bot_h = FC_H - GROUND_H - bot_top;
    if (bot_h > 0) {
        xgui_win_rect_filled(w, p->x, bot_top, PIPE_W, bot_h, pb);
        xgui_win_vline(w, p->x, bot_top, bot_h, pl);
        xgui_win_vline(w, p->x + PIPE_W - 1, bot_top, bot_h, pd);
        /* Cap */
        xgui_win_rect_filled(w, cap_x, bot_top, cap_w, 8, pb);
        xgui_win_rect(w, cap_x, bot_top, cap_w, 8, pd);
        xgui_win_hline(w, cap_x + 1, bot_top, cap_w - 2, pl);
    }
}

/* Draw ground */
static void draw_ground(xgui_window_t* w) {
    int gy = FC_H - GROUND_H;
    xgui_win_rect_filled(w, 0, gy, FC_W, GROUND_H, XGUI_RGB(210, 180, 100));
    xgui_win_hline(w, 0, gy, FC_W, XGUI_RGB(100, 180, 50));
    xgui_win_hline(w, 0, gy + 1, FC_W, XGUI_RGB(80, 160, 40));
    /* Ground stripes */
    for (int i = -1; i < FC_W / 20 + 2; i++) {
        int sx = i * 20 - (ground_off % 20);
        xgui_win_rect_filled(w, sx, gy + 4, 10, 2, XGUI_RGB(190, 160, 80));
    }
}

/* Draw sky background */
static void draw_sky(xgui_window_t* w) {
    for (int row = 0; row < FC_H - GROUND_H; row++) {
        int t = (row * 255) / (FC_H - GROUND_H);
        int r = 100 + (155 - 100) * t / 255;
        int g = 180 + (220 - 180) * t / 255;
        int b = 255;
        xgui_win_hline(w, 0, row, FC_W, XGUI_RGB(r, g, b));
    }
}

/* Draw score */
static void draw_score_text(xgui_window_t* w, int s) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", s);
    /* Shadow */
    xgui_win_text_transparent(w, FC_W / 2 - 5, 21, buf, XGUI_RGB(0, 0, 0));
    xgui_win_text_transparent(w, FC_W / 2 - 6, 20, buf, XGUI_WHITE);
}

/* Menu screen */
static void draw_menu(xgui_window_t* w) {
    draw_sky(w);
    draw_ground(w);

    /* Title */
    xgui_win_text_transparent(w, FC_W / 2 - 44, 40, "FLAPPY CAT", XGUI_RGB(240, 160, 50));
    xgui_win_text_transparent(w, FC_W / 2 - 45, 39, "FLAPPY CAT", XGUI_RGB(255, 200, 80));

    /* Cat preview */
    draw_cat(w, FC_H / 2 - 40, -3);

    /* Instructions */
    xgui_win_text_transparent(w, FC_W / 2 - 60, FC_H / 2, "Press SPACE to flap", XGUI_RGB(60, 60, 60));
    xgui_win_text_transparent(w, FC_W / 2 - 60, FC_H / 2 + 20, "Avoid the pipes!", XGUI_RGB(60, 60, 60));

    xgui_win_text_transparent(w, FC_W / 2 - 68, FC_H / 2 + 60, "Press Spacebar to start", XGUI_RGB(0, 100, 0));
}

/* Game over overlay */
static void draw_gameover(xgui_window_t* w) {
    for (int y = FC_H / 2 - 40; y < FC_H / 2 + 40; y++)
        xgui_win_hline(w, 15, y, FC_W - 30, XGUI_RGB(40, 40, 60));

    xgui_win_text_transparent(w, FC_W / 2 - 36, FC_H / 2 - 25, "GAME OVER", XGUI_WHITE);

    char buf[32];
    snprintf(buf, sizeof(buf), "Score: %d", fc_score);
    xgui_win_text_transparent(w, FC_W / 2 - 30, FC_H / 2 - 5, buf, XGUI_RGB(255, 255, 100));

    xgui_win_text_transparent(w, FC_W / 2 - 48, FC_H / 2 + 18, "Press R to retry", XGUI_RGB(180, 180, 180));
}

/* Paint callback */
static void fc_paint(xgui_window_t* w) {
    if (fc_state == FC_MENU) {
        draw_menu(w);
        return;
    }

    draw_sky(w);
    for (int i = 0; i < MAX_PIPES; i++) draw_pipe(w, &pipes[i]);
    draw_ground(w);
    draw_cat(w, cat_y, cat_vy);
    draw_score_text(w, fc_score);

    if (fc_state == FC_GAMEOVER) draw_gameover(w);
}

/* Game tick */
static void fc_tick(void) {
    fc_frames++;

    /* Gravity */
    cat_vy += GRAVITY;
    if (cat_vy > MAX_FALL) cat_vy = MAX_FALL;
    cat_y += cat_vy;

    /* Ground collision */
    if (cat_y + CAT_H / 2 >= FC_H - GROUND_H) {
        cat_y = FC_H - GROUND_H - CAT_H / 2;
        fc_state = FC_GAMEOVER;
        return;
    }
    /* Ceiling */
    if (cat_y - CAT_H / 2 < 0) {
        cat_y = CAT_H / 2;
        cat_vy = 0;
    }

    /* Move pipes and check collisions */
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].active) continue;
        pipes[i].x -= PIPE_SPEED;

        /* Score when passing */
        if (!pipes[i].scored && pipes[i].x + PIPE_W < CAT_X) {
            pipes[i].scored = true;
            fc_score++;
        }

        /* Recycle pipe */
        if (pipes[i].x + PIPE_W + 6 < 0) {
            int max_x = 0;
            for (int j = 0; j < MAX_PIPES; j++)
                if (pipes[j].x > max_x) max_x = pipes[j].x;
            pipes[i].x = max_x + PIPE_SPACING;
            pipes[i].gap_y = 50 + (int)(fc_rand() % 140);
            pipes[i].scored = false;
        }

        /* Collision detection */
        int cx = CAT_X, cy = cat_y;
        int chw = CAT_W / 2 - 2, chh = CAT_H / 2 - 2;
        int px = pipes[i].x - 3, pw = PIPE_W + 6; /* Include cap */

        if (cx + chw > px && cx - chw < px + pw) {
            /* In pipe column — check if in gap */
            if (cy - chh < pipes[i].gap_y || cy + chh > pipes[i].gap_y + PIPE_GAP) {
                fc_state = FC_GAMEOVER;
                return;
            }
        }
    }

    /* Scroll ground */
    ground_off += PIPE_SPEED;
}

/* Event handler */
static void fc_handler(xgui_window_t* w, xgui_event_t* event) {
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(w);
        fc_window = NULL;
        fc_state = FC_MENU;
        return;
    }

    if (event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;
        if (fc_state == FC_MENU && c == ' ') {
            fc_reset();
            fc_state = FC_PLAYING;
            w->dirty = true;
            return;
        }
        if (fc_state == FC_PLAYING && c == ' ') {
            cat_vy = FLAP_STRENGTH;
            return;
        }
        if (fc_state == FC_GAMEOVER && (c == 'r' || c == 'R')) {
            fc_reset();
            fc_state = FC_PLAYING;
            w->dirty = true;
            return;
        }
    }

    if (event->type == XGUI_EVENT_MOUSE_DOWN) {
        if (fc_state == FC_PLAYING) {
            cat_vy = FLAP_STRENGTH;
        }
    }
}

/* Update — called from main loop */
void xgui_flappycat_update(void) {
    if (!fc_window) return;
    if (fc_state != FC_PLAYING) return;
    fc_tick();
    xgui_window_invalidate(fc_window);
}

/* Create window */
void xgui_flappycat_create(void) {
    if (fc_window) {
        xgui_window_focus(fc_window);
        return;
    }
    fc_state = FC_MENU;
    fc_window = xgui_window_create("Flappy Cat", 120, 30, FC_W, FC_H,
                                    XGUI_WINDOW_DEFAULT);
    if (!fc_window) return;
    xgui_window_set_paint(fc_window, fc_paint);
    xgui_window_set_handler(fc_window, fc_handler);
    xgui_window_set_bgcolor(fc_window, XGUI_RGB(100, 180, 255));
}
