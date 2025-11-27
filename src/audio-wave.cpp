#include "audio-wave.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#define BLOG(log_level, format, ...) \
	blog(log_level, "[audio-wave] " format, ##__VA_ARGS__)

static const char *kSourceId = "audio_wave_source";
static const char *kSourceName = "Audio Wave (Simple)";

static const char *SETTING_AUDIO_SOURCE = "audio_source";
static const char *SETTING_COLOR = "wave_color";
static const char *SETTING_COLOR2 = "wave_color2";
static const char *SETTING_GRADIENT_ENABLE = "use_gradient";
static const char *SETTING_WIDTH = "width";
static const char *SETTING_HEIGHT = "height";
static const char *SETTING_MODE = "draw_mode";
static const char *SETTING_AMPLITUDE = "amplitude";
static const char *SETTING_MIRROR = "mirror_wave";
static const char *SETTING_FRAME_RADIUS = "frame_radius";
static const char *SETTING_FRAME_DENSITY = "frame_density";

static struct obs_source_info audio_wave_source_info;

static void *audio_wave_create(obs_data_t *settings, obs_source_t *source);
static void audio_wave_destroy(void *data);
static void audio_wave_update(void *data, obs_data_t *settings);
static void audio_wave_get_defaults(obs_data_t *settings);
static obs_properties_t *audio_wave_get_properties(void *data);
static void audio_wave_show(void *data);
static void audio_wave_hide(void *data);
static uint32_t audio_wave_get_width(void *data);
static uint32_t audio_wave_get_height(void *data);
static void audio_wave_video_render(void *data, gs_effect_t *effect);

static bool gradient_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings);
static bool mode_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings);

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

/* ------------------------------------------------------------------------- */
/* Utility                                                                   */
/* ------------------------------------------------------------------------- */

static void set_solid_color(gs_eparam_t *param, uint32_t color)
{
	if (!param)
		return;

	vec4 c;
	const uint8_t r = color & 0xFF;
	const uint8_t g = (color >> 8) & 0xFF;
	const uint8_t b = (color >> 16) & 0xFF;

	c.x = r / 255.0f;
	c.y = g / 255.0f;
	c.z = b / 255.0f;
	c.w = 1.0f;

	gs_effect_set_vec4(param, &c);
}

static uint32_t lerp_color(uint32_t c1, uint32_t c2, float t)
{
	if (t < 0.0f)
		t = 0.0f;
	if (t > 1.0f)
		t = 1.0f;

	uint8_t r1 = c1 & 0xFF;
	uint8_t g1 = (c1 >> 8) & 0xFF;
	uint8_t b1 = (c1 >> 16) & 0xFF;

	uint8_t r2 = c2 & 0xFF;
	uint8_t g2 = (c2 >> 8) & 0xFF;
	uint8_t b2 = (c2 >> 16) & 0xFF;

	uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
	uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
	uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);

	return (uint32_t)((b << 16) | (g << 8) | r);
}

/* Sample point + normal on a rectangle/ellipse frame */
static void sample_frame_rect_ellipse(const audio_wave_source *s, float u, float radius_factor, float &x, float &y,
				      float &nx, float &ny)
{
	const float w = (float)s->width;
	const float h = (float)s->height;

	const float cx = w * 0.5f;
	const float cy = h * 0.5f;
	const float rx = w * 0.5f;
	const float ry = h * 0.5f;

	float t = u - std::floor(u);
	if (t < 0.0f)
		t += 1.0f;

	float xr = 0.0f, yr = 0.0f;
	float nxr = 0.0f, nyr = -1.0f;

	float edge = t * 4.0f;
	if (edge < 1.0f) {
		float f = edge;
		xr = f * (w - 1.0f);
		yr = 0.0f;
		nxr = 0.0f;
		nyr = -1.0f;
	} else if (edge < 2.0f) {
		float f = edge - 1.0f;
		xr = (w - 1.0f);
		yr = f * (h - 1.0f);
		nxr = 1.0f;
		nyr = 0.0f;
	} else if (edge < 3.0f) {
		float f = edge - 2.0f;
		xr = (1.0f - f) * (w - 1.0f);
		yr = (h - 1.0f);
		nxr = 0.0f;
		nyr = 1.0f;
	} else {
		float f = edge - 3.0f;
		xr = 0.0f;
		yr = (1.0f - f) * (h - 1.0f);
		nxr = -1.0f;
		nyr = 0.0f;
	}

	const float angle = t * 2.0f * (float)M_PI;

	float xe = cx + rx * std::cos(angle);
	float ye = cy + ry * std::sin(angle);
	float nxe = std::cos(angle);
	float nye = std::sin(angle);

	float f = std::clamp(radius_factor, 0.0f, 1.0f);

	x = xr * (1.0f - f) + xe * f;
	y = yr * (1.0f - f) + ye * f;
	nx = nxr * (1.0f - f) + nxe * f;
	ny = nyr * (1.0f - f) + nye * f;

	float len = std::sqrt(nx * nx + ny * ny);
	if (len > 0.0001f) {
		nx /= len;
		ny /= len;
	} else {
		nx = 0.0f;
		ny = -1.0f;
	}
}

