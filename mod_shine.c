/*
 * FreeSWITCH Module for MP3 recording
 * Copyright (C) 2013-2020, Seven Du <dujinfang@gmail.com>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH FreeSWITCH Module for MP3 recording
 *
 * The Initial Developer of the Original Code is
 * Seven Du <dujinfang@gmail.com>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 *
 * mod_shine.c -- Encode mp3 using shine
 *
 */
#include <switch.h>
#include "layer3.h"

#define DEFAULT_SAMPLE_RATE 32000

SWITCH_MODULE_LOAD_FUNCTION(mod_shine_load);
SWITCH_MODULE_DEFINITION(mod_shine, mod_shine_load, NULL, NULL);

struct shine_context {
	switch_file_t *fd;
	shine_config_t config;
	shine_t s;
	int16_t *buffer;
	size_t buffer_used;
};

typedef struct shine_context shine_context;

/* remove warning */
const long *rates = samplerates;
const int *bitrate = bitrates;

/* Print some info about what we're going to encode */
static void check_config(shine_config_t *config)
{
	static char *mode_names[4]    = { "stereo", "j-stereo", "dual-ch", "mono" };
	static char *demp_names[4]    = { "none", "50/15us", "", "CITT" };

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
		"MPEG-I layer III, %s  Psychoacoustic Model: Shine\n",
		mode_names[config->mpeg.mode]);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Bitrate: %d kbps\n", config->mpeg.bitr);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "De-emphasis: %s   %s %s\n",
		demp_names[config->mpeg.emph],
		((config->mpeg.original) ? "Original" : ""),
		((config->mpeg.copyright) ? "(C)" : ""));
	// switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Encoding \"%s\" to \"%s\"\n", infname, outfname);
}

