/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#ifndef MOD_GOOGLE_TTS_H
#define MOD_GOOGLE_TTS_H

#include <switch.h>
#include <switch_stun.h>
#include <switch_curl.h>
#include <stdint.h>
#include <string.h>

#ifndef true
#define true SWITCH_TRUE
#endif
#ifndef false
#define false SWITCH_FALSE
#endif

#define VERSION             "1.0 (gcp-tts-api-v1_http)"
#define LOG_BUF_MAX_LEN     2*1024
//#define CURL_DEBUG_REQUESTS 1

typedef struct {
    switch_memory_pool_t    *pool;
    switch_file_handle_t    *fhnd;
    switch_file_t           *fd_tmp;
    char                    *post_buffer_ref;
    char                    *lang_code;
    char                    *gender;
    char                    *voice_name;
    char                    *dst_file;
    char                    *tmp_file;
    uint32_t                samplerate;
    uint32_t                channels;
    size_t                  pos_buffer_len;
    uint8_t                 fl_synth_success;
} tts_ctx_t;


/* utils.c */
char *lang2bcp47(const char *lng);
char *fmt_gemder2voice(const char *gender);
char *fmt_enct2enct(const char *fmt);
char *fmt_enct2fext(const char *fmt);
char *strnstr(const char *s, const char *find, size_t slen);

#endif
