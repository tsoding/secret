#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include <raylib.h>
#include <raymath.h>

#include <dlfcn.h>

#define NOB_IMPLEMENTATION
#include "nob.h"
#include "plug.h"

static void *libplug = NULL;

#define PLUG(name, ret, ...) static ret (*name)(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

static bool reload_libplug(const char *libplug_path)
{
    if (libplug != NULL) {
        dlclose(libplug);
    }

    libplug = dlopen(libplug_path, RTLD_NOW);
    if (libplug == NULL) {
        fprintf(stderr, "ERROR: %s\n", dlerror());
        return false;
    }

    #define PLUG(name, ...) \
        name = dlsym(libplug, #name); \
        if (name == NULL) { \
            fprintf(stderr, "ERROR: %s\n", dlerror()); \
            return false; \
        }
    LIST_OF_PLUGS
    #undef PLUG

    return true;
}

int main(int argc, char **argv)
{
    const char *program_name = nob_shift_args(&argc, &argv);

    if (argc <= 0) {
        fprintf(stderr, "Usage: %s <libplug.so>\n", program_name);
        fprintf(stderr, "ERROR: no animation dynamic library is provided\n");
        return 1;
    }

    const char *libplug_path = nob_shift_args(&argc, &argv);

    if (!reload_libplug(libplug_path)) return 1;

    float factor = 100.0f;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(16*factor, 9*factor, "Secret");
    InitAudioDevice();
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    plug_init();

    while (!WindowShouldClose()) {
        BeginDrawing();
        {
            if (IsKeyPressed(KEY_H)) {
                void *state = plug_pre_reload();
                reload_libplug(libplug_path);
                plug_post_reload(state);
            }
            if (IsKeyPressed(KEY_Q)) {
                plug_reset();
            }

            plug_update();
        }
        EndDrawing();
    }

    CloseWindow();

    return 0;
}
