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
static const char *SETTING_WIDTH = "width";
static const char *SETTING_HEIGHT = "height";
static const char *SETTING_SHAPE = "shape_type";
static const char *SETTING_STYLE = "style_type";
static const char *SETTING_AMPLITUDE = "amplitude";
static const char *SETTING_MIRROR = "mirror_wave";
static const char *SETTING_FRAME_DENSITY = "frame_density";
static const char *SETTING_CURVE = "curve_power";

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

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

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

static inline float apply_curve(const audio_wave_source *s, float v)
{
	if (v < 0.0f)
		v = 0.0f;
	if (v > 1.0f)
		v = 1.0f;

	float p = s ? s->curve_power : 1.0f;
	if (p <= 0.0f)
		p = 1.0f;

	return powf(v, p);
}

struct shape_vertex {
	float x;
	float y;
};

static void build_shape_vertices(const audio_wave_source *s, std::vector<shape_vertex> &verts)
{
	verts.clear();

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float R = std::max(1.0f, std::min(w, h) * 0.5f - 1.0f);

	switch (s->shape) {
	case AUDIO_WAVE_SHAPE_RECT: {
		const float x0 = 0.0f;
		const float x1 = w - 1.0f;
		const float y0 = 0.0f;
		const float y1 = h - 1.0f;

		verts.push_back({x0, y0});
		verts.push_back({x1, y0});
		verts.push_back({x1, y1});
		verts.push_back({x0, y1});
		break;
	}
	case AUDIO_WAVE_SHAPE_CIRCLE: {
		const int segments = 64;
		for (int i = 0; i < segments; ++i) {
			const float t = (float)i / (float)segments;
			const float angle = t * 2.0f * (float)M_PI;
			const float x = cx + R * std::cos(angle);
			const float y = cy * 1.0f + R * std::sin(angle);
			verts.push_back({x, y});
		}
		break;
	}
	case AUDIO_WAVE_SHAPE_HEX: {
		const int sides = 6;
		for (int i = 0; i < sides; ++i) {
			const float angle = (2.0f * (float)M_PI * (float)i) / (float)sides;
			const float x = cx + R * std::cos(angle);
			const float y = cy + R * std::sin(angle);
			verts.push_back({x, y});
		}
		break;
	}
	case AUDIO_WAVE_SHAPE_STAR: {
		const int points = 5;
		const float R_outer = R;
		const float R_inner = R * 0.5f;

		for (int i = 0; i < points * 2; ++i) {
			const float angle = (float)i * (float)M_PI / (float)points;
			const float r = (i % 2 == 0) ? R_outer : R_inner;
			const float x = cx + r * std::cos(angle - (float)M_PI / 2.0f);
			const float y = cy + r * std::sin(angle - (float)M_PI / 2.0f);
			verts.push_back({x, y});
		}
		break;
	}
	case AUDIO_WAVE_SHAPE_TRIANGLE: {
		const int sides = 3;
		for (int i = 0; i < sides; ++i) {
			const float angle = (2.0f * (float)M_PI * (float)i) / (float)sides - (float)M_PI / 2.0f;
			const float x = cx + R * std::cos(angle);
			const float y = cy + R * std::sin(angle);
			verts.push_back({x, y});
		}
		break;
	}
	case AUDIO_WAVE_SHAPE_DIAMOND: {
		verts.push_back({cx, cy - R});
		verts.push_back({cx + R, cy});
		verts.push_back({cx, cy + R});
		verts.push_back({cx - R, cy});
		break;
	}
	default:
		verts.push_back({0.0f, 0.0f});
		verts.push_back({w - 1.0f, 0.0f});
		verts.push_back({w - 1.0f, h - 1.0f});
		verts.push_back({0.0f, h - 1.0f});
		break;
	}

	if (verts.size() < 3) {
		verts.clear();
		verts.push_back({0.0f, 0.0f});
		verts.push_back({w - 1.0f, 0.0f});
		verts.push_back({w - 1.0f, h - 1.0f});
	}
}

