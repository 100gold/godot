/**************************************************************************/
/*  video_replay.h                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "servers/movie_writer/movie_writer.h"

#include <theora/theoraenc.h>
#include "core/os/thread.h"
#include "core/os/condition_variable.h"
#include "core/os/mutex.h"


class VideoReplay : public Object {
	GDCLASS(VideoReplay, Object)

	Vector2i target_resolution;
	int fps;
	int frame_delay;

	uint64_t last_frame_time = 0;

	Thread* encoder_thread;
	volatile bool is_working = true;
	BinaryMutex thread_mutex;
	List<Ref<Image>> frame_queue;
	ConditionVariable queue_cvar;

	Ref<FileAccess> output_file;

	ogg_uint32_t keyframe_frequency = 64;

	ogg_stream_state ogg_stream;
	ogg_packet video_packet;
	ogg_page video_page;

	th_info theora_info;
	th_enc_ctx* theora_encoding_context;
	th_comment theora_comment;

	uint8_t* y, * u, * v;
	th_ycbcr_buffer ycbcr;

	static void thread_callback(void* p_user);
	void thread_method();
	bool process_frame(Ref<Image> frame);

public:
	bool is_initialized = false;

	Error write_begin(const String& p_output_path);
	Error write_frame(Ref<Image> p_image);
	void write_end();
};
