/*
 * MiniOS XGUI Ski Game — SkiFree clone
 *
 * The player skis downhill avoiding trees, rocks, and jumps.
 * Controls: Left/Right arrows to steer, Down to go faster, Up to slow down.
 * Press Space to start, R to restart after game over.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/display.h"
#include "xgui/theme.h"
#include "string.h"
#include "keyboard.h"
#include "stdio.h"
#include "timer.h"

/* Window */
static xgui_window_t* ski_window = NULL;

/* Game area */
#define GAME_W  280
#define GAME_H  360
#define WIN_W   GAME_W
#define WIN_H   GAME_H

/* Game states */
typedef enum {
    SKI_STATE_MENU,
    SKI_STATE_PLAYING,
    SKI_STATE_GAMEOVER
} ski_state_t;

/* Obstacle types */
typedef enum {
    OBS_TREE_SMALL,
    OBS_TREE_LARGE,
    OBS_ROCK,
    OBS_JUMP
} obs_type_t;

/* Obstacle */
#define MAX_OBSTACLES 40
typedef struct {
    int x, y;
    obs_type_t type;
    bool active;
} obstacle_t;

/* Game state */
static ski_state_t game_state = SKI_STATE_MENU;
static int player_x;           /* Player X position (center) */
static int player_dir;         /* -1 = left, 0 = straight, 1 = right */
static int speed;              /* Scroll speed (pixels per tick) */
static int score;
static int distance;
static uint32_t frame_count;
static obstacle_t obstacles[MAX_OBSTACLES];
static uint32_t rng_state;
static int input_left;         /* >0 means left is active */
static int input_right;
static int input_speedup;
static int input_slowdown;
static int steer_vel;          /* Steering velocity: -128..+128 (fixed point) */
static bool crashed;

#define STEER_MAX       128    /* Max steering velocity */
#define STEER_FRICTION  4      /* Deceleration when no input */
#define STEER_MOVE_DIV  64     /* Divide velocity by this for pixel movement */

/* Simple PRNG */
static uint32_t ski_rand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int ski_rand_range(int min, int max) {
    if (min >= max) return min;
    return min + (int)(ski_rand() % (uint32_t)(max - min));
}

/*
 * Initialize/reset game
 */
static void ski_reset(void) {
    player_x = GAME_W / 2;
    player_dir = 0;
    speed = 2;
    score = 0;
    distance = 0;
    frame_count = 0;
    input_left = 0;
    input_right = 0;
    input_speedup = 0;
    input_slowdown = 0;
    steer_vel = 0;
    crashed = false;
    rng_state = timer_get_ticks() ^ 0xDEADBEEF;

    /* Clear obstacles */
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        obstacles[i].active = false;
    }

    /* Pre-populate some obstacles below the screen */
    for (int i = 0; i < 15; i++) {
        obstacles[i].active = true;
        obstacles[i].x = ski_rand_range(10, GAME_W - 10);
        obstacles[i].y = ski_rand_range(80, GAME_H + 200);
        obstacles[i].type = (obs_type_t)(ski_rand() % 4);
    }
}

/*
 * Spawn a new obstacle at the bottom
 */
static void spawn_obstacle(void) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) {
            obstacles[i].active = true;
            obstacles[i].x = ski_rand_range(10, GAME_W - 10);
            obstacles[i].y = GAME_H + ski_rand_range(10, 60);
            obstacles[i].type = (obs_type_t)(ski_rand() % 4);
            return;
        }
    }
}

/*
 * Check collision between player and obstacle
 * Player hitbox: roughly 6px wide, 10px tall, centered at (player_x, 60)
 */
static bool check_collision(obstacle_t* obs) {
    int px = player_x;
    int py = 60;
    int pw = 4;  /* half-width */
    int ph = 6;  /* half-height */

    int ox = obs->x;
    int oy = obs->y;
    int ow, oh;

    switch (obs->type) {
        case OBS_TREE_SMALL: ow = 4; oh = 8; break;
        case OBS_TREE_LARGE: ow = 6; oh = 12; break;
        case OBS_ROCK:       ow = 5; oh = 4; break;
        case OBS_JUMP:       return false; /* Jumps don't kill */
        default:             ow = 4; oh = 4; break;
    }

    return (px - pw < ox + ow && px + pw > ox - ow &&
            py - ph < oy + oh && py + ph > oy - oh);
}

/*
 * Draw the skier at position (player_x, 60)
 */