/* ------------------------------------------------------------------------- */
/* Wave building                                                             */
/* ------------------------------------------------------------------------- */

void audio_wave_build_wave(audio_wave_source *s)
{
	if (!s)
		return;

	std::lock_guard<std::mutex> lock(s->audio_mutex);

	const size_t frames = s->num_samples;
	if (!frames || s->samples_left.empty()) {
		s->wave.clear();
		return;
	}

	const auto &L = s->samples_left;
	const auto &R = s->samples_right;

	s->wave.resize(frames);

	for (size_t i = 0; i < frames; ++i) {
		const float l = (i < L.size()) ? L[i] : 0.0f;
		const float r = (i < R.size()) ? R[i] : l;
		float m = s->gain * 0.5f * (std::fabs(l) + std::fabs(r));
		if (m > 1.0f)
			m = 1.0f;
		s->wave[i] = m;
	}
}

/* ------------------------------------------------------------------------- */
/* Rectangular / rounded frame modes                                         */
/* ------------------------------------------------------------------------- */

static void draw_rectangular_frame_wave(audio_wave_source *s, gs_eparam_t *color_param, bool filled)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const size_t frames = s->wave.size();
	if (!frames)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;

	const float max_bar_len = std::min(w, h) * 0.15f;

	const float perimeter_factor = (w + h) * 0.5f / 4.0f;
	const float density_factor = std::clamp((float)s->frame_density / 100.0f, 0.2f, 3.0f);

	uint32_t segments = (uint32_t)std::floor(perimeter_factor * density_factor);

	if (segments < 32)
		segments = 32;

	const float radius_factor = std::clamp((float)s->frame_radius / 100.0f, 0.0f, 1.0f);

	// Precompute base & outer points for filled mode
	std::vector<float> base_x, base_y, outer_x, outer_y;
	if (filled) {
		base_x.resize(segments);
		base_y.resize(segments);
		outer_x.resize(segments);
		outer_y.resize(segments);
	}

	/* Solid color path (non-gradient) is batched into one draw call */
	if (!s->use_gradient) {
		if (color_param)
			set_solid_color(color_param, s->color);

		if (!filled) {
			gs_render_start(true);
			for (uint32_t i = 0; i < segments; ++i) {
				const float u = (float)i / (float)segments;

				const size_t idx = (size_t)(u * (float)(frames - 1));
				const float v = s->wave[idx];
				const float bar_len = v * max_bar_len;

				float x, y, nx, ny;
				sample_frame_rect_ellipse(s, u, radius_factor, x, y, nx, ny);

				float x2 = x + nx * bar_len;
				float y2 = y + ny * bar_len;

				gs_vertex2f(x, y);
				gs_vertex2f(x2, y2);
			}
			gs_render_stop(GS_LINES);
		} else {
			// Filled ring: triangle strip using base + outer points
			for (uint32_t i = 0; i < segments; ++i) {
				const float u = (float)i / (float)segments;

				const size_t idx = (size_t)(u * (float)(frames - 1));
				const float v = s->wave[idx];
				const float bar_len = v * max_bar_len;

				float x, y, nx, ny;
				sample_frame_rect_ellipse(s, u, radius_factor, x, y, nx, ny);

				base_x[i] = x;
				base_y[i] = y;
				outer_x[i] = x + nx * bar_len;
				outer_y[i] = y + ny * bar_len;
			}

			gs_render_start(true);
			for (uint32_t i = 0; i < segments; ++i) {
				uint32_t idx = i;
				uint32_t next = (i + 1) % segments;

				gs_vertex2f(base_x[idx], base_y[idx]);
				gs_vertex2f(outer_x[idx], outer_y[idx]);
				gs_vertex2f(base_x[next], base_y[next]);

				gs_vertex2f(outer_x[idx], outer_y[idx]);
				gs_vertex2f(outer_x[next], outer_y[next]);
				gs_vertex2f(base_x[next], base_y[next]);
			}
			gs_render_stop(GS_TRIS);
		}

		return;
	}

	/* Gradient mode – keep per-segment color (less batching possible) */
	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;

		const size_t idx = (size_t)(u * (float)(frames - 1));
		const float v = s->wave[idx];
		const float bar_len = v * max_bar_len;

		float x, y, nx, ny;
		sample_frame_rect_ellipse(s, u, radius_factor, x, y, nx, ny);

		float x2 = x + nx * bar_len;
		float y2 = y + ny * bar_len;

		float color_t = u;
		if (color_param)
			set_solid_color(color_param, lerp_color(s->color, s->color2, color_t));

		if (!filled) {
			gs_render_start(true);
			gs_vertex2f(x, y);
			gs_vertex2f(x2, y2);
			gs_render_stop(GS_LINES);
		} else {
			// For gradient-filled ring, just draw thin quads segment by segment
			gs_render_start(true);
			gs_vertex2f(x, y);
			gs_vertex2f(x2, y2);
			gs_vertex2f(x, y);

			gs_vertex2f(x2, y2);
			gs_vertex2f(x2, y2);
			gs_vertex2f(x, y);
			gs_render_stop(GS_TRIS);
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Main drawing                                                              */
/* ------------------------------------------------------------------------- */

void audio_wave_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	audio_wave_build_wave(s);

	const size_t frames = s->wave.size();
	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;

	gs_matrix_push();

	if (frames < 2) {
		// Flat line when no data
		if (!s->use_gradient) {
			if (color_param)
				set_solid_color(color_param, s->color);

			gs_render_start(true);
			for (uint32_t x = 0; x < (uint32_t)w; ++x)
				gs_vertex2f((float)x, mid_y);
			gs_render_stop(GS_LINESTRIP);
		} else {
			for (uint32_t x = 0; x + 1 < (uint32_t)w; x += 2) {
				float t = w > 1.0f ? (float)x / (w - 1.0f) : 0.0f;
				if (color_param)
					set_solid_color(color_param, lerp_color(s->color, s->color2, t));

				gs_render_start(true);
				gs_vertex2f((float)x, mid_y);
				gs_vertex2f((float)(x + 1), mid_y);
				gs_render_stop(GS_LINES);
			}
		}

		gs_matrix_pop();
		return;
	}

	/* Rectangular / rounded modes (2 = line, 3 = filled) */
	if (s->mode == 2 || s->mode == 3) {
		const bool filled = (s->mode == 3);
		draw_rectangular_frame_wave(s, color_param, filled);
		gs_matrix_pop();
		return;
	}

	/* --------------------------------------------------------------------- */
	/* Horizontal Wave / Bars                                                */
	/* --------------------------------------------------------------------- */

	if (!s->use_gradient) {
		if (color_param)
			set_solid_color(color_param, s->color);

		if (s->mode == 0) {
			// Wave line (batched)
			gs_render_start(true);
			for (uint32_t x = 0; x < (uint32_t)w; ++x) {
				const size_t idx =
					(size_t)((double)x * (double)(frames - 1) / (double)std::max(1.0f, w - 1.0f));
				const float v = s->wave[idx];
				const float y = mid_y - v * (mid_y - 2.0f);
				gs_vertex2f((float)x, y);
			}
			gs_render_stop(GS_LINESTRIP);

			if (s->mirror) {
				gs_render_start(true);
				for (uint32_t x = 0; x < (uint32_t)w; ++x) {
					const size_t idx = (size_t)((double)x * (double)(frames - 1) /
								    (double)std::max(1.0f, w - 1.0f));
					const float v = s->wave[idx];
					const float y = mid_y - v * (mid_y - 2.0f);
					const float y_m = mid_y + (mid_y - y);
					gs_vertex2f((float)x, y_m);
				}
				gs_render_stop(GS_LINESTRIP);
			}
		} else {
			// Bars
			const uint32_t step = 3;
			gs_render_start(true);
			for (uint32_t x = 0; x < (uint32_t)w; x += step) {
				const size_t idx =
					(size_t)((double)x * (double)(frames - 1) / (double)std::max(1.0f, w - 1.0f));
				const float v = s->wave[idx];
				const float y = mid_y - v * (mid_y - 4.0f);

				gs_vertex2f((float)x, mid_y);
				gs_vertex2f((float)x, y);

				if (s->mirror) {
					const float y_m = mid_y + (mid_y - y);
					gs_vertex2f((float)x, mid_y);
					gs_vertex2f((float)x, y_m);
				}
			}
			gs_render_stop(GS_LINES);
		}
	} else {
		if (s->mode == 0) {
			float prev_x = 0.0f;
			float prev_y = mid_y;

			for (uint32_t x = 1; x < (uint32_t)w; ++x) {
				const size_t idx_prev = (size_t)((double)(x - 1) * (double)(frames - 1) /
								 (double)std::max(1.0f, w - 1.0f));
				const size_t idx_cur =
					(size_t)((double)x * (double)(frames - 1) / (double)std::max(1.0f, w - 1.0f));

				const float v_prev = s->wave[idx_prev];
				const float v_cur = s->wave[idx_cur];

				const float y_prev = mid_y - v_prev * (mid_y - 2.0f);
				const float y_cur = mid_y - v_cur * (mid_y - 2.0f);

				float t = w > 1.0f ? (float)x / (w - 1.0f) : 0.0f;
				if (color_param)
					set_solid_color(color_param, lerp_color(s->color, s->color2, t));

				gs_render_start(true);
				gs_vertex2f(prev_x, prev_y);
				gs_vertex2f((float)x, y_cur);
				gs_render_stop(GS_LINES);

				if (s->mirror) {
					const float y_prev_m = mid_y + (mid_y - y_prev);
					const float y_cur_m = mid_y + (mid_y - y_cur);

					gs_render_start(true);
					gs_vertex2f(prev_x, y_prev_m);
					gs_vertex2f((float)x, y_cur_m);
					gs_render_stop(GS_LINES);
				}

				prev_x = (float)x;
				prev_y = y_cur;
			}
		} else {
			const uint32_t step = 3;
			for (uint32_t x = 0; x < (uint32_t)w; x += step) {
				const size_t idx =
					(size_t)((double)x * (double)(frames - 1) / (double)std::max(1.0f, w - 1.0f));
				const float v = s->wave[idx];
				const float y = mid_y - v * (mid_y - 4.0f);

				float t = w > 1.0f ? (float)x / (w - 1.0f) : 0.0f;
				if (color_param)
					set_solid_color(color_param, lerp_color(s->color, s->color2, t));

				gs_render_start(true);
				gs_vertex2f((float)x, mid_y);
				gs_vertex2f((float)x, y);

				if (s->mirror) {
					const float y_m = mid_y + (mid_y - y);
					gs_vertex2f((float)x, mid_y);
					gs_vertex2f((float)x, y_m);
				}
				gs_render_stop(GS_LINES);
			}
		}
	}

	gs_matrix_pop();
}

