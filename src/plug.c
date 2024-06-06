#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>
#include <raymath.h>
#include "plug.h"
#include "nob.h"

#define PLUG(name, ret, ...) ret name(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

#define NODE_RADIUS 3
#define NODES_BIN_PATH "./assets/data/nodes.bin"
#define GRID_ROWS 22
#define GRID_COLS 18
#define SAMPLE_COUNT 20
#define SAMPLE_AREA 0.08
#define SAMPLE_THRESHOLD 15
#define BITS_COUNT 6

typedef struct {
    float *items;
    int width, height, stride;
} Mat;

typedef struct {
    size_t size;
    Image image;
    Texture2D texture;
    float zoom;
    bool dragging;
    Vector2 target;
    Vector2 anchor;
    Vector2 nodes[4];
    Mat lum;
    Mat grad;
} Plug;

static Plug *p;

#define MAT_AT(mat, row, col) (mat).items[(row)*(mat).stride + (col)]

static Mat mat_alloc(int width, int height)
{
    Mat mat = {0};
    mat.items = malloc(sizeof(float)*width*height);
    assert(mat.items != NULL);
    mat.width = width;
    mat.height = height;
    mat.stride = width;
    return mat;
}

static float rgb_to_lum(Color color)
{
    Vector4 n = ColorNormalize(color);
    return 0.2126*n.x + 0.7152*n.y + 0.0722*n.z;
}

static void luminance(Image image, Mat lum)
{
    assert(image.width == lum.width);
    assert(image.height == lum.height);
    Color *pixels = image.data;
    for (int y = 0; y < lum.height; ++y) {
        for (int x = 0; x < lum.width; ++x) {
            MAT_AT(lum, y, x) = rgb_to_lum(pixels[y*image.width + x]);
        }
    }
}

static float sobel_filter_at(Mat mat, int cx, int cy)
{
    static float gx[3][3] = {
        {1.0, 0.0, -1.0},
        {2.0, 0.0, -2.0},
        {1.0, 0.0, -1.0},
    };

    static float gy[3][3] = {
        {1.0, 2.0, 1.0},
        {0.0, 0.0, 0.0},
        {-1.0, -2.0, -1.0},
    };

    float sx = 0.0;
    float sy = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int x = cx + dx;
            int y = cy + dy;
            float c = 0 <= x && x < mat.width && 0 <= y && y < mat.height ? MAT_AT(mat, y, x) : 0.0;
            sx += c*gx[dy + 1][dx + 1];
            sy += c*gy[dy + 1][dx + 1];
        }
    }
    return sqrtf(sx*sx + sy*sy);
}

static void sobel_filter(Mat mat, Mat grad)
{
    assert(mat.width == grad.width);
    assert(mat.height == grad.height);

    for (int cy = 0; cy < mat.height; ++cy) {
        for (int cx = 0; cx < mat.width; ++cx) {
            MAT_AT(grad, cy, cx) = sobel_filter_at(mat, cx, cy);
        }
    }
}