static void draw_skier(xgui_window_t* win) {
    int px = player_x;
    int py = 60;

    if (crashed) {
        /* Crashed: X shape */
        xgui_win_line(win, px - 5, py - 5, px + 5, py + 5, XGUI_RED);
        xgui_win_line(win, px + 5, py - 5, px - 5, py + 5, XGUI_RED);
        xgui_win_line(win, px - 4, py - 5, px + 6, py + 5, XGUI_RED);
        xgui_win_line(win, px + 6, py - 5, px - 4, py + 5, XGUI_RED);
        return;
    }

    /* Head */
    xgui_win_rect_filled(win, px - 2, py - 8, 4, 4, XGUI_RGB(255, 200, 150));

    /* Body */
    xgui_win_vline(win, px, py - 4, 8, XGUI_RGB(0, 0, 200));

    /* Arms (angle based on direction) */
    if (player_dir < 0) {
        /* Leaning left */
        xgui_win_line(win, px, py - 2, px - 7, py - 5, XGUI_RGB(0, 0, 200));
        xgui_win_line(win, px, py - 2, px + 4, py + 1, XGUI_RGB(0, 0, 200));
    } else if (player_dir > 0) {
        /* Leaning right */
        xgui_win_line(win, px, py - 2, px + 7, py - 5, XGUI_RGB(0, 0, 200));
        xgui_win_line(win, px, py - 2, px - 4, py + 1, XGUI_RGB(0, 0, 200));
    } else {
        /* Straight */
        xgui_win_line(win, px, py - 2, px - 6, py, XGUI_RGB(0, 0, 200));
        xgui_win_line(win, px, py - 2, px + 6, py, XGUI_RGB(0, 0, 200));
    }

    /* Skis */
    if (player_dir < 0) {
        xgui_win_line(win, px - 6, py + 4, px + 2, py + 6, XGUI_RGB(80, 80, 80));
        xgui_win_line(win, px - 4, py + 5, px + 4, py + 7, XGUI_RGB(80, 80, 80));
    } else if (player_dir > 0) {
        xgui_win_line(win, px - 2, py + 6, px + 6, py + 4, XGUI_RGB(80, 80, 80));
        xgui_win_line(win, px - 4, py + 7, px + 4, py + 5, XGUI_RGB(80, 80, 80));
    } else {
        xgui_win_hline(win, px - 5, py + 5, 10, XGUI_RGB(80, 80, 80));
        xgui_win_hline(win, px - 5, py + 6, 10, XGUI_RGB(80, 80, 80));
    }

    /* Poles */
    if (player_dir <= 0) {
        xgui_win_line(win, px - 6, py, px - 8, py + 10, XGUI_RGB(120, 80, 40));
    }
    if (player_dir >= 0) {
        xgui_win_line(win, px + 6, py, px + 8, py + 10, XGUI_RGB(120, 80, 40));
    }
}

/*
 * Draw an obstacle
 */
static void draw_obstacle(xgui_window_t* win, obstacle_t* obs) {
    int x = obs->x;
    int y = obs->y;

    switch (obs->type) {
        case OBS_TREE_SMALL:
            /* Small tree: green triangle + brown trunk */
            xgui_win_line(win, x, y - 10, x - 5, y, XGUI_RGB(0, 120, 0));
            xgui_win_line(win, x, y - 10, x + 5, y, XGUI_RGB(0, 120, 0));
            xgui_win_hline(win, x - 5, y, 11, XGUI_RGB(0, 120, 0));
            /* Fill */
            for (int row = 0; row < 10; row++) {
                int hw = (row * 5) / 10;
                xgui_win_hline(win, x - hw, y - 10 + row, hw * 2 + 1, XGUI_RGB(0, 140, 0));
            }
            /* Trunk */
            xgui_win_rect_filled(win, x - 1, y, 3, 4, XGUI_RGB(100, 60, 20));
            break;

        case OBS_TREE_LARGE:
            /* Large tree: bigger triangle */
            for (int row = 0; row < 16; row++) {
                int hw = (row * 8) / 16;
                xgui_win_hline(win, x - hw, y - 16 + row, hw * 2 + 1, XGUI_RGB(0, 100, 0));
            }
            /* Snow on top */
            for (int row = 0; row < 4; row++) {
                int hw = (row * 2) / 4;
                xgui_win_hline(win, x - hw, y - 16 + row, hw * 2 + 1, XGUI_WHITE);
            }
            /* Trunk */
            xgui_win_rect_filled(win, x - 2, y, 4, 5, XGUI_RGB(80, 50, 20));
            break;

        case OBS_ROCK:
            /* Gray rock */
            xgui_win_rect_filled(win, x - 4, y - 2, 8, 5, XGUI_RGB(130, 130, 130));
            xgui_win_rect_filled(win, x - 3, y - 3, 6, 1, XGUI_RGB(150, 150, 150));
            xgui_win_rect_filled(win, x - 2, y + 3, 4, 1, XGUI_RGB(100, 100, 100));
            break;

        case OBS_JUMP:
            /* Ramp: brown wedge shape */
            for (int row = 0; row < 4; row++) {
                int w = 8 + row * 2;
                xgui_win_hline(win, x - w / 2, y - 4 + row, w, XGUI_RGB(160, 120, 60));
            }
            /* Snow on ramp */
            xgui_win_hline(win, x - 4, y - 4, 9, XGUI_WHITE);
            break;
    }
}

