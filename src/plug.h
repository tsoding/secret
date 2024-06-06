#ifndef PLUG_H_
#define PLUG_H_

// void plug_init(void)
// void *plug_pre_reload(void)
// void plug_post_reload(void *state)
// void plug_update(void)
// void plug_reset(void)

#define LIST_OF_PLUGS \
    PLUG(plug_init, void, void)         /* Initialize the plugin */ \
    PLUG(plug_pre_reload, void*, void)  /* Notify the plugin that it's about to get reloaded */ \
    PLUG(plug_post_reload, void, void*) /* Notify the plugin that it got reloaded */ \
    PLUG(plug_update, void, void)        /* Render next frame of the animation */ \
    PLUG(plug_reset, void, void)        /* Reset the state of the animation */ \

#endif // PLUG_H_