static switch_status_t shine_file_open(switch_file_handle_t *handle, const char *path)
{
	shine_context *context;
	char *ext;
	unsigned int flags = 0;

	if ((ext = strrchr(path, '.')) == 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid Format\n");
		return SWITCH_STATUS_GENERR;
	}
	ext++;

	if ((context = switch_core_alloc(handle->memory_pool, sizeof(*context))) == 0) {
		return SWITCH_STATUS_MEMERR;
	}

	if (switch_test_flag(handle, SWITCH_FILE_FLAG_WRITE)) {
		flags |= SWITCH_FOPEN_WRITE | SWITCH_FOPEN_CREATE;
		if (switch_test_flag(handle, SWITCH_FILE_WRITE_APPEND) || switch_test_flag(handle, SWITCH_FILE_WRITE_OVER)) {
			flags |= SWITCH_FOPEN_READ;
		} else {
			flags |= SWITCH_FOPEN_TRUNCATE;
		}

	}

	if (switch_test_flag(handle, SWITCH_FILE_FLAG_READ)) {
		flags |= SWITCH_FOPEN_READ;
	}

	if (switch_file_open(&context->fd, path, flags, SWITCH_FPROT_UREAD | SWITCH_FPROT_UWRITE, handle->memory_pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error opening %s\n", path);
		return SWITCH_STATUS_GENERR;
	}

	if (switch_test_flag(handle, SWITCH_FILE_WRITE_APPEND)) {
		int64_t samples = 0;
		switch_file_seek(context->fd, SEEK_END, &samples);
		handle->pos = samples;
	}

	shine_set_config_mpeg_defaults(&(context->config.mpeg));

	context->config.mpeg.bitr = 48;
	context->config.wave.channels = 2;  // even setting channels = 1, the result will be 2 channels, why?
	context->config.wave.samplerate = DEFAULT_SAMPLE_RATE;
	context->s = shine_initialise(&context->config);
	context->buffer_used = 0;
	context->buffer = (int16_t *)malloc(2 * samp_per_frame * sizeof(int16_t));

	if (!context->buffer) return SWITCH_STATUS_MEMERR;

	if (context->config.wave.channels > 1) {
		context->config.mpeg.mode = STEREO;
	} else {
		context->config.mpeg.mode = MONO;
	}

	/* See if samplerate is valid */
	if (shine_find_samplerate_index(context->config.wave.samplerate) < 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unsupported samplerate");
	}

	/* See if bitrate is valid */
	if (shine_find_bitrate_index(context->config.mpeg.bitr) < 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unsupported bitrate\n");
	}

	check_config(&context->config);

	handle->samples = 0;
	handle->samplerate = DEFAULT_SAMPLE_RATE;
	// handle->channels = 1;
	handle->format = 0;
	handle->sections = 0;
	handle->seekable = 0;
	handle->speed = 0;
	handle->pos = 0;
	handle->private_info = context;
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Opening File [%s] %dhz\n", path, handle->samplerate);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t shine_file_truncate(switch_file_handle_t *handle, int64_t offset)
{
	shine_context *context = handle->private_info;
	switch_status_t status;

	if ((status = switch_file_trunc(context->fd, offset)) == SWITCH_STATUS_SUCCESS) {
		handle->pos = 0;
	}

	return status;

}

static switch_status_t shine_file_close(switch_file_handle_t *handle)
{
	shine_context *context = handle->private_info;
	long written = 0;
	size_t writing = 0;
	unsigned char *mp3_data;
	int padding = samp_per_frame - context->buffer_used;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	if (context->buffer_used > 0) {
		// padding with 0
		memset(context->buffer + context->buffer_used, 0, padding);
		memset(context->buffer + context->buffer_used + samp_per_frame, 0, padding);

		mp3_data = shine_encode_frame(context->s, (void *)context->buffer, &written);

		writing = written;

		if (writing && mp3_data) {
			status = switch_file_write(context->fd, mp3_data, &writing);
			if (status != SWITCH_STATUS_SUCCESS) goto end;
		}
	}

	// write any pendding data;
	mp3_data = shine_flush(context->s, &written);
	writing = written;
	switch_file_write(context->fd, mp3_data, &writing);

end:
	switch_file_close(context->fd);

	/* Close encoder. */
	shine_close(context->s);

	switch_safe_free(context->buffer);
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t shine_file_seek(switch_file_handle_t *handle, unsigned int *cur_sample, int64_t samples, int whence)
{
	return SWITCH_STATUS_FALSE;
}

static switch_status_t shine_file_read(switch_file_handle_t *handle, void *data, size_t *len)
{
	*len = 0;
	return SWITCH_STATUS_FALSE;
}

static switch_status_t shine_file_write(switch_file_handle_t *handle, void *data, size_t *len)
{
	shine_context *context = handle->private_info;
	long written = 0;
	size_t writing = 0;
	int16_t *samples = data;
	size_t left = *len;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "writing %ld buffer_used:%ld\n", *len, context->buffer_used);

	while (left > 0) {
		size_t room;
		int16_t *buf1 = context->buffer + context->buffer_used;
		int16_t *buf2 = buf1 + samp_per_frame;
		int i;

		assert(samp_per_frame > context->buffer_used);
		room = samp_per_frame - context->buffer_used;

		if (left < room) {
			for(i = 0; i < left; i++) {
				*(buf1 + i) = *samples++;
				*(buf2 + i) = handle->channels == 1 ? 0 : *samples++;
			}

			context->buffer_used += left;
			// switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "buffering %u samples\n", (uint32_t)left);
			return status;
		} else {
			unsigned char *mp3_data;

			for(i = 0; i < room; i++) {
				*(buf1 + i) = *samples++;
				*(buf2 + i) = 0;
				*(buf2 + i) = handle->channels == 1 ? 0 : *samples++;
			}

			mp3_data = shine_encode_frame(context->s, (void *)context->buffer, &written);

			writing = written;
			// switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "writing %ld %ld\n", *len, writing);
			if (writing && mp3_data) {
				status = switch_file_write(context->fd, mp3_data, &writing);
				if (status != SWITCH_STATUS_SUCCESS) return status;
			}
			left -= room;
			context->buffer_used = 0;
		}
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t shine_file_set_string(switch_file_handle_t *handle, switch_audio_col_t col, const char *string)
{
	return SWITCH_STATUS_FALSE;
}

static switch_status_t shine_file_get_string(switch_file_handle_t *handle, switch_audio_col_t col, const char **string)
{
	return SWITCH_STATUS_FALSE;
}

/* Registration */

static char *supported_formats[2] = { 0 };

SWITCH_MODULE_LOAD_FUNCTION(mod_shine_load)
{
	switch_file_interface_t *file_interface;

	supported_formats[0] = "mp3";

	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	file_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_FILE_INTERFACE);
	file_interface->interface_name = modname;
	file_interface->extens = supported_formats;
	file_interface->file_open = shine_file_open;
	file_interface->file_close = shine_file_close;
	file_interface->file_truncate = shine_file_truncate;
	file_interface->file_read = shine_file_read;
	file_interface->file_write = shine_file_write;
	file_interface->file_seek = shine_file_seek;
	file_interface->file_set_string = shine_file_set_string;
	file_interface->file_get_string = shine_file_get_string;

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4:
 */