/* ------------------------------------------------------------------------- */
/* Audio capture & source plumbing                                           */
/* ------------------------------------------------------------------------- */

static void release_audio_weak(audio_wave_source *s)
{
	if (!s || !s->audio_weak)
		return;

	obs_weak_source_release(s->audio_weak);
	s->audio_weak = nullptr;
}

static void attach_to_audio_source(audio_wave_source *s);

static bool enum_audio_sources(void *data, obs_source_t *source)
{
	obs_property_t *prop = (obs_property_t *)data;

	const char *id = obs_source_get_id(source);
	if (id && std::strcmp(id, kSourceId) == 0)
		return true;

	if (!obs_source_audio_active(source))
		return true;

	const char *name = obs_source_get_name(source);
	if (!name)
		return true;

	obs_property_list_add_string(prop, name, name);
	return true;
}

/* Optimized: no logging, minimal work, tiny lock window */
static void audio_capture_cb(void *param, obs_source_t *, const struct audio_data *audio, bool muted)
{
	auto *s = static_cast<audio_wave_source *>(param);
	if (!s || !audio)
		return;

	if (muted || audio->frames == 0 || !audio->data[0])
		return;

	const size_t frames = audio->frames;

	const float *left = reinterpret_cast<const float *>(audio->data[0]);
	const float *right = audio->data[1] ? reinterpret_cast<const float *>(audio->data[1]) : nullptr;

	std::lock_guard<std::mutex> lock(s->audio_mutex);

	if (s->samples_left.size() != frames)
		s->samples_left.resize(frames);
	if (s->samples_right.size() != frames)
		s->samples_right.resize(frames);

	std::memcpy(s->samples_left.data(), left, frames * sizeof(float));
	if (right)
		std::memcpy(s->samples_right.data(), right, frames * sizeof(float));
	else
		std::memcpy(s->samples_right.data(), left, frames * sizeof(float));

	s->num_samples = frames;
}

