#include "audio-wave.hpp"

#include <algorithm>
#include <cmath>

static void set_solid_color(gs_eparam_t *param, uint32_t color)
{
	if (!param)
		return;

	vec4 c;
	const uint8_t r =  color        & 0xFF;
	const uint8_t g = (color >> 8)  & 0xFF;
	const uint8_t b = (color >> 16) & 0xFF;

	c.x = r / 255.0f;
	c.y = g / 255.0f;
	c.z = b / 255.0f;
	c.w = 1.0f;

	gs_effect_set_vec4(param, &c);
}

static uint32_t lerp_color(uint32_t c1, uint32_t c2, float t)
{
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	uint8_t r1 =  c1        & 0xFF;
	uint8_t g1 = (c1 >> 8)  & 0xFF;
	uint8_t b1 = (c1 >> 16) & 0xFF;

	uint8_t r2 =  c2        & 0xFF;
	uint8_t g2 = (c2 >> 8)  & 0xFF;
	uint8_t b2 = (c2 >> 16) & 0xFF;

	uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
	uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
	uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);

	return (uint32_t)((b << 16) | (g << 8) | r);
}

static void sample_frame_rect_ellipse(const audio_wave_source *s,
                                      float u,
                                      float radius_factor,
                                      float &x, float &y,
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
		xr  = f * (w - 1.0f);
		yr  = 0.0f;
		nxr = 0.0f;
		nyr = -1.0f;
	} else if (edge < 2.0f) {
		float f = edge - 1.0f;
		xr  = (w - 1.0f);
		yr  = f * (h - 1.0f);
		nxr = 1.0f;
		nyr = 0.0f;
	} else if (edge < 3.0f) {
		float f = edge - 2.0f;
		xr  = (1.0f - f) * (w - 1.0f);
		yr  = (h - 1.0f);
		nxr = 0.0f;
		nyr = 1.0f;
	} else {
		float f = edge - 3.0f;
		xr  = 0.0f;
		yr  = (1.0f - f) * (h - 1.0f);
		nxr = -1.0f;
		nyr = 0.0f;
	}

	const float angle = t * 2.0f * (float)M_PI;

	float xe  = cx + rx * std::cos(angle);
	float ye  = cy + ry * std::sin(angle);
	float nxe = std::cos(angle);
	float nye = std::sin(angle);

	float f = std::clamp(radius_factor, 0.0f, 1.0f);

	x  = xr  * (1.0f - f) + xe  * f;
	y  = yr  * (1.0f - f) + ye  * f;
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

static void draw_rectangular_frame_wave(audio_wave_source *s,
                                        gs_eparam_t *color_param)
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
	const float density_factor =
		std::clamp((float)s->frame_density / 100.0f, 0.2f, 3.0f);

	uint32_t segments =
		(uint32_t)std::floor(perimeter_factor * density_factor);

	if (segments < 32)
		segments = 32;

	const float radius_factor =
		std::clamp((float)s->frame_radius / 100.0f, 0.0f, 1.0f);

	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;

		const size_t idx =
			(size_t)(u * (float)(frames - 1));
		const float v = s->wave[idx];
		const float bar_len = v * max_bar_len;

		float x, y, nx, ny;
		sample_frame_rect_ellipse(s, u, radius_factor, x, y, nx, ny);

		float x2 = x + nx * bar_len;
		float y2 = y + ny * bar_len;

		float color_t = u;

		if (!s->use_gradient) {
			if (color_param)
				set_solid_color(color_param, s->color);

			gs_render_start(true);
			gs_vertex2f(x, y);
			gs_vertex2f(x2, y2);
			gs_render_stop(GS_LINES);
		} else {
			if (color_param)
				set_solid_color(color_param,
				                lerp_color(s->color, s->color2, color_t));

			gs_render_start(true);
			gs_vertex2f(x, y);
			gs_vertex2f(x2, y2);
			gs_render_stop(GS_LINES);
		}
	}
}

void audio_wave_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	audio_wave_build_wave(s);

	const size_t frames = s->wave.size();
	const float  w      = (float)s->width;
	const float  h      = (float)s->height;
	const float  mid_y  = h * 0.5f;

	gs_matrix_push();

	if (frames < 2) {
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
					set_solid_color(color_param,
					                lerp_color(s->color, s->color2, t));

				gs_render_start(true);
				gs_vertex2f((float)x, mid_y);
				gs_vertex2f((float)(x + 1), mid_y);
				gs_render_stop(GS_LINES);
			}
		}

		gs_matrix_pop();
		return;
	}

	if (s->mode == 2) {
		draw_rectangular_frame_wave(s, color_param);
		gs_matrix_pop();
		return;
	}

	if (!s->use_gradient) {
		if (color_param)
			set_solid_color(color_param, s->color);

		if (s->mode == 0) {
			gs_render_start(true);
			for (uint32_t x = 0; x < (uint32_t)w; ++x) {
				const size_t idx =
					(size_t)((double)x * (double)(frames - 1) /
					         (double)std::max(1.0f, w - 1.0f));
				const float v = s->wave[idx];
				const float y = mid_y - v * (mid_y - 2.0f);
				gs_vertex2f((float)x, y);
			}
			gs_render_stop(GS_LINESTRIP);

			if (s->mirror) {
				gs_render_start(true);
				for (uint32_t x = 0; x < (uint32_t)w; ++x) {
					const size_t idx =
						(size_t)((double)x * (double)(frames - 1) /
						         (double)std::max(1.0f, w - 1.0f));
					const float v = s->wave[idx];
					const float y = mid_y - v * (mid_y - 2.0f);
					const float y_m = mid_y + (mid_y - y);
					gs_vertex2f((float)x, y_m);
				}
				gs_render_stop(GS_LINESTRIP);
			}
		} else {
			const uint32_t step = 3;
			gs_render_start(true);
			for (uint32_t x = 0; x < (uint32_t)w; x += step) {
				const size_t idx =
					(size_t)((double)x * (double)(frames - 1) /
					         (double)std::max(1.0f, w - 1.0f));
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
				const size_t idx_prev =
					(size_t)((double)(x - 1) * (double)(frames - 1) /
					         (double)std::max(1.0f, w - 1.0f));
				const size_t idx_cur =
					(size_t)((double)x * (double)(frames - 1) /
					         (double)std::max(1.0f, w - 1.0f));

				const float v_prev = s->wave[idx_prev];
				const float v_cur  = s->wave[idx_cur];

				const float y_prev = mid_y - v_prev * (mid_y - 2.0f);
				const float y_cur  = mid_y - v_cur  * (mid_y - 2.0f);

				float t = w > 1.0f ? (float)x / (w - 1.0f) : 0.0f;
				if (color_param)
					set_solid_color(color_param,
					                lerp_color(s->color, s->color2, t));

				gs_render_start(true);
				gs_vertex2f(prev_x, prev_y);
				gs_vertex2f((float)x, y_cur);
				gs_render_stop(GS_LINES);

				if (s->mirror) {
					const float y_prev_m = mid_y + (mid_y - y_prev);
					const float y_cur_m  = mid_y + (mid_y - y_cur);

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
					(size_t)((double)x * (double)(frames - 1) /
					         (double)std::max(1.0f, w - 1.0f));
				const float v = s->wave[idx];
				const float y = mid_y - v * (mid_y - 4.0f);

				float t = w > 1.0f ? (float)x / (w - 1.0f) : 0.0f;
				if (color_param)
					set_solid_color(color_param,
					                lerp_color(s->color, s->color2, t));

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
