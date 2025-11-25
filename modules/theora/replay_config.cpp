/*  replay_config.cpp                                                        */
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

#include "video_replay_config.h"

#if defined(ENABLE_VIDEO_REPLAY)
#include "servers/rendering_server.h"
#include "replay/video_replay.h"
static VideoReplay* g_video_replay = nullptr;
#endif


void VideoReplayConfig::cleanup() {
#if defined(ENABLE_VIDEO_REPLAY)
	if (g_video_replay) {
		memdelete(g_video_replay);
		g_video_replay = nullptr;
	}
#endif
}


void VideoReplayConfig::_bind_methods() {
	ClassDB::bind_static_method("VideoReplayConfig", D_METHOD("setup", "output_path"), &VideoReplayConfig::setup);
}


Error VideoReplayConfig::setup(const String& p_output_path) {
#if defined(ENABLE_VIDEO_REPLAY)
	if (!g_video_replay) {
		g_video_replay = memnew(VideoReplay);
	}
	return g_video_replay->write_begin(p_output_path);
#else
	return ERR_UNAVAILABLE;
#endif
}


void VideoReplayConfig::add_frame() {
#if defined(ENABLE_VIDEO_REPLAY)
	if (!g_video_replay || !g_video_replay->is_initialized) {
		return;
	}

	RID main_vp_rid = RenderingServer::get_singleton()->viewport_find_from_screen_attachment(DisplayServer::MAIN_WINDOW_ID);
	RID main_vp_texture = RenderingServer::get_singleton()->viewport_get_texture(main_vp_rid);
	Ref<Image> vp_tex = RenderingServer::get_singleton()->texture_2d_get(main_vp_texture);
	if (RenderingServer::get_singleton()->viewport_is_using_hdr_2d(main_vp_rid)) {
		vp_tex->convert(Image::FORMAT_RGBA8);
		vp_tex->linear_to_srgb();
	}

	g_video_replay->write_frame(vp_tex);
#endif
}
  