static void attach_to_audio_source(audio_wave_source *s)
{
	if (!s)
		return;

	release_audio_weak(s);

	if (s->audio_source_name.empty())
		return;

	obs_source_t *target = obs_get_source_by_name(s->audio_source_name.c_str());
	if (!target) {
		BLOG(LOG_WARNING, "Audio source '%s' not found", s->audio_source_name.c_str());
		return;
	}

	s->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, s);

	obs_source_release(target);

	BLOG(LOG_INFO, "Attached to audio source '%s'", s->audio_source_name.c_str());
}

static void detach_from_audio_source(audio_wave_source *s)
{
	if (!s || !s->audio_weak)
		return;

	obs_source_t *target = obs_weak_source_get_source(s->audio_weak);
	if (target) {
		obs_source_remove_audio_capture_callback(target, audio_capture_cb, s);
		obs_source_release(target);
	}

	release_audio_weak(s);
}

/* ------------------------------------------------------------------------- */
/* Properties & defaults                                                     */
/* ------------------------------------------------------------------------- */

static bool gradient_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	bool enabled = obs_data_get_bool(settings, SETTING_GRADIENT_ENABLE);
	obs_property_t *c2 = obs_properties_get(props, SETTING_COLOR2);
	if (c2)
		obs_property_set_visible(c2, enabled);

	return true;
}

