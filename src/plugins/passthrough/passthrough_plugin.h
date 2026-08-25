#ifndef BL_PASSTHROUGH_PLUGIN_H
#define BL_PASSTHROUGH_PLUGIN_H

#include <bl_plugins/codec_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

BlCodecPlugin* bl_passthrough_video_plugin(void);
BlCodecPlugin* bl_passthrough_audio_plugin(void);

#ifdef __cplusplus
}
#endif

#endif /* BL_PASSTHROUGH_PLUGIN_H */
