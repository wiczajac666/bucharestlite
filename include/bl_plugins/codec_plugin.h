#ifndef BL_PLUGINS_CODEC_PLUGIN_H
#define BL_PLUGINS_CODEC_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BL_PLUGIN_ABI_VERSION 2u

typedef enum BlCodecType {
    BL_CODEC_VIDEO = 0,
    BL_CODEC_AUDIO = 1
} BlCodecType;

typedef enum BlCodecRole {
    BL_ROLE_DECODE = 1 << 0,
    BL_ROLE_ENCODE = 1 << 1
} BlCodecRole;

#define BL_FLAG_LOSSLESS      (1u << 0)
#define BL_FLAG_HWACCEL       (1u << 1)
#define BL_FLAG_EXPERIMENTAL  (1u << 2)
#define BL_FLAG_PASSTHROUGH   (1u << 3)

#define BL_PIXFMT_BGRA32      1u
#define BL_SAMPFMT_F32_PLANAR 1u

#define BL_PARAM_INT    0u
#define BL_PARAM_FLOAT  1u
#define BL_PARAM_BOOL   2u
#define BL_PARAM_ENUM   3u
#define BL_PARAM_STRING 4u

#define BL_OK                        0
#define BL_ERR_FILE_NOT_FOUND        (-1)
#define BL_ERR_DECODE_FAILED         (-2)
#define BL_ERR_ENCODE_FAILED         (-3)
#define BL_ERR_PLUGIN_ABI_MISMATCH   (-4)
#define BL_ERR_REGISTRY_DUPLICATE    (-5)
#define BL_ERR_INVALID_ARGUMENT      (-6)
#define BL_ERR_OUT_OF_MEMORY         (-7)
#define BL_ERR_CANCELLED             (-8)
#define BL_ERR_IO_ERROR              (-9)
#define BL_ERR_JSON_ERROR            (-10)
#define BL_ERR_INTERNAL              (-11)

#define BL_DECODE_NEED_MORE_INPUT    1

typedef struct BlRational {
    int32_t num;
    uint32_t den;
} BlRational;

typedef struct BlVideoInfo {
    uint32_t width;
    uint32_t height;
    BlRational fps;
    BlRational pixel_aspect;
    uint32_t pix_fmt;
} BlVideoInfo;

typedef struct BlAudioInfo {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t sample_fmt;
} BlAudioInfo;

typedef struct BlParamDesc {
    const char* name;
    uint8_t kind;
    double min;
    double max;
    double def;
    const char* const* enum_values;
    const char* help;
} BlParamDesc;

typedef struct BlCaps {
    uint32_t roles;
    uint32_t flags;
    const char* const* file_extensions;
    const char* ff_encoder;
    const char* ff_decoder;
    const BlParamDesc* params;
} BlCaps;

typedef struct BlHostApi {
    uint32_t host_abi_version;
    void* (*alloc)(size_t size, void* userdata);
    void (*free)(void* ptr, void* userdata);
    void* userdata;
} BlHostApi;

typedef enum BlValueType {
    BL_VALUE_INT = 0,
    BL_VALUE_FLOAT = 1,
    BL_VALUE_BOOL = 2,
    BL_VALUE_STRING = 3
} BlValueType;

typedef struct BlValue {
    uint8_t type;
    int64_t i;
    double f;
    const char* s;
} BlValue;

typedef struct BlConfigEntry {
    const char* name;
    BlValue value;
} BlConfigEntry;

typedef struct BlCodecConfig {
    uint32_t abi_version;
    const BlConfigEntry* params;
    const char* codec_name;
    const uint8_t* extradata;
    size_t extradata_size;
    BlVideoInfo video;
    BlAudioInfo audio;
    const BlHostApi* host;
} BlCodecConfig;

typedef struct BlFrameMeta {
    uint64_t pts;
    int keyframe;
    uint32_t width;
    uint32_t height;
    uint32_t linesize;
    uint32_t sample_count;
    uint32_t channels;
} BlFrameMeta;

typedef struct BlCodecPlugin {
    uint32_t abi_version;
    const char* name;
    const char* description;
    uint8_t type;
    BlCaps caps;

    int (*init)(void** ctx, const BlCodecConfig* cfg);
    int (*decode)(void* ctx, const uint8_t* pkt, size_t pkt_size,
                  uint8_t** out, size_t* out_size, BlFrameMeta* meta);
    int (*encode)(void* ctx, const uint8_t* in, size_t in_size,
                  uint8_t** out, size_t* out_size, const BlFrameMeta* meta);
    int (*flush)(void* ctx, uint8_t** out, size_t* out_size);
    void (*cleanup)(void* ctx);
} BlCodecPlugin;

extern BlCodecPlugin* bl_get_codec_plugin(void);

#ifdef __cplusplus
}
#endif

#endif /* BL_PLUGINS_CODEC_PLUGIN_H */