static void sample_frame_shape(const audio_wave_source *s, const std::vector<shape_vertex> &verts, float u, float &x,
			       float &y, float &nx, float &ny)
{
	if (verts.empty()) {
		x = y = nx = ny = 0.0f;
		return;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	float t = u - std::floor(u);
	if (t < 0.0f)
		t += 1.0f;

	const size_t N = verts.size();
	const float pos = t * (float)N;
	const size_t i = (size_t)std::floor(pos) % N;
	const float f = pos - std::floor(pos);

	const shape_vertex &v0 = verts[i];
	const shape_vertex &v1 = verts[(i + 1) % N];

	x = v0.x + (v1.x - v0.x) * f;
	y = v0.y + (v1.y - v0.y) * f;

	float dx = x - cx;
	float dy = y - cy;
	float len = std::sqrt(dx * dx + dy * dy);
	if (len > 1e-4f) {
		nx = dx / len;
		ny = dy / len;
	} else {
		nx = 0.0f;
		ny = -1.0f;
	}
}

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

static void draw_line_wave(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;

	const float mid_y = h * 0.5f;
	const float baseline = h;      
	const float top_margin = 2.0f;

	const uint32_t width_u = (uint32_t)w;
	if (width_u == 0)
		return;

	std::vector<float> amp(width_u);
	for (uint32_t x = 0; x < width_u; ++x) {
		const size_t idx = (size_t)((double)x * (double)(frames - 1) / (double)std::max(1.0f, w - 1.0f));
		amp[x] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	std::vector<float> amp_smooth;
	const bool smooth_needed =
		(s->style == AUDIO_WAVE_STYLE_WAVE_LINEAR_SMOOTH || s->style == AUDIO_WAVE_STYLE_WAVE_LINEAR_FILLED);

	if (smooth_needed && !amp.empty()) {
		amp_smooth.resize(amp.size());
		float prev = amp[0];
		amp_smooth[0] = prev;
		const float alpha = 0.15f;
		for (size_t i = 1; i < amp.size(); ++i) {
			prev = prev + alpha * (amp[i] - prev);
			amp_smooth[i] = prev;
		}
	}

	auto get_amp_raw = [&](uint32_t x) -> float {
		return smooth_needed ? amp_smooth[x] : amp[x];
	};

	if (color_param)
		set_solid_color(color_param, s->color);

	if (s->style == AUDIO_WAVE_STYLE_WAVE_LINE || s->style == AUDIO_WAVE_STYLE_WAVE_LINEAR_SMOOTH) {
		gs_render_start(true);
		for (uint32_t x = 0; x < width_u; ++x) {
			const float v_raw = get_amp_raw(x);
			const float v = apply_curve(s, v_raw);
			const float y = mid_y - v * (mid_y - top_margin);
			gs_vertex2f((float)x, y);
		}
		gs_render_stop(GS_LINESTRIP);

		if (s->mirror) {
			gs_render_start(true);
			for (uint32_t x = 0; x < width_u; ++x) {
				const float v_raw = get_amp_raw(x);
				const float v = apply_curve(s, v_raw);
				const float y = mid_y - v * (mid_y - top_margin);
				const float y_m = mid_y + (mid_y - y);
				gs_vertex2f((float)x, y_m);
			}
			gs_render_stop(GS_LINESTRIP);
		}
	} else if (s->style == AUDIO_WAVE_STYLE_WAVE_LINEAR_FILLED) {
		gs_render_start(true);
		for (uint32_t x = 0; x + 1 < width_u; ++x) {
			const float v1_raw = get_amp_raw(x);
			const float v2_raw = get_amp_raw(x + 1);

			const float v1 = apply_curve(s, v1_raw);
			const float v2 = apply_curve(s, v2_raw);

			const float y1 = top_margin + (1.0f - v1) * (h - top_margin);
			const float y2 = top_margin + (1.0f - v2) * (h - top_margin);

			gs_vertex2f((float)x, baseline);
			gs_vertex2f((float)x, y1);
			gs_vertex2f((float)(x + 1), baseline);
			gs_vertex2f((float)(x + 1), baseline);
			gs_vertex2f((float)x, y1);
			gs_vertex2f((float)(x + 1), y2);
		}
		gs_render_stop(GS_TRIS);
	} else {
		const uint32_t step = 3;
		gs_render_start(true);
		for (uint32_t x = 0; x < width_u; x += step) {
			const float v_raw = get_amp_raw(x);
			const float v = apply_curve(s, v_raw);
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
}

static void draw_shape_wave(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;

	const float max_bar_len = std::min(w, h) * 0.20f;

	std::vector<shape_vertex> verts;
	build_shape_vertices(s, verts);
	if (verts.empty())
		return;

	float density_raw = (float)s->frame_density;
	if (!std::isfinite(density_raw))
		density_raw = 100.0f;

	uint32_t segments = (uint32_t)std::clamp(density_raw * 4.0f, 32.0f, 2048.0f);

	if (segments == 0)
		return;

	std::vector<float> base_x(segments), base_y(segments);
	std::vector<float> nx(segments), ny(segments);
	std::vector<float> amp(segments), amp_smooth;

	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;

		float x, y, nxx, nyy;
		sample_frame_shape(s, verts, u, x, y, nxx, nyy);
		base_x[i] = x;
		base_y[i] = y;
		nx[i] = nxx;
		ny[i] = nyy;

		const size_t idx = (size_t)(u * (float)(frames - 1));
		amp[i] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	const bool smooth_needed =
		(s->style == AUDIO_WAVE_STYLE_WAVE_LINEAR_SMOOTH || s->style == AUDIO_WAVE_STYLE_WAVE_LINEAR_FILLED);

	if (smooth_needed && segments > 0) {
		amp_smooth.resize(segments);
		float prev = amp[0];
		amp_smooth[0] = prev;
		const float alpha = 0.15f;
		for (uint32_t i = 1; i < segments; ++i) {
			prev = prev + alpha * (amp[i] - prev);
			amp_smooth[i] = prev;
		}
	}

	auto get_amp_raw = [&](uint32_t i) -> float {
		return smooth_needed ? amp_smooth[i] : amp[i];
	};

	if (color_param)
		set_solid_color(color_param, s->color);

	if (s->style == AUDIO_WAVE_STYLE_WAVE_LINE || s->style == AUDIO_WAVE_STYLE_WAVE_LINEAR_SMOOTH) {
		gs_render_start(true);
		for (uint32_t i = 0; i < segments; ++i) {
			const float v_raw = get_amp_raw(i);
			const float v = apply_curve(s, v_raw);
			const float len = v * max_bar_len;
			const float x = base_x[i] + nx[i] * len;
			const float y = base_y[i] + ny[i] * len;
			gs_vertex2f(x, y);
		}
		gs_render_stop(GS_LINESTRIP);

		if (s->mirror) {
			gs_render_start(true);
			for (uint32_t i = 0; i < segments; ++i) {
				const float v_raw = get_amp_raw(i);
				const float v = apply_curve(s, v_raw);
				const float len = v * max_bar_len;
				const float x = base_x[i] - nx[i] * len;
				const float y = base_y[i] - ny[i] * len;
				gs_vertex2f(x, y);
			}
			gs_render_stop(GS_LINESTRIP);
		}
	} else if (s->style == AUDIO_WAVE_STYLE_WAVE_LINEAR_FILLED) {
		std::vector<float> outer_x(segments), outer_y(segments);
		std::vector<float> inner_x, inner_y;

		if (s->mirror) {
			inner_x.resize(segments);
			inner_y.resize(segments);
		}

		for (uint32_t i = 0; i < segments; ++i) {
			const float v_raw = get_amp_raw(i);
			const float v = apply_curve(s, v_raw);
			const float len = v * max_bar_len;

			outer_x[i] = base_x[i] + nx[i] * len;
			outer_y[i] = base_y[i] + ny[i] * len;

			if (s->mirror) {
				inner_x[i] = base_x[i] - nx[i] * len;
				inner_y[i] = base_y[i] - ny[i] * len;
			}
		}

		gs_render_start(true);
		for (uint32_t i = 0; i < segments; ++i) {
			uint32_t next = (i + 1) % segments;

			gs_vertex2f(base_x[i], base_y[i]);
			gs_vertex2f(outer_x[i], outer_y[i]);
			gs_vertex2f(base_x[next], base_y[next]);
			gs_vertex2f(outer_x[i], outer_y[i]);
			gs_vertex2f(outer_x[next], outer_y[next]);
			gs_vertex2f(base_x[next], base_y[next]);

			if (s->mirror) {
				gs_vertex2f(base_x[i], base_y[i]);
				gs_vertex2f(inner_x[i], inner_y[i]);
				gs_vertex2f(base_x[next], base_y[next]);

				gs_vertex2f(inner_x[i], inner_y[i]);
				gs_vertex2f(inner_x[next], inner_y[next]);
				gs_vertex2f(base_x[next], base_y[next]);
			}
		}
		gs_render_stop(GS_TRIS);
	} else {
		gs_render_start(true);
		for (uint32_t i = 0; i < segments; ++i) {
			const float v_raw = get_amp_raw(i);
			const float v = apply_curve(s, v_raw);
			const float len = v * max_bar_len;

			const float x1 = base_x[i];
			const float y1 = base_y[i];
			const float x2 = x1 + nx[i] * len;
			const float y2 = y1 + ny[i] * len;

			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);

			if (s->mirror) {
				const float x3 = x1 - nx[i] * len;
				const float y3 = y1 - ny[i] * len;
				gs_vertex2f(x1, y1);
				gs_vertex2f(x3, y3);
			}
		}
		gs_render_stop(GS_LINES);
	}
}

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
		if (color_param)
			set_solid_color(color_param, s->color);

		gs_render_start(true);
		for (uint32_t x = 0; x < (uint32_t)w; ++x)
			gs_vertex2f((float)x, mid_y);
		gs_render_stop(GS_LINESTRIP);

		gs_matrix_pop();
		return;
	}

	if (s->shape == AUDIO_WAVE_SHAPE_LINE) {
		draw_line_wave(s, color_param);
	} else {
		draw_shape_wave(s, color_param);
	}

	gs_matrix_pop();
}

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

static obs_properties_t *audio_wave_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_property_t *p_list = obs_properties_add_list(props, SETTING_AUDIO_SOURCE, "Audio Source",
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, p_list);

	obs_property_t *shape =
		obs_properties_add_list(props, SETTING_SHAPE, "Shape", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(shape, "Line (Horizontal)", AUDIO_WAVE_SHAPE_LINE);
	obs_property_list_add_int(shape, "Rectangle", AUDIO_WAVE_SHAPE_RECT);
	obs_property_list_add_int(shape, "Circle", AUDIO_WAVE_SHAPE_CIRCLE);
	obs_property_list_add_int(shape, "Hexagon", AUDIO_WAVE_SHAPE_HEX);
	obs_property_list_add_int(shape, "Star", AUDIO_WAVE_SHAPE_STAR);
	obs_property_list_add_int(shape, "Triangle", AUDIO_WAVE_SHAPE_TRIANGLE);
	obs_property_list_add_int(shape, "Diamond", AUDIO_WAVE_SHAPE_DIAMOND);

	obs_property_t *style =
		obs_properties_add_list(props, SETTING_STYLE, "Style", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(style, "Wave (Line)", AUDIO_WAVE_STYLE_WAVE_LINE);
	obs_property_list_add_int(style, "Wave (Bars)", AUDIO_WAVE_STYLE_WAVE_BARS);
	obs_property_list_add_int(style, "Wave (Linear Smooth)", AUDIO_WAVE_STYLE_WAVE_LINEAR_SMOOTH);
	obs_property_list_add_int(style, "Wave (Linear Filled)", AUDIO_WAVE_STYLE_WAVE_LINEAR_FILLED);

	obs_properties_add_color(props, SETTING_COLOR, "Wave Color");

	obs_properties_add_int_slider(props, SETTING_FRAME_DENSITY, "Shape Density (%)", 10, 300, 5);

	obs_properties_add_int(props, SETTING_WIDTH, "Width", 64, 4096, 1);
	obs_properties_add_int(props, SETTING_HEIGHT, "Height", 32, 2048, 1);

	obs_properties_add_int_slider(props, SETTING_AMPLITUDE, "Amplitude (%)", 10, 400, 10);

	obs_properties_add_int_slider(props, SETTING_CURVE, "Curve Power (%)", 20, 300, 5);

	obs_properties_add_bool(props, SETTING_MIRROR, "Mirror wave horizontally");

	return props;
}

static void audio_wave_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTING_AUDIO_SOURCE, "");
	obs_data_set_default_int(settings, SETTING_WIDTH, 800);
	obs_data_set_default_int(settings, SETTING_HEIGHT, 200);
	obs_data_set_default_int(settings, SETTING_COLOR, 0xFFFFFF);

	obs_data_set_default_int(settings, SETTING_SHAPE, AUDIO_WAVE_SHAPE_LINE);
	obs_data_set_default_int(settings, SETTING_STYLE, AUDIO_WAVE_STYLE_WAVE_LINE);

	obs_data_set_default_int(settings, SETTING_AMPLITUDE, 200);
	obs_data_set_default_int(settings, SETTING_CURVE, 100);
	obs_data_set_default_bool(settings, SETTING_MIRROR, false);
	obs_data_set_default_int(settings, SETTING_FRAME_DENSITY, 100);
}

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

	s->shape = (int)obs_data_get_int(settings, SETTING_SHAPE);
	s->style = (int)obs_data_get_int(settings, SETTING_STYLE);

	if (s->shape < AUDIO_WAVE_SHAPE_LINE || s->shape > AUDIO_WAVE_SHAPE_DIAMOND)
		s->shape = AUDIO_WAVE_SHAPE_LINE;
	if (s->style < AUDIO_WAVE_STYLE_WAVE_LINE || s->style > AUDIO_WAVE_STYLE_WAVE_LINEAR_FILLED)
		s->style = AUDIO_WAVE_STYLE_WAVE_LINE;

	s->mirror = obs_data_get_bool(settings, SETTING_MIRROR);

	s->frame_density = (int)obs_data_get_int(settings, SETTING_FRAME_DENSITY);
	if (s->frame_density < 10)
		s->frame_density = 10;
	if (s->frame_density > 300)
		s->frame_density = 300;

	int amp_pct = (int)obs_data_get_int(settings, SETTING_AMPLITUDE);
	amp_pct = std::clamp(amp_pct, 10, 400);
	s->gain = (float)amp_pct / 100.0f;

	int curve_pct = (int)obs_data_get_int(settings, SETTING_CURVE);
	curve_pct = std::clamp(curve_pct, 20, 300);
	s->curve_power = (float)curve_pct / 100.0f;

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

	{
		std::lock_guard<std::mutex> lock(s->audio_mutex);
		s->samples_left.clear();
		s->samples_right.clear();
		s->wave.clear();
		s->num_samples = 0;
	}

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
