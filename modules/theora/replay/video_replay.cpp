/**************************************************************************/
/*  video_replay.cpp                                                        */
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

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "video_replay.h"
#include "../editor/rgb2yuv.h"


Error VideoReplay::write_begin(const String& p_output_path) {
	ERR_FAIL_COND_V_MSG(is_initialized, ERR_ALREADY_EXISTS, "VideoReplay already initialized");

	output_file = FileAccess::open(p_output_path, FileAccess::WRITE_READ);
	ERR_FAIL_COND_V(output_file.is_null(), ERR_FILE_CANT_OPEN);

	target_resolution.x = (int)GLOBAL_GET("video_replay/width");
	target_resolution.y = (int)GLOBAL_GET("video_replay/height");
	fps = (int)GLOBAL_GET("video_replay/fps");
	frame_delay = 1000000 / fps;

	ogg_stream_init(&ogg_stream, rand());

	y = (uint8_t*)memalloc(target_resolution.x * target_resolution.y);
	u = (uint8_t*)memalloc(target_resolution.x * target_resolution.y / 4);
	v = (uint8_t*)memalloc(target_resolution.x * target_resolution.y / 4);

	ycbcr[0].width = target_resolution.x;
	ycbcr[0].height = target_resolution.y;
	ycbcr[0].stride = target_resolution.x;
	ycbcr[0].data = y;
	ycbcr[1].width = target_resolution.x / 2;
	ycbcr[1].height = target_resolution.y / 2;
	ycbcr[1].stride = target_resolution.x / 2;
	ycbcr[1].data = u;
	ycbcr[2].width = target_resolution.x / 2;
	ycbcr[2].height = target_resolution.y / 2;
	ycbcr[2].stride = target_resolution.x / 2;
	ycbcr[2].data = v;

	th_info_init(&theora_info);
	theora_info.frame_width = target_resolution.x;
	theora_info.frame_height = target_resolution.y;
	theora_info.pic_width = target_resolution.x;
	theora_info.pic_height = target_resolution.y;
	theora_info.pic_x = 0;
	theora_info.pic_y = 0;
	theora_info.fps_numerator = fps;
	theora_info.fps_denominator = 1;
	theora_info.aspect_numerator = 1;
	theora_info.aspect_denominator = 1;
	theora_info.colorspace = TH_CS_UNSPECIFIED;
	theora_info.target_bitrate = 0;
	theora_info.quality = 60;
	theora_info.pixel_fmt = TH_PF_420;
	theora_encoding_context = th_encode_alloc(&theora_info);
	th_info_clear(&theora_info);
	ERR_FAIL_NULL_V_MSG(theora_encoding_context, ERR_UNCONFIGURED, "Couldn't create a Theora encoder instance. Check that the video parameters are valid.");

	// Setting just the granule shift only allows power-of-two keyframe spacing.
	// Set the actual requested spacing.
	int ret = th_encode_ctl(theora_encoding_context, TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE, &keyframe_frequency, sizeof(keyframe_frequency));
	if (ret < 0) {
		ERR_PRINT("Couldn't set keyframe interval.");
	}

	// Write the bitstream header packets with proper page interleave.
	th_comment_init(&theora_comment);
	// The first packet will get its own page automatically.
	ogg_packet op;
	if (th_encode_flushheader(theora_encoding_context, &theora_comment, &op) <= 0) {
		ERR_FAIL_V_MSG(ERR_UNCONFIGURED, "Internal Theora library error.");
	}

	ogg_stream_packetin(&ogg_stream, &op);
	if (ogg_stream_pageout(&ogg_stream, &video_page) != 1) {
		ERR_FAIL_V_MSG(ERR_UNCONFIGURED, "Internal Ogg library error.");
	}
	output_file->store_buffer(video_page.header, video_page.header_len);
	output_file->store_buffer(video_page.body, video_page.body_len);

	// Create the remaining Theora headers.
	while (true) {
		ret = th_encode_flushheader(theora_encoding_context, &theora_comment, &op);
		if (ret < 0) {
			ERR_FAIL_V_MSG(ERR_UNCONFIGURED, "Internal Theora library error.");
		}
		else if (ret == 0) {
			break;
		}
		ogg_stream_packetin(&ogg_stream, &op);
	}

	// Flush the rest of our headers. This ensures the actual data in each stream will start on a new page, as per spec.
	while (true) {
		ret = ogg_stream_flush(&ogg_stream, &video_page);
		if (ret < 0) {
			ERR_FAIL_V_MSG(ERR_UNCONFIGURED, "Internal Ogg library error.");
		}
		else if (ret == 0) {
			break;
		}
		output_file->store_buffer(video_page.header, video_page.header_len);
		output_file->store_buffer(video_page.body, video_page.body_len);
	}

	is_working = true;
	is_initialized = true;
	last_frame_time = OS::get_singleton()->get_ticks_usec();
	encoder_thread = memnew(Thread);
	encoder_thread->start(&VideoReplay::thread_callback, this);
	return OK;
}


Error VideoReplay::write_frame(Ref<Image> p_image) {
	if (is_initialized) {
		uint64_t current_time = OS::get_singleton()->get_ticks_usec();
		if (current_time - frame_delay > last_frame_time)
		{
			last_frame_time = current_time;
			MutexLock lock(thread_mutex);
			frame_queue.push_back(p_image);
			queue_cvar.notify_one();
		}
	}
	return OK;
}


void VideoReplay::write_end() {
	is_working = false;
	queue_cvar.notify_one();
	if (encoder_thread) {
		encoder_thread->wait_to_finish();
		memdelete(encoder_thread);
		encoder_thread = nullptr;
	}
}


bool VideoReplay::process_frame(Ref<Image> frame) {
	frame->resize(target_resolution.x, target_resolution.y);
	PackedByteArray data = frame->get_data();
	if (frame->get_format() == Image::FORMAT_RGBA8) {
		rgba2yuv420(y, u, v, data.ptrw(), frame->get_width(), frame->get_height());
	}
	else {
		rgb2yuv420(y, u, v, data.ptrw(), frame->get_width(), frame->get_height());
	}
	if (th_encode_ycbcr_in(theora_encoding_context, ycbcr) != 0) {
		print_error("theora encode error");
		is_initialized = false;
		return false;
	}

	while (th_encode_packetout(theora_encoding_context, 0, &video_packet) > 0) {
		ogg_stream_packetin(&ogg_stream, &video_packet);

		while (ogg_stream_pageout(&ogg_stream, &video_page) != 0) {
			output_file->store_buffer(video_page.header, video_page.header_len);
			output_file->store_buffer(video_page.body, video_page.body_len);
			output_file->flush();
		}
	}
	return true;
}


void VideoReplay::thread_callback(void* p_user) {
	((VideoReplay*)p_user)->thread_method();
}


void VideoReplay::thread_method() {
	while (true) {
		Ref<Image> frame;
		{
			MutexLock lock(thread_mutex);
			if (!frame_queue.is_empty()) {
				frame = frame_queue.front()->get();
				frame_queue.pop_front();
			}
		}

		if (frame.is_valid()) {
			if (!process_frame(frame)) {
				break;
			}
			continue;
		}

		if (!is_working) {
			break;
		}

		{
			MutexLock lock(thread_mutex);
			if (frame_queue.is_empty()) {
				queue_cvar.wait(lock);
			}
		}
	}

	th_encode_free(theora_encoding_context);
	ogg_stream_clear(&ogg_stream);
	th_comment_clear(&theora_comment);

	memfree(y);
	memfree(u);
	memfree(v);
	output_file->close();
}
