// Minimal stand-in for miniaudio.h: just enough for beatmapper's stretch.cpp
// to compile unmodified.  The data-source vtable is kept so the harness can
// drive stretch_on_read / stretch_on_seek through it.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint64_t ma_uint64;
typedef uint32_t ma_uint32;
typedef uint8_t  ma_channel;
typedef int      ma_result;
typedef void     ma_data_source;
enum { MA_SUCCESS = 0, MA_AT_END = -17 };
typedef enum { ma_format_unknown = 0, ma_format_f32 = 5 } ma_format;
typedef enum { ma_standard_channel_map_default = 0 } ma_standard_channel_map;

typedef struct {
    ma_result (*onRead)(ma_data_source*, void*, ma_uint64, ma_uint64*);
    ma_result (*onSeek)(ma_data_source*, ma_uint64);
    ma_result (*onGetDataFormat)(ma_data_source*, ma_format*, ma_uint32*, ma_uint32*, ma_channel*, size_t);
    ma_result (*onGetCursor)(ma_data_source*, ma_uint64*);
    ma_result (*onGetLength)(ma_data_source*, ma_uint64*);
    ma_result (*onSetLooping)(ma_data_source*, int);
    ma_uint32 flags;
} ma_data_source_vtable;

typedef struct { const ma_data_source_vtable* vtable; } ma_data_source_config;
typedef struct { const ma_data_source_vtable* vtable; } ma_data_source_base;

static inline ma_data_source_config ma_data_source_config_init(void) { ma_data_source_config c = { 0 }; return c; }
static inline ma_result ma_data_source_init(const ma_data_source_config* c, ma_data_source* ds) {
    ((ma_data_source_base*)ds)->vtable = c->vtable; return MA_SUCCESS;
}
static inline void ma_data_source_uninit(ma_data_source*) {}
static inline void ma_channel_map_init_standard(ma_standard_channel_map, ma_channel* m, size_t cap, ma_uint32 ch) {
    for (size_t i = 0; i < cap && i < ch; i++) m[i] = (ma_channel)(i + 1);
}
