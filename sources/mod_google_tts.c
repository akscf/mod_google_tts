/**
 * Copyright (C) AlexandrinKS
 * https://akscf.me/
 **/
#include "mod_google_tts.h"

static struct {
    const char              *file_ext;
    const char              *cache_path;
    const char              *tmp_path;
    const char              *gender;
    const char              *encoding_format;
    const char              *user_agent;
    const char              *api_url;
    const char              *api_key;
    char                    *api_url_ep;
    uint32_t                request_timeout;
    uint8_t                 fl_voice_name_as_lang_code;
    uint8_t                 fl_log_gcp_request_error;
    uint8_t                 fl_cache_disabled;
} globals;

SWITCH_MODULE_LOAD_FUNCTION(mod_google_tts_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_google_tts_shutdown);
SWITCH_MODULE_DEFINITION(mod_google_tts, mod_google_tts_load, mod_google_tts_shutdown, NULL);

// ---------------------------------------------------------------------------------------------------------------------------------------------
static void delete_file(tts_ctx_t *tts_ctx) {
    if(tts_ctx->tmp_file) {
        unlink(tts_ctx->tmp_file);
    }
    if(tts_ctx->dst_file && globals.fl_cache_disabled) {
        unlink(tts_ctx->dst_file);
    }
}

static switch_status_t extract_audio(tts_ctx_t *tts_ctx) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    switch_memory_pool_t *pool = tts_ctx->pool;
    switch_file_t *fd = NULL;
    char *buf_in = NULL, *buf_out = NULL, *ptr = NULL;
    size_t file_size = 0, len = 0, dec_len = 0;
    uint32_t ofs1 = 0, ofs2 = 0;

    if(switch_file_open(&fd, tts_ctx->tmp_file, (SWITCH_FOPEN_READ | SWITCH_FOPEN_BINARY), (SWITCH_FPROT_UREAD | SWITCH_FPROT_UWRITE), pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Malformed media content (%s)\n", tts_ctx->tmp_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }
    file_size = switch_file_get_size(fd);
    if(file_size < 11) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Malformed media content (%s)\n", tts_ctx->tmp_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

    if((buf_in = switch_core_alloc(pool, file_size)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    len = file_size;
    status = switch_file_read(fd, buf_in, &len);
    if(status != SWITCH_STATUS_SUCCESS || len != file_size) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Couldn't read from file (%s)\n", tts_ctx->tmp_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }
    switch_file_close(fd); // src file

    if((ptr = strnstr(buf_in, "\"audioContent\"", len)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Malformed media content (%s)\n", tts_ctx->tmp_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }
    for(ofs1 = ((ptr - buf_in) + 14); ofs1 < len; ofs1++) {
        if(buf_in[ofs1] == '"') { ofs1++; break; }
    }
    if(ofs1 >= len) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Malformed media content (%s)\n", tts_ctx->tmp_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }
    for(ofs2 = len; ofs2 > ofs1; ofs2--) {
        if(buf_in[ofs2] == '"') { buf_in[ofs2]='\0'; ofs2--; break; }
    }
    if(ofs2 <= ofs1) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Malformed media content (%s)\n", tts_ctx->tmp_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }
    ptr = (void *)(buf_in + ofs1);
    len = (ofs2 - ofs1);
    dec_len = ((len * 3) / 4);

    if(dec_len < 4 ) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Malformed media content (%s)\n", tts_ctx->tmp_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

    if((buf_out = switch_core_alloc(pool, dec_len)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }

    len = switch_b64_decode(ptr, buf_out, dec_len);
    if(len != dec_len) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "len != dec_len (%d / %d)\n", (uint32_t) len, (uint32_t) dec_len);
        dec_len = len;
    }

    if(switch_file_open(&fd, tts_ctx->dst_file, (SWITCH_FOPEN_WRITE | SWITCH_FOPEN_CREATE | SWITCH_FOPEN_TRUNCATE| SWITCH_FOPEN_BINARY), (SWITCH_FPROT_UREAD | SWITCH_FPROT_UWRITE), pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Couldn't create output file (%s)\n", tts_ctx->dst_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }
    status = switch_file_write(fd, buf_out, &len);
    if(status != SWITCH_STATUS_SUCCESS || len != dec_len) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Couldn't write into file (%s)\n", tts_ctx->dst_file);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }
out:
    if(fd) {
        switch_file_close(fd);
    }
    return status;
}

void log_gcp_http_fault(tts_ctx_t *tts_ctx) {
    switch_memory_pool_t *pool = tts_ctx->pool;
    switch_file_t *fd = NULL;
    size_t file_size = 0, len = 0;
    char *buf = NULL;

    if(switch_file_open(&fd, tts_ctx->tmp_file, (SWITCH_FOPEN_READ | SWITCH_FOPEN_BINARY), (SWITCH_FPROT_UREAD | SWITCH_FPROT_UWRITE), pool) != SWITCH_STATUS_SUCCESS) {
        goto out;
    }

    file_size = switch_file_get_size(fd);
    if(!file_size) {
        goto out;
    }

    file_size = (file_size > LOG_BUF_MAX_LEN) ? LOG_BUF_MAX_LEN : file_size;
    if((buf = switch_core_alloc(pool, file_size + 1)) == NULL) {
        goto out;
    }

    len = file_size;
    if(switch_file_read(fd, buf, &len) != SWITCH_STATUS_SUCCESS) {
        goto out;
    }

    buf[len] = '\0';
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GCP-HTTP-FAULT: %s\n", buf);
out:
    if(fd) {
        switch_file_close(fd);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
static size_t curl_io_write_callback(char *buffer, size_t size, size_t nitems, void *user_data) {
    tts_ctx_t *tts_ctx = (tts_ctx_t *)user_data;
    size_t len = (size * nitems);
    size_t wlen = len;

    if(tts_ctx->fd_tmp) {
        if(switch_file_write(tts_ctx->fd_tmp, buffer, &wlen) != SWITCH_STATUS_SUCCESS || wlen != len) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "write failed (wlen != len) [%d / %d]\n", (uint32_t)wlen, (uint32_t)len);
            switch_file_close(tts_ctx->fd_tmp);
        }
    }

    return len;
}

static size_t curl_io_read_callback(char *buffer, size_t size, size_t nitems, void *user_data) {
    tts_ctx_t *tts_ctx = (tts_ctx_t *)user_data;
    size_t nmax = (size * nitems);
    size_t ncur = (tts_ctx->pos_buffer_len > nmax) ? nmax : tts_ctx->pos_buffer_len;

    memmove(buffer, tts_ctx->post_buffer_ref, ncur);
    tts_ctx->post_buffer_ref += ncur;
    tts_ctx->pos_buffer_len -= ncur;

    return ncur;
}

static switch_status_t curl_perform(tts_ctx_t *tts_ctx, char *text) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    CURL *chnd = NULL;
    switch_curl_slist_t *headers = NULL;
    switch_CURLcode http_resp = 0;
    const char *xgender = (tts_ctx->gender ? tts_ctx->gender : globals.gender);
    const char *ygender = (!globals.fl_voice_name_as_lang_code && tts_ctx->voice_name) ? tts_ctx->voice_name : NULL;
    char *pdata = NULL;
    char *qtext = NULL;

    qtext = switch_util_quote_shell_arg(text);
    pdata = switch_mprintf("{'input':{'text':%s},'voice':{'languageCode':'%s','ssmlGender':'%s'},'audioConfig':{'audioEncoding':'%s', 'sampleRateHertz':'%d'}}",
                           qtext, tts_ctx->lang_code, (ygender ? ygender : xgender), globals.encoding_format, tts_ctx->samplerate );

#ifdef CURL_DEBUG_REQUESTS
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "CURL: URL=[%s], PDATA=[%s]\n", globals.api_url_ep, pdata);
#endif

    tts_ctx->pos_buffer_len = strlen(pdata);
    tts_ctx->post_buffer_ref = pdata;

    chnd = switch_curl_easy_init();
    headers = switch_curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");

    switch_curl_easy_setopt(chnd, CURLOPT_HTTPHEADER, headers);
    switch_curl_easy_setopt(chnd, CURLOPT_POST, 1);
    switch_curl_easy_setopt(chnd, CURLOPT_NOSIGNAL, 1);
    switch_curl_easy_setopt(chnd, CURLOPT_READFUNCTION, curl_io_read_callback);
    switch_curl_easy_setopt(chnd, CURLOPT_READDATA, (void *) tts_ctx);
    switch_curl_easy_setopt(chnd, CURLOPT_WRITEFUNCTION, curl_io_write_callback);
    switch_curl_easy_setopt(chnd, CURLOPT_WRITEDATA, (void *) tts_ctx);

    if(globals.request_timeout > 0) {
        switch_curl_easy_setopt(chnd, CURLOPT_TIMEOUT, globals.request_timeout);
    }
    if(globals.user_agent) {
        switch_curl_easy_setopt(chnd, CURLOPT_USERAGENT, globals.user_agent);
    }

    switch_curl_easy_setopt(chnd, CURLOPT_URL, globals.api_url_ep);

    if(strncasecmp(globals.api_url_ep, "https", 5) == 0) {
        switch_curl_easy_setopt(chnd, CURLOPT_SSL_VERIFYPEER, 0);
        switch_curl_easy_setopt(chnd, CURLOPT_SSL_VERIFYHOST, 0);
    }

    if(switch_file_open(&tts_ctx->fd_tmp, tts_ctx->tmp_file, (SWITCH_FOPEN_WRITE | SWITCH_FOPEN_CREATE | SWITCH_FOPEN_TRUNCATE| SWITCH_FOPEN_BINARY), (SWITCH_FPROT_UREAD | SWITCH_FPROT_UWRITE), tts_ctx->pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't create temporary file (%s)\n", tts_ctx->tmp_file);
        status = SWITCH_STATUS_FALSE;
        goto out;
    }

    switch_curl_easy_perform(chnd);
    switch_curl_easy_getinfo(chnd, CURLINFO_RESPONSE_CODE, &http_resp);

    if(http_resp != 200) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "http-error=[%d] (%s)\n", http_resp, globals.api_url);
        status = SWITCH_STATUS_FALSE;
    }

out:
    if(chnd)    { switch_curl_easy_cleanup(chnd); }
    if(headers) { switch_curl_slist_free_all(headers); }

    if(tts_ctx->fd_tmp) {
        switch_file_close(tts_ctx->fd_tmp);
    }

    switch_safe_free(pdata);
    switch_safe_free(qtext);
    return status;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
static switch_status_t speech_open(switch_speech_handle_t *sh, const char *voice, int samplerate, int channels, switch_speech_flag_t *flags) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    char name_uuid[SWITCH_UUID_FORMATTED_LENGTH + 1] = { 0 };
    tts_ctx_t *tts_ctx = NULL;

    tts_ctx = switch_core_alloc(sh->memory_pool, sizeof(tts_ctx_t));
    tts_ctx->pool = sh->memory_pool;
    tts_ctx->fhnd = switch_core_alloc(tts_ctx->pool, sizeof(switch_file_handle_t));
    tts_ctx->voice_name = switch_core_strdup(tts_ctx->pool, voice);
    tts_ctx->lang_code = (globals.fl_voice_name_as_lang_code ?  lang2bcp47(voice) : "en-gb");
    tts_ctx->channels = channels;
    tts_ctx->samplerate = samplerate;
    tts_ctx->dst_file = NULL;

    switch_uuid_str((char *)name_uuid, sizeof(name_uuid));
    tts_ctx->tmp_file = switch_core_sprintf(tts_ctx->pool, "%s%s%s%s.bin", globals.tmp_path, SWITCH_PATH_SEPARATOR, "tmp-gcp-tts-", name_uuid);

    if(globals.fl_cache_disabled) {
        tts_ctx->dst_file = switch_core_sprintf(sh->memory_pool, "%s%s%s.%s", globals.cache_path, SWITCH_PATH_SEPARATOR, name_uuid, globals.file_ext);
    }

    sh->private_info = tts_ctx;

    return status;
}

static switch_status_t speech_close(switch_speech_handle_t *sh, switch_speech_flag_t *flags) {
    tts_ctx_t *tts_ctx = (tts_ctx_t *) sh->private_info;
    assert(tts_ctx != NULL);

    if(switch_test_flag(tts_ctx->fhnd, SWITCH_FILE_OPEN)) {
        switch_core_file_close(tts_ctx->fhnd);
    }

    delete_file(tts_ctx);

    return SWITCH_STATUS_SUCCESS;
}

static switch_status_t speech_feed_tts(switch_speech_handle_t *sh, char *text, switch_speech_flag_t *flags) {
    tts_ctx_t *tts_ctx = (tts_ctx_t *) sh->private_info;
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    char digest[SWITCH_MD5_DIGEST_STRING_SIZE + 1] = { 0 };

    assert(tts_ctx != NULL);

    if(!tts_ctx->dst_file) {
        switch_md5_string(digest, (void *) text, strlen(text));
        tts_ctx->dst_file = switch_core_sprintf(sh->memory_pool, "%s%s%s.%s", globals.cache_path, SWITCH_PATH_SEPARATOR, digest, globals.file_ext);
    }

    if(switch_file_exists(tts_ctx->dst_file, tts_ctx->pool) == SWITCH_STATUS_SUCCESS) {
        if((status = switch_core_file_open(tts_ctx->fhnd, tts_ctx->dst_file, tts_ctx->channels, tts_ctx->samplerate, (SWITCH_FILE_FLAG_READ | SWITCH_FILE_DATA_SHORT), NULL)) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't open file: %s\n", tts_ctx->dst_file);
            status = SWITCH_STATUS_FALSE;
            goto out;
        }
    } else {
        if((status = curl_perform(tts_ctx , text)) == SWITCH_STATUS_SUCCESS) {
            tts_ctx->fl_synth_success = true;

            if(switch_file_exists(tts_ctx->dst_file, tts_ctx->pool) != SWITCH_STATUS_SUCCESS) {
                if((status = extract_audio(tts_ctx)) != SWITCH_STATUS_SUCCESS) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't extract media from file: %s \n", tts_ctx->tmp_file);
                    status = SWITCH_STATUS_FALSE;
                    goto out;
                }
            }

            if((status = switch_core_file_open(tts_ctx->fhnd, tts_ctx->dst_file, tts_ctx->channels, tts_ctx->samplerate, (SWITCH_FILE_FLAG_READ | SWITCH_FILE_DATA_SHORT), NULL)) != SWITCH_STATUS_SUCCESS) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't open file: %s\n", tts_ctx->dst_file);
                status = SWITCH_STATUS_FALSE;
                goto out;
            }
        } else {
            if(globals.fl_log_gcp_request_error) {
                log_gcp_http_fault(tts_ctx);
            }
        }
    }
out:
    return status;
}

static switch_status_t speech_read_tts(switch_speech_handle_t *sh, void *data, size_t *data_len, switch_speech_flag_t *flags) {
    tts_ctx_t *tts_ctx = (tts_ctx_t *) sh->private_info;
    size_t len = (*data_len / 2);

    assert(tts_ctx != NULL);

    if(tts_ctx->fhnd->file_interface == NULL) {
        if(tts_ctx->fl_synth_success) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "file_interface == NULL (dst_file: %s)\n", tts_ctx->dst_file);
        }
        return SWITCH_STATUS_FALSE;
    }

    if(switch_core_file_read(tts_ctx->fhnd, data, &len) != SWITCH_STATUS_SUCCESS) {
        switch_core_file_close(tts_ctx->fhnd);
        return SWITCH_STATUS_FALSE;
    }

    *data_len = (len * 2);
    if(data_len == 0) {
        switch_core_file_close(tts_ctx->fhnd);
        return SWITCH_STATUS_BREAK;
    }

    return SWITCH_STATUS_SUCCESS;
}

