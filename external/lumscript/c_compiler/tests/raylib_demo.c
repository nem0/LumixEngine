#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
	#define LS_ALIGNOF(T) __alignof(T)
#else
	#define LS_ALIGNOF(T) __alignof__(T)
#endif

typedef struct { void* data; int64_t length; } ls_slice;
typedef struct { const char* data; int64_t length; } ls_string;
static bool ls_string_equal(ls_string a, ls_string b) { return a.length == b.length && memcmp(a.data, b.data, (size_t)a.length) == 0; }
static ls_string ls_string_from_cstr(const char* value) { return (ls_string){ value, value ? (int64_t)strlen(value) : 0 }; }

typedef enum RaylibKey { RaylibKey_Enter = 257, RaylibKey_Right = 262, RaylibKey_Left = 263, RaylibKey_Down = 264, RaylibKey_Up = 265 } RaylibKey;
typedef struct Color Color;
struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};
const int32_t board_width = 10;
const int32_t board_height = 20;
const int32_t cell_size = 32;
const int32_t board_left = 40;
const int32_t board_top = 40;
int32_t board[200];
int32_t piece[16];
int32_t piece_x = 3;
int32_t piece_y = 0;
int32_t piece_kind = 0;
int32_t score = 0;
bool game_over = false;
extern void InitWindow(int32_t width, int32_t height, const char* title);
extern void SetTargetFPS(int32_t fps);
extern bool WindowShouldClose(void);
extern bool IsKeyDown(int32_t key);
extern bool IsKeyPressed(int32_t key);
extern void BeginDrawing(void);
extern void EndDrawing(void);
extern void CloseWindow(void);
extern void ClearBackground(Color c);
extern void DrawRectangle(int32_t x, int32_t y, int32_t width, int32_t height, Color c);
extern void DrawRectangleLines(int32_t x, int32_t y, int32_t width, int32_t height, Color c);
extern void DrawText(const char* text, int32_t x, int32_t y, int32_t size, Color c);
void set_piece_shape(void);
bool can_move(int32_t dx, int32_t dy);
void rotate_piece(void);
void spawn_piece(void);
void lock_piece(void);
void clear_lines(void);
void draw_block(int32_t x, int32_t y, int32_t kind);
void draw_game(void);
void reset_game(void);
int32_t main(void);
void set_piece_shape(void) {
    for (int32_t i = 0; i <= 15; ++i) {
        {
            piece[i] = 0;
        }
        __lum_continue_0: ;
    }
    __lum_break_0: ;
    {
        int32_t __lum_match_0 = piece_kind;
        if ((__lum_match_0 == 0)) {
            piece[4] = 1;
            piece[5] = 1;
            piece[6] = 1;
            piece[7] = 1;
        }
        else if ((__lum_match_0 == 1)) {
            piece[1] = 1;
            piece[2] = 1;
            piece[5] = 1;
            piece[6] = 1;
        }
        else if ((__lum_match_0 == 2)) {
            piece[1] = 1;
            piece[4] = 1;
            piece[5] = 1;
            piece[6] = 1;
        }
        else if ((__lum_match_0 == 3)) {
            piece[1] = 1;
            piece[2] = 1;
            piece[4] = 1;
            piece[5] = 1;
        }
        else if ((__lum_match_0 == 4)) {
            piece[0] = 1;
            piece[1] = 1;
            piece[5] = 1;
            piece[6] = 1;
        }
        else if ((__lum_match_0 == 5)) {
            piece[0] = 1;
            piece[4] = 1;
            piece[5] = 1;
            piece[6] = 1;
        }
        else {
            piece[2] = 1;
            piece[4] = 1;
            piece[5] = 1;
            piece[6] = 1;
        }
    }
}
bool can_move(int32_t dx, int32_t dy) {
    for (int32_t i = 0; i <= 15; ++i) {
        {
            if ((piece[i] != 0)) {
                int32_t local_x = (i % 4);
                int32_t local_y = (i / 4);
                int32_t x = ((piece_x + local_x) + dx);
                int32_t y = ((piece_y + local_y) + dy);
                if ((((x < 0) || (x >= board_width)) || (y >= board_height))) {
                                        return false;
                }
                if ((y >= 0)) {
                    if ((board[(x + (y * board_width))] != 0)) {
                                                return false;
                    }
                }
            }
        }
        __lum_continue_1: ;
    }
    __lum_break_1: ;
        return true;
}
void rotate_piece(void) {
    int32_t rotated[16];
    for (int32_t y = 0; y <= 3; ++y) {
        {
            for (int32_t x = 0; x <= 3; ++x) {
                {
                    rotated[((x * 4) + (3 - y))] = piece[((y * 4) + x)];
                }
                __lum_continue_3: ;
            }
            __lum_break_3: ;
        }
        __lum_continue_2: ;
    }
    __lum_break_2: ;
    for (int32_t i = 0; i <= 15; ++i) {
        {
            piece[i] = rotated[i];
        }
        __lum_continue_4: ;
    }
    __lum_break_4: ;
}
void spawn_piece(void) {
    piece_x = 3;
    piece_y = 0;
    piece_kind = ((piece_kind + 1) % 7);
    set_piece_shape();
    if (!(can_move(0, 0))) {
        game_over = true;
    }
}
void lock_piece(void) {
    for (int32_t i = 0; i <= 15; ++i) {
        {
            if ((piece[i] != 0)) {
                int32_t x = (piece_x + (i % 4));
                int32_t y = (piece_y + (i / 4));
                if (((y >= 0) && (y < board_height))) {
                    board[(x + (y * board_width))] = (piece_kind + 1);
                }
            }
        }
        __lum_continue_5: ;
    }
    __lum_break_5: ;
}
void clear_lines(void) {
    int32_t y = (board_height - 1);
    while ((y >= 0)) {
        {
            bool full = true;
            for (int32_t x = 0; x <= 9; ++x) {
                {
                    if ((board[(x + (y * board_width))] == 0)) {
                        full = false;
                    }
                }
                __lum_continue_7: ;
            }
            __lum_break_7: ;
            if (!(full)) {
                y -= 1;
                                continue;
            }
            int32_t row = y;
            while ((row > 0)) {
                {
                    for (int32_t x = 0; x <= 9; ++x) {
                        {
                            board[(x + (row * board_width))] = board[(x + ((row - 1) * board_width))];
                        }
                        __lum_continue_9: ;
                    }
                    __lum_break_9: ;
                    row -= 1;
                }
                __lum_continue_8: ;
            }
            __lum_break_8: ;
            for (int32_t x = 0; x <= 9; ++x) {
                {
                    board[x] = 0;
                }
                __lum_continue_10: ;
            }
            __lum_break_10: ;
            score += 100;
        }
        __lum_continue_6: ;
    }
    __lum_break_6: ;
}
void draw_block(int32_t x, int32_t y, int32_t kind) {
    Color color = (Color){ 80, 80, 80, 255 };
    {
        int32_t __lum_match_1 = kind;
        if ((__lum_match_1 == 1)) {
            color = (Color){ 40, 190, 220, 255 };
        }
        else if ((__lum_match_1 == 2)) {
            color = (Color){ 240, 220, 50, 255 };
        }
        else if ((__lum_match_1 == 3)) {
            color = (Color){ 170, 70, 220, 255 };
        }
        else if ((__lum_match_1 == 4)) {
            color = (Color){ 60, 190, 80, 255 };
        }
        else if ((__lum_match_1 == 5)) {
            color = (Color){ 220, 60, 60, 255 };
        }
        else if ((__lum_match_1 == 6)) {
            color = (Color){ 60, 100, 220, 255 };
        }
        else if ((__lum_match_1 == 7)) {
            color = (Color){ 230, 140, 40, 255 };
        }
        else {
            color = (Color){ 80, 80, 80, 255 };
        }
    }
    DrawRectangle((board_left + (x * cell_size)), (board_top + (y * cell_size)), (cell_size - 1), (cell_size - 1), color);
}
void draw_game(void) {
    for (int32_t x = 0; x <= 9; ++x) {
        {
            for (int32_t y = 0; y <= 19; ++y) {
                {
                    int32_t value = board[(x + (y * board_width))];
                    if ((value != 0)) {
                        draw_block(x, y, value);
                    }
                }
                __lum_continue_12: ;
            }
            __lum_break_12: ;
        }
        __lum_continue_11: ;
    }
    __lum_break_11: ;
    for (int32_t i = 0; i <= 15; ++i) {
        {
            if ((piece[i] != 0)) {
                draw_block((piece_x + (i % 4)), (piece_y + (i / 4)), (piece_kind + 1));
            }
        }
        __lum_continue_13: ;
    }
    __lum_break_13: ;
    DrawRectangleLines((board_left - 2), (board_top - 2), ((board_width * cell_size) + 3), ((board_height * cell_size) + 3), (Color){ 30, 30, 30, 255 });
    DrawText(((ls_string){ "\x4C\x55\x4D\x53\x43\x52\x49\x50\x54\x20\x54\x45\x54\x52\x49\x53", 16 }).data, 420, 55, 24, (Color){ 30, 30, 30, 255 });
    DrawText(((ls_string){ "\x41\x72\x72\x6F\x77\x73\x3A\x20\x6D\x6F\x76\x65\x20\x2F\x20\x55\x70\x3A\x20\x72\x6F\x74\x61\x74\x65\x20\x2F\x20\x44\x6F\x77\x6E\x3A\x20\x64\x72\x6F\x70", 38 }).data, 420, 90, 18, (Color){ 50, 50, 50, 255 });
    DrawText(((ls_string){ "\x53\x63\x6F\x72\x65\x3A", 6 }).data, 420, 125, 20, (Color){ 50, 50, 50, 255 });
    if (game_over) {
        DrawText(((ls_string){ "\x47\x41\x4D\x45\x20\x4F\x56\x45\x52", 9 }).data, 420, 210, 32, (Color){ 220, 50, 50, 255 });
        DrawText(((ls_string){ "\x50\x72\x65\x73\x73\x20\x45\x6E\x74\x65\x72\x20\x74\x6F\x20\x72\x65\x73\x74\x61\x72\x74", 22 }).data, 420, 250, 20, (Color){ 50, 50, 50, 255 });
    }
}
void reset_game(void) {
    for (int32_t i = 0; i <= 199; ++i) {
        {
            board[i] = 0;
        }
        __lum_continue_14: ;
    }
    __lum_break_14: ;
    score = 0;
    game_over = false;
    piece_kind = -(1);
    spawn_piece();
}
int32_t main(void) {
    InitWindow(1200, 820, ((ls_string){ "\x4C\x75\x6D\x53\x63\x72\x69\x70\x74\x20\x54\x65\x74\x72\x69\x73", 16 }).data);
    SetTargetFPS(60);
    reset_game();
    int32_t gravity = 0;
    while (!(WindowShouldClose())) {
        {
            if (game_over) {
                if (IsKeyPressed((int32_t)RaylibKey_Enter)) {
                    reset_game();
                }
            } else {
                if (IsKeyPressed((int32_t)RaylibKey_Left)) {
                    if (can_move(-(1), 0)) {
                        piece_x -= 1;
                    }
                }
                if (IsKeyPressed((int32_t)RaylibKey_Right)) {
                    if (can_move(1, 0)) {
                        piece_x += 1;
                    }
                }
                if (IsKeyPressed((int32_t)RaylibKey_Up)) {
                    rotate_piece();
                    if (!(can_move(0, 0))) {
                        rotate_piece();
                        rotate_piece();
                        rotate_piece();
                    }
                }
                if (IsKeyDown((int32_t)RaylibKey_Down)) {
                    gravity = 30;
                }
                gravity += 1;
                if ((gravity >= 30)) {
                    gravity = 0;
                    if (can_move(0, 1)) {
                        piece_y += 1;
                    } else {
                        lock_piece();
                        clear_lines();
                        spawn_piece();
                    }
                }
            }
            BeginDrawing();
            ClearBackground((Color){ 245, 245, 245, 255 });
            draw_game();
            EndDrawing();
        }
        __lum_continue_15: ;
    }
    __lum_break_15: ;
    CloseWindow();
        return 0;
}