static bool mode_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	const int mode = (int)obs_data_get_int(settings, SETTING_MODE);

	obs_property_t *mirror = obs_properties_get(props, SETTING_MIRROR);
	if (mirror)
		obs_property_set_visible(mirror, mode != 2 && mode != 3);

	obs_property_t *radius = obs_properties_get(props, SETTING_FRAME_RADIUS);
	obs_property_t *density = obs_properties_get(props, SETTING_FRAME_DENSITY);
	const bool rect_mode = (mode == 2 || mode == 3);

	if (radius)
		obs_property_set_visible(radius, rect_mode);
	if (density)
		obs_property_set_visible(density, rect_mode);

	return true;
}

static obs_properties_t *audio_wave_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_property_t *p_list = obs_properties_add_list(props, SETTING_AUDIO_SOURCE, "Audio Source",
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, p_list);

	obs_property_t *mode =
		obs_properties_add_list(props, SETTING_MODE, "Display Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, "Wave", 0);
	obs_property_list_add_int(mode, "Bars", 1);
	obs_property_list_add_int(mode, "Rectangular Wave", 2);
	obs_property_list_add_int(mode, "Rectangular Wave (Filled)", 3);
	obs_property_set_modified_callback(mode, mode_modified);

	obs_property_t *grad = obs_properties_add_bool(props, SETTING_GRADIENT_ENABLE, "Use Gradient");
	obs_property_set_modified_callback(grad, gradient_modified);

	obs_properties_add_color(props, SETTING_COLOR, "Wave Color");
	obs_property_t *c2 = obs_properties_add_color(props, SETTING_COLOR2, "Wave Color 2");
	obs_property_set_visible(c2, false);

	obs_properties_add_int_slider(props, SETTING_FRAME_RADIUS, "Corner Radius (%)", 0, 100, 1);
	obs_properties_add_int_slider(props, SETTING_FRAME_DENSITY, "Bar Density (%)", 10, 300, 5);

	obs_properties_add_int(props, SETTING_WIDTH, "Width", 64, 4096, 1);
	obs_properties_add_int(props, SETTING_HEIGHT, "Height", 32, 2048, 1);

	obs_properties_add_int_slider(props, SETTING_AMPLITUDE, "Amplitude (%)", 10, 400, 10);

	obs_properties_add_bool(props, SETTING_MIRROR, "Mirror wave horizontally");

	return props;
}