static void speech_flush_tts(switch_speech_handle_t *sh) {
    tts_ctx_t *tts_ctx = (tts_ctx_t *) sh->private_info;
    assert(tts_ctx != NULL);

    if(tts_ctx->fhnd != NULL && tts_ctx->fhnd->file_interface != NULL) {
        switch_core_file_close(tts_ctx->fhnd);
    }
}

static void speech_text_param_tts(switch_speech_handle_t *sh, char *param, const char *val) {
    tts_ctx_t *tts_ctx = (tts_ctx_t *) sh->private_info;

    assert(tts_ctx != NULL);

    if(strcasecmp(param, "lang") == 0) {
        if(val) { tts_ctx->lang_code = lang2bcp47(val); }
    } else if(strcasecmp(param, "gender") == 0) {
        if(val) { tts_ctx->gender = fmt_gemder2voice(val); }
    }
}

static void speech_numeric_param_tts(switch_speech_handle_t *sh, char *param, int val) {
}

static void speech_float_param_tts(switch_speech_handle_t *sh, char *param, double val) {
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------------------------------------------------------------------------
#define CONFIG_NAME "google_tts.conf"
SWITCH_MODULE_LOAD_FUNCTION(mod_google_tts_load) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    switch_xml_t cfg, xml, settings, param;

    switch_speech_interface_t *speech_interface;

    memset(&globals, 0, sizeof(globals));

    if((xml = switch_xml_open_cfg(CONFIG_NAME, &cfg, NULL)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't open configuration file: %s\n", CONFIG_NAME);
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }

    if((settings = switch_xml_child(cfg, "settings"))) {
        for (param = switch_xml_child(settings, "param"); param; param = param->next) {
            char *var = (char *) switch_xml_attr_soft(param, "name");
            char *val = (char *) switch_xml_attr_soft(param, "value");

            if(!strcasecmp(var, "api-url")) {
                globals.api_url = switch_core_strdup(pool, val);
            } else if(!strcasecmp(var, "api-key")) {
                globals.api_key = switch_core_strdup(pool, val);
            } else if(!strcasecmp(var, "cache-path")) {
                globals.cache_path = switch_core_strdup(pool, val);
            } else if(!strcasecmp(var, "gender")) {
                globals.gender = switch_core_strdup(pool, val);
            } else if(!strcasecmp(var, "encoding-format")) {
                globals.encoding_format = switch_core_strdup(pool, val);
            } else if(!strcasecmp(var, "user-agent")) {
                globals.user_agent = switch_core_strdup(pool, val);
            } else if(!strcasecmp(var, "request-timeout")) {
                globals.request_timeout = atoi(val);
            } else if(!strcasecmp(var, "voice-name-as-language-code")) {
                globals.fl_voice_name_as_lang_code = (strcasecmp(val, "true") == 0 ? true : false);
            } else if(!strcasecmp(var, "log-gcp-request-errors")) {
                globals.fl_log_gcp_request_error = (strcasecmp(val, "true") == 0 ? true : false);
            } else if(!strcasecmp(var, "cache-disable")) {
                globals.fl_cache_disabled = (strcasecmp(val, "true") == 0 ? true : false);
            }
        }
    }

    if(!globals.api_url) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid parameter: api-url\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if(!globals.api_key) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid parameter: api-key\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }

    globals.api_url_ep = switch_string_replace(globals.api_url, "${api-key}", globals.api_key);
    globals.cache_path = (globals.cache_path == NULL ? "/tmp/gcp-tts-cache" : globals.cache_path);
    globals.tmp_path = SWITCH_GLOBAL_dirs.temp_dir;
    globals.gender = fmt_gemder2voice( (globals.gender == NULL ? "female" : globals.gender) );
    globals.encoding_format = fmt_enct2enct( (globals.encoding_format == NULL ? "mp3" : globals.encoding_format) );
    globals.file_ext = fmt_enct2fext(globals.encoding_format);

    if(!globals.api_url_ep) {
        globals.api_url_ep = strdup(globals.api_key);
    }

    if(switch_directory_exists(globals.cache_path, NULL) != SWITCH_STATUS_SUCCESS) {
        switch_dir_make(globals.cache_path, SWITCH_FPROT_OS_DEFAULT, NULL);
    }

    // -------------------------
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);
    speech_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_SPEECH_INTERFACE);
    speech_interface->interface_name = "google-tts";

    speech_interface->speech_open = speech_open;
    speech_interface->speech_close = speech_close;
    speech_interface->speech_feed_tts = speech_feed_tts;
    speech_interface->speech_read_tts = speech_read_tts;
    speech_interface->speech_flush_tts = speech_flush_tts;

    speech_interface->speech_text_param_tts = speech_text_param_tts;
    speech_interface->speech_numeric_param_tts = speech_numeric_param_tts;
    speech_interface->speech_float_param_tts = speech_float_param_tts;

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "GoogleTTS-%s\n", VERSION);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "cache=%s, path=%s\n", (globals.fl_cache_disabled ? "disabled" : "enabled"), globals.cache_path);
out:
    if(xml) {
        switch_xml_free(xml);
    }
    return status;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_google_tts_shutdown) {

    switch_safe_free(globals.api_url_ep);

    return SWITCH_STATUS_SUCCESS;
}