static void load_assets(void)
{
    // p->image = LoadImage("./assets/images/Screenshot 2024-06-05 16-13-57.png");
    // p->image = LoadImage("./assets/images/Screenshot 2024-06-05 17-28-51.png");
    p->image = LoadImage("./assets/images/Screenshot 2024-06-05 18-17-39.png");
    ImageFormat(&p->image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    p->lum = mat_alloc(p->image.width, p->image.height);
    luminance(p->image, p->lum);

    // p->grad = mat_alloc(p->image.width, p->image.height);
    // sobel_filter(p->lum, p->grad);

    Color *pixels = p->image.data;
    for (int y = 0; y < p->image.height; ++y) {
        for (int x = 0; x < p->image.width; ++x) {
            pixels[y*p->image.width + x] = ColorFromHSV(0, 0, MAT_AT(p->lum, y, x));
        }
    }

    p->texture = LoadTextureFromImage(p->image);

    int dataSize;
    unsigned char *data = LoadFileData(NODES_BIN_PATH, &dataSize);
    if (data) {
        if (dataSize == sizeof(p->nodes)) {
            memcpy(p->nodes, data, sizeof(p->nodes));
        } else {
            TraceLog(LOG_ERROR, "Unexpected size of %s. Expected %d, but got %d.", NODES_BIN_PATH, sizeof(p->nodes), dataSize);
        }
    }
}

static void unload_assets(void)
{
    UnloadImage(p->image);
    UnloadTexture(p->texture);
    free(p->lum.items);
    free(p->grad.items);
}

void plug_reset(void)
{
    p->zoom = 1.0;
    p->target = (Vector2){0};
}

void plug_init(void)
{
    p = malloc(sizeof(*p));
    assert(p != NULL);
    memset(p, 0, sizeof(*p));
    p->size = sizeof(*p);

    load_assets();
    plug_reset();
}

void *plug_pre_reload(void)
{
    unload_assets();
    return p;
}

void plug_post_reload(void *state)
{
    p = state;
    if (p->size < sizeof(*p)) {
        TraceLog(LOG_INFO, "Migrating plug state schema %zu bytes -> %zu bytes", p->size, sizeof(*p));
        p = realloc(p, sizeof(*p));
        p->size = sizeof(*p);
    }

    load_assets();
}

Vector2 map_point(float x, float y)
{
    Vector2 v1 = Vector2Lerp(p->nodes[0], p->nodes[1], x);
    Vector2 v2 = Vector2Lerp(p->nodes[2], p->nodes[3], x);
    return Vector2Lerp(v1, v2, y);
}

float rand_float(void)
{
    return (float) rand() / (float) RAND_MAX;
}

float sample_cell(size_t row, size_t col)
{
    float cell_width = 1.0/GRID_COLS;
    float cell_height = 1.0/GRID_ROWS;
    float x = col*cell_width;
    float y = row*cell_height;
    float sum = 0.0;
    for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
        float sx = x + cell_width*0.5 - cell_width*SAMPLE_AREA*0.5 + rand_float()*cell_width*SAMPLE_AREA;
        float sy = y + cell_height*0.5 - cell_height*SAMPLE_AREA*0.5 + rand_float()*cell_height*SAMPLE_AREA;
        Vector2 s = map_point(sx, sy);
        sum += MAT_AT(p->lum, (int)s.y, (int)s.x);
    }
    return sum;
}

void plug_update(void)
{
    Color background_color = ColorFromHSV(0, 0, 0.05);

    ClearBackground(background_color);

    Camera2D camera = {
        .target = p->target,
        .zoom = p->zoom,
        .offset = {GetScreenWidth()*0.5, GetScreenHeight()*0.5},
    };
    float wheel = GetMouseWheelMove();
    if (wheel < 0) {
        p->zoom -= 0.1;
    } else if (wheel > 0) {
        p->zoom += 0.1;
    }
    Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), camera);
    if (p->dragging) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
            p->dragging = false;
        }
    } else {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            p->dragging = true;
            p->anchor = mouse;
        }
    }
    if (IsKeyPressed(KEY_S)) {
        SaveFileData("./assets/data/nodes.bin", p->nodes, sizeof(p->nodes));
    }
    if (IsKeyPressed(KEY_P)) {
        for (size_t bit_col = 0; bit_col < GRID_COLS/BITS_COUNT; ++bit_col) {
            for (size_t row = 0; row < GRID_ROWS; ++row) {
                int value = 1;
                for (int offset = BITS_COUNT-1; offset >= 0; --offset) {
                    int bit = sample_cell(row, bit_col*BITS_COUNT + offset) < SAMPLE_THRESHOLD;
                    value = (value << 1) | bit;
                }
                printf("%c", value);
            }
        }
        printf("\n");
    }
    BeginMode2D(camera);
        Vector2 position = {0};
        DrawTextureV(p->texture, position, WHITE);
        for (size_t i = 0; i < NOB_ARRAY_LEN(p->nodes); ++i) {
            bool selected = IsKeyDown(KEY_ONE + i);
            if (selected && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                p->nodes[i] = mouse;
            }
            if (selected) {
                DrawCircleV(p->nodes[i], NODE_RADIUS, ColorAlpha(YELLOW, 0.75));
            } else {
                DrawCircleV(p->nodes[i], NODE_RADIUS, ColorAlpha(RED, 0.75));
            }
        }

        float cell_width = 1.0/GRID_COLS;
        float cell_height = 1.0/GRID_ROWS;
        for (size_t row = 0; row < GRID_ROWS; ++row) {
            for (size_t col = 0; col < GRID_COLS; ++col) {
                float x = col*cell_width;
                float y = row*cell_height;
                if (sample_cell(row, col) < SAMPLE_THRESHOLD) {
                    DrawCircleV(map_point(x + cell_width*0.5, y + cell_height*0.5), 2, RED);
                }
            }
        }
        if (p->dragging) {
            p->target = Vector2Subtract(p->target, Vector2Subtract(mouse, p->anchor));
        }
    EndMode2D();
}