static void audio_wave_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTING_AUDIO_SOURCE, "");
	obs_data_set_default_int(settings, SETTING_WIDTH, 800);
	obs_data_set_default_int(settings, SETTING_HEIGHT, 200);
	obs_data_set_default_int(settings, SETTING_COLOR, 0xFFFFFF);
	obs_data_set_default_int(settings, SETTING_COLOR2, 0x00FF00);
	obs_data_set_default_bool(settings, SETTING_GRADIENT_ENABLE, false);
	obs_data_set_default_int(settings, SETTING_MODE, 0);
	obs_data_set_default_int(settings, SETTING_AMPLITUDE, 200);
	obs_data_set_default_bool(settings, SETTING_MIRROR, false);
	obs_data_set_default_int(settings, SETTING_FRAME_RADIUS, 0);
	obs_data_set_default_int(settings, SETTING_FRAME_DENSITY, 100);
}

/* ------------------------------------------------------------------------- */
/* Source lifecycle                                                          */
/* ------------------------------------------------------------------------- */

static void audio_wave_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);

	s->audio_source_name = obs_data_get_string(settings, SETTING_AUDIO_SOURCE);

	s->width = (int)obs_data_get_int(settings, SETTING_WIDTH);
	s->height = (int)obs_data_get_int(settings, SETTING_HEIGHT);
	s->color = (uint32_t)obs_data_get_int(settings, SETTING_COLOR);
	s->color2 = (uint32_t)obs_data_get_int(settings, SETTING_COLOR2);
	s->mode = (int)obs_data_get_int(settings, SETTING_MODE);
	s->use_gradient = obs_data_get_bool(settings, SETTING_GRADIENT_ENABLE);
	s->mirror = obs_data_get_bool(settings, SETTING_MIRROR);

	s->frame_radius = (int)obs_data_get_int(settings, SETTING_FRAME_RADIUS);
	s->frame_density = (int)obs_data_get_int(settings, SETTING_FRAME_DENSITY);

	int amp_pct = (int)obs_data_get_int(settings, SETTING_AMPLITUDE);
	if (amp_pct < 10)
		amp_pct = 10;
	if (amp_pct > 400)
		amp_pct = 400;
	s->gain = (float)amp_pct / 100.0f;

	if (s->width < 1)
		s->width = 1;
	if (s->height < 1)
		s->height = 1;

	attach_to_audio_source(s);
}

static void *audio_wave_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new audio_wave_source{};
	s->self = source;

	audio_wave_update(s, settings);

	BLOG(LOG_INFO, "Created Audio Wave source");

	return s;
}

static void audio_wave_destroy(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
	delete s;
}

static void audio_wave_show(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
	attach_to_audio_source(s);
}

static void audio_wave_hide(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
}

static uint32_t audio_wave_get_width(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	return s ? (uint32_t)s->width : 0;
}

static uint32_t audio_wave_get_height(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	return s ? (uint32_t)s->height : 0;
}

/* ------------------------------------------------------------------------- */
/* Render                                                                    */
/* ------------------------------------------------------------------------- */

static void audio_wave_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!solid)
		return;

	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	if (!tech)
		return;

	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		gs_technique_begin_pass(tech, i);
		audio_wave_draw(s, color_param);
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);
}

static const char *audio_wave_get_name(void *)
{
	return kSourceName;
}

/* ------------------------------------------------------------------------- */
/* Registration                                                              */
/* ------------------------------------------------------------------------- */

extern "C" void register_audio_wave_source(void)
{
	std::memset(&audio_wave_source_info, 0, sizeof(audio_wave_source_info));

	audio_wave_source_info.id = kSourceId;
	audio_wave_source_info.type = OBS_SOURCE_TYPE_INPUT;
	audio_wave_source_info.output_flags = OBS_SOURCE_VIDEO;

	audio_wave_source_info.get_name = audio_wave_get_name;
	audio_wave_source_info.create = audio_wave_create;
	audio_wave_source_info.destroy = audio_wave_destroy;
	audio_wave_source_info.update = audio_wave_update;
	audio_wave_source_info.get_defaults = audio_wave_get_defaults;
	audio_wave_source_info.get_properties = audio_wave_get_properties;
	audio_wave_source_info.show = audio_wave_show;
	audio_wave_source_info.hide = audio_wave_hide;
	audio_wave_source_info.get_width = audio_wave_get_width;
	audio_wave_source_info.get_height = audio_wave_get_height;
	audio_wave_source_info.video_render = audio_wave_video_render;

	obs_register_source(&audio_wave_source_info);

	BLOG(LOG_INFO, "Registered Audio Wave source as '%s'", kSourceId);
}