/*
 * Draw snow trail behind skier
 */
static void draw_trail(xgui_window_t* win) {
    if (crashed) return;
    /* Two thin lines from ski positions going up */
    uint32_t trail_col = XGUI_RGB(210, 220, 235);
    if (player_dir == 0) {
        xgui_win_vline(win, player_x - 3, 20, 35, trail_col);
        xgui_win_vline(win, player_x + 3, 20, 35, trail_col);
    } else if (player_dir < 0) {
        xgui_win_line(win, player_x - 4, 55, player_x + 2, 20, trail_col);
        xgui_win_line(win, player_x + 2, 56, player_x + 6, 20, trail_col);
    } else {
        xgui_win_line(win, player_x + 4, 55, player_x - 2, 20, trail_col);
        xgui_win_line(win, player_x - 2, 56, player_x - 6, 20, trail_col);
    }
}

/*
 * Draw the HUD (score, distance, speed)
 */
static void draw_hud(xgui_window_t* win) {
    char buf[32];

    snprintf(buf, sizeof(buf), "Score: %d", score);
    xgui_win_text_transparent(win, 5, 5, buf, XGUI_RGB(40, 40, 80));

    snprintf(buf, sizeof(buf), "Dist: %dm", distance / 10);
    xgui_win_text_transparent(win, GAME_W - 90, 5, buf, XGUI_RGB(40, 40, 80));
}

/*
 * Draw the menu screen
 */
static void draw_menu(xgui_window_t* win) {
    xgui_win_clear(win, XGUI_WHITE);

    /* Title */
    xgui_win_text_transparent(win, GAME_W / 2 - 40, 20, "SKI GAME", XGUI_RGB(0, 60, 160));

    /* Mountain scene decoration */
    /* Mountain 1 */
    for (int row = 0; row < 50; row++) {
        int hw = row * 40 / 50;
        uint32_t c = XGUI_RGB(180 - row, 200 - row, 220);
        xgui_win_hline(win, 70 - hw, 60 + row, hw * 2 + 1, c);
    }
    /* Mountain 2 */
    for (int row = 0; row < 40; row++) {
        int hw = row * 30 / 40;
        uint32_t c = XGUI_RGB(160 - row, 180 - row, 210);
        xgui_win_hline(win, 190 - hw, 70 + row, hw * 2 + 1, c);
    }
    /* Snow caps */
    for (int row = 0; row < 12; row++) {
        int hw = row * 8 / 12;
        xgui_win_hline(win, 70 - hw, 60 + row, hw * 2 + 1, XGUI_WHITE);
    }
    for (int row = 0; row < 8; row++) {
        int hw = row * 6 / 8;
        xgui_win_hline(win, 190 - hw, 70 + row, hw * 2 + 1, XGUI_WHITE);
    }

    /* Small tree decorations */
    int tree_xs[] = {30, 100, 170, 230, 60, 200};
    int tree_ys[] = {140, 150, 145, 155, 165, 160};
    for (int t = 0; t < 6; t++) {
        int tx = tree_xs[t], ty = tree_ys[t];
        for (int row = 0; row < 10; row++) {
            int hw = (row * 5) / 10;
            xgui_win_hline(win, tx - hw, ty - 10 + row, hw * 2 + 1, XGUI_RGB(0, 140, 0));
        }
        xgui_win_rect_filled(win, tx - 1, ty, 3, 3, XGUI_RGB(100, 60, 20));
    }

    /* How to Play */
    xgui_win_text_transparent(win, GAME_W / 2 - 48, 190, "HOW TO PLAY", XGUI_RGB(0, 0, 0));

    xgui_win_hline(win, 40, 206, GAME_W - 80, XGUI_RGB(180, 180, 180));

    xgui_win_text_transparent(win, 45, 214, "Left/Right: Steer", XGUI_RGB(60, 60, 60));
    xgui_win_text_transparent(win, 45, 230, "Down Arrow: Speed up", XGUI_RGB(60, 60, 60));
    xgui_win_text_transparent(win, 45, 246, "Up Arrow:   Slow down", XGUI_RGB(60, 60, 60));
    xgui_win_text_transparent(win, 45, 262, "Avoid trees & rocks!", XGUI_RGB(60, 60, 60));

    xgui_win_hline(win, 40, 280, GAME_W - 80, XGUI_RGB(180, 180, 180));

    xgui_win_text_transparent(win, GAME_W / 2 - 76, 295, "Press Spacebar to start", XGUI_RGB(0, 100, 0));
}

/*
 * Draw game over screen
 */
static void draw_gameover(xgui_window_t* win) {
    /* Semi-dark overlay */
    for (int y = GAME_H / 2 - 50; y < GAME_H / 2 + 50; y++) {
        xgui_win_hline(win, 20, y, GAME_W - 40, XGUI_RGB(40, 40, 60));
    }
    /* Border */
    xgui_win_rect(win, 20, GAME_H / 2 - 50, GAME_W - 40, 100, XGUI_WHITE);

    char buf[32];
    xgui_win_text_transparent(win, GAME_W / 2 - 40, GAME_H / 2 - 35, "GAME OVER", XGUI_RED);

    snprintf(buf, sizeof(buf), "Score: %d", score);
    xgui_win_text_transparent(win, GAME_W / 2 - 40, GAME_H / 2 - 10, buf, XGUI_WHITE);

    snprintf(buf, sizeof(buf), "Distance: %dm", distance / 10);
    xgui_win_text_transparent(win, GAME_W / 2 - 52, GAME_H / 2 + 8, buf, XGUI_WHITE);

    xgui_win_text_transparent(win, GAME_W / 2 - 60, GAME_H / 2 + 30, "Press R to restart", XGUI_RGB(200, 200, 100));
}

/*
 * Paint callback
 */
static void ski_paint(xgui_window_t* win) {
    if (game_state == SKI_STATE_MENU) {
        draw_menu(win);
        return;
    }

    /* Snow background */
    xgui_win_clear(win, XGUI_RGB(235, 240, 250));

    /* Subtle snow texture — scattered dots */
    uint32_t snow_seed = frame_count * 7 + 1;
    for (int i = 0; i < 30; i++) {
        snow_seed ^= snow_seed << 13;
        snow_seed ^= snow_seed >> 17;
        snow_seed ^= snow_seed << 5;
        int sx = snow_seed % GAME_W;
        int sy = (snow_seed / GAME_W) % GAME_H;
        xgui_win_pixel(win, sx, sy, XGUI_RGB(220, 225, 235));
    }

    /* Draw trail */
    draw_trail(win);

    /* Draw obstacles */
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active && obstacles[i].y > -20 && obstacles[i].y < GAME_H + 10) {
            draw_obstacle(win, &obstacles[i]);
        }
    }

    /* Draw skier */
    draw_skier(win);

    /* HUD */
    draw_hud(win);

    /* Game over overlay */
    if (game_state == SKI_STATE_GAMEOVER) {
        draw_gameover(win);
    }
}

/*
 * Game tick — advance simulation
 */
static void ski_tick(void) {
    if (game_state != SKI_STATE_PLAYING) return;

    frame_count++;

    /* Steering: diminishing acceleration curve.
     * Acceleration = base * (1 - |vel|/max), so it's fast at first,
     * then slows as you approach max velocity. */
    if (input_left > 0 && input_right == 0) {
        int remaining = STEER_MAX - (-steer_vel > 0 ? -steer_vel : 0);
        int accel = 8 + (remaining * 12) / STEER_MAX; /* 8..20 range */
        steer_vel -= accel;
        if (steer_vel < -STEER_MAX) steer_vel = -STEER_MAX;
        input_left--;
    } else if (input_right > 0 && input_left == 0) {
        int remaining = STEER_MAX - (steer_vel > 0 ? steer_vel : 0);
        int accel = 8 + (remaining * 12) / STEER_MAX; /* 8..20 range */
        steer_vel += accel;
        if (steer_vel > STEER_MAX) steer_vel = STEER_MAX;
        input_right--;
    } else {
        /* Friction: gentle deceleration toward zero */
        if (steer_vel > 0) {
            steer_vel -= STEER_FRICTION;
            if (steer_vel < 0) steer_vel = 0;
        } else if (steer_vel < 0) {
            steer_vel += STEER_FRICTION;
            if (steer_vel > 0) steer_vel = 0;
        }
        if (input_left > 0) input_left--;
        if (input_right > 0) input_right--;
    }

    /* Apply steering velocity to position (minimum 1px if any velocity) */
    int move = steer_vel / STEER_MOVE_DIV;
    if (move == 0 && steer_vel > 0) move = 1;
    if (move == 0 && steer_vel < 0) move = -1;
    player_x += move;

    /* Set visual direction based on velocity */
    if (steer_vel < -8) player_dir = -1;
    else if (steer_vel > 8) player_dir = 1;
    else player_dir = 0;

    /* Speed control (inputs decay each frame) */
    if (input_speedup > 0) {
        if (speed < 6) speed = 6;
        input_speedup--;
    } else if (input_slowdown > 0) {
        if (speed > 1) speed = 1;
        input_slowdown--;
    } else {
        /* Gradually return to normal speed */
        if (speed < 3) speed++;
        else if (speed > 3) speed--;
    }

    /* Clamp player position */
    if (player_x < 8) player_x = 8;
    if (player_x > GAME_W - 8) player_x = GAME_W - 8;

    /* Scroll obstacles upward */
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            obstacles[i].y -= speed;
            /* Remove if scrolled off top */
            if (obstacles[i].y < -30) {
                obstacles[i].active = false;
                score += 10;
            }
        }
    }

    /* Spawn new obstacles */
    if (frame_count % (8 - speed) == 0 || frame_count % 5 == 0) {
        spawn_obstacle();
    }

    /* Collision detection */
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            if (obstacles[i].type == OBS_JUMP) {
                /* Hitting a jump gives bonus points */
                int dx = player_x - obstacles[i].x;
                int dy = 60 - obstacles[i].y;
                if (dx > -6 && dx < 6 && dy > -4 && dy < 4) {
                    score += 50;
                    obstacles[i].active = false;
                }
            } else if (check_collision(&obstacles[i])) {
                crashed = true;
                game_state = SKI_STATE_GAMEOVER;
                return;
            }
        }
    }

    /* Update distance */
    distance += speed;

    /* Gradual difficulty: increase base speed over time */
    if (distance % 500 == 0 && distance > 0) {
        if (speed < 5 && !input_slowdown) speed++;
    }
}

/*
 * Event handler
 */
static void ski_handler(xgui_window_t* win, xgui_event_t* event) {
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        ski_window = NULL;
        game_state = SKI_STATE_MENU;
        return;
    }

    /* Printable keys (space, R) come through KEY_CHAR */
    if (event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;

        if (game_state == SKI_STATE_MENU) {
            if (c == ' ') {
                ski_reset();
                game_state = SKI_STATE_PLAYING;
                win->dirty = true;
            }
            return;
        }

        if (game_state == SKI_STATE_GAMEOVER) {
            if (c == 'r' || c == 'R') {
                ski_reset();
                game_state = SKI_STATE_PLAYING;
                win->dirty = true;
            }
            return;
        }
    }

    /* Arrow keys come through KEY_DOWN (no KEY_UP events in this system) */
    if (event->type == XGUI_EVENT_KEY_DOWN) {
        uint8_t key = event->key.keycode;
        if (key == KEY_LEFT)  { input_left = 2; input_right = 0; }
        if (key == KEY_RIGHT) { input_right = 2; input_left = 0; }
        if (key == KEY_DOWN)  input_speedup = 2;
        if (key == KEY_UP)    input_slowdown = 2;
    }
}

/*
 * Update callback — called from the main loop every frame
 */
void xgui_skigame_update(void) {
    if (!ski_window) return;
    if (game_state != SKI_STATE_PLAYING) return;

    ski_tick();
    xgui_window_invalidate(ski_window);
}

/*
 * Create the Ski Game window
 */
void xgui_skigame_create(void) {
    if (ski_window) {
        xgui_window_focus(ski_window);
        return;
    }

    game_state = SKI_STATE_MENU;

    ski_window = xgui_window_create("Ski Game", 100, 40, WIN_W, WIN_H,
                                     XGUI_WINDOW_DEFAULT);
    if (!ski_window) return;

    xgui_window_set_paint(ski_window, ski_paint);
    xgui_window_set_handler(ski_window, ski_handler);
    xgui_window_set_bgcolor(ski_window, XGUI_WHITE);
}
