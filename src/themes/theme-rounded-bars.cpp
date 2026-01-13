#include "theme-rounded-bars.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <obs-module.h>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_rounded_bars = "rounded_bars";
static const char *k_theme_name_rounded_bars = "Rounded Wobble Bars";

// ─────────────────────────────────────────────
// Property keys
// ─────────────────────────────────────────────


static const char *RB_PROP_DB_FLOOR = "rb_db_floor";
static const char *RB_PROP_DB_TARGET = "rb_db_target";
static const char *RB_PROP_DB_REACT = "rb_db_react"; // from which dB to start reacting
static const char *RB_PROP_BAR_COUNT = "rb_bar_count";
static const char *RB_PROP_WOBBLE_INT = "rb_wobble_intensity";
static const char *RB_PROP_MIRROR_VERT = "rb_mirror_vertical"; // true vertical mirroring

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static inline float rb_db_from_amp(float a)
{
	if (a <= 1e-6f)
		return -120.0f;
	return 20.0f * log10f(a);
}

static inline float rb_clamp01(float v)
{
	if (v < 0.0f)
		return 0.0f;
	if (v > 1.0f)
		return 1.0f;
	return v;
}

// Full pill (both ends rounded).
// y_bottom: bottom of bar
// height:   full height including caps
// barWidth: full width
static void rb_draw_rounded_bar(float cx, float y_bottom, float height, float barWidth, int capSegments)
{
	if (height <= 0.0f || barWidth <= 0.0f)
		return;

	const float radius = barWidth * 0.5f;
	const float minHeight = 2.0f * radius;

	if (height < minHeight)
		height = minHeight;

	const float y_top = y_bottom - height;

	const float y_top_center = y_top + radius;
	const float y_bottom_center = y_bottom - radius;

	const float left = cx - radius;
	const float right = cx + radius;

	const float y_rect_top = y_top_center;
	const float y_rect_bottom = y_bottom_center;

	// Rectangle body
	if (y_rect_bottom > y_rect_top) {
		gs_render_start(true);
		// Tri 1
		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);
		// Tri 2
		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);
		gs_vertex2f(left, y_rect_top);
		gs_render_stop(GS_TRIS);
	}

	// Top cap (semicircle)
	{
		const int segments = std::max(capSegments, 4);
		const float step = (float)M_PI / (float)segments;

		gs_render_start(true);
		for (int i = 0; i < segments; ++i) {
			const float theta0 = step * (float)i;
			const float theta1 = step * (float)(i + 1);

			const float cx0 = cx;
			const float cy0 = y_top_center;

			const float x1 = cx + cosf(theta0) * radius;
			const float y1 = y_top_center - sinf(theta0) * radius;

			const float x2 = cx + cosf(theta1) * radius;
			const float y2 = y_top_center - sinf(theta1) * radius;

			gs_vertex2f(cx0, cy0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);
		}
		gs_render_stop(GS_TRIS);
	}

	// Bottom cap (semicircle)
	{
		const int segments = std::max(capSegments, 4);
		const float step = (float)M_PI / (float)segments;

		gs_render_start(true);
		for (int i = 0; i < segments; ++i) {
			const float theta0 = step * (float)i;
			const float theta1 = step * (float)(i + 1);

			const float cx0 = cx;
			const float cy0 = y_bottom_center;

			const float x1 = cx + cosf(theta0) * radius;
			const float y1 = y_bottom_center + sinf(theta0) * radius;

			const float x2 = cx + cosf(theta1) * radius;
			const float y2 = y_bottom_center + sinf(theta1) * radius;

			gs_vertex2f(cx0, cy0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);
		}
		gs_render_stop(GS_TRIS);
	}
}

// Half bar: flat at bottom (center line), rounded at top.
// centerY:  where the flat edge sits (center line).
// halfHeight: total height from center to top (including rounded cap).
static void rb_draw_rounded_bar_half_up(float cx, float centerY, float halfHeight, float barWidth, int capSegments)
{
	if (halfHeight <= 0.0f || barWidth <= 0.0f)
		return;

	const float radius = barWidth * 0.5f;
	const float minHeight = radius; // at least enough for a cap
	if (halfHeight < minHeight)
		halfHeight = minHeight;

	const float y_bottom = centerY; // flat edge at center
	const float y_top = centerY - halfHeight;

	const float left = cx - radius;
	const float right = cx + radius;

	const float y_cap_center = y_top + radius; // cap center
	const float y_rect_top = y_cap_center;
	const float y_rect_bottom = y_bottom;

	// Rectangle body (from cap center down to center line)
	if (y_rect_bottom > y_rect_top) {
		gs_render_start(true);
		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);

		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);
		gs_vertex2f(left, y_rect_top);
		gs_render_stop(GS_TRIS);
	}

	// Top cap (rounded)
	{
		const int segments = std::max(capSegments, 4);
		const float step = (float)M_PI / (float)segments;

		gs_render_start(true);
		for (int i = 0; i < segments; ++i) {
			const float theta0 = step * (float)i;
			const float theta1 = step * (float)(i + 1);

			const float cx0 = cx;
			const float cy0 = y_cap_center;

			const float x1 = cx + cosf(theta0) * radius;
			const float y1 = y_cap_center - sinf(theta0) * radius;

			const float x2 = cx + cosf(theta1) * radius;
			const float y2 = y_cap_center - sinf(theta1) * radius;

			gs_vertex2f(cx0, cy0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);
		}
		gs_render_stop(GS_TRIS);
	}
}

// Half bar: flat at top (center line), rounded at bottom.
// centerY:    where the flat edge sits (center line).
// halfHeight: total height from center to bottom (including rounded cap).
static void rb_draw_rounded_bar_half_down(float cx, float centerY, float halfHeight, float barWidth, int capSegments)
{
	if (halfHeight <= 0.0f || barWidth <= 0.0f)
		return;

	const float radius = barWidth * 0.5f;
	const float minHeight = radius;
	if (halfHeight < minHeight)
		halfHeight = minHeight;

	const float y_top = centerY; // flat edge at center
	const float y_bottom = centerY + halfHeight;

	const float left = cx - radius;
	const float right = cx + radius;

	const float y_cap_center = y_bottom - radius;
	const float y_rect_top = y_top;
	const float y_rect_bottom = y_cap_center;

	// Rectangle body (from center line down to cap center)
	if (y_rect_bottom > y_rect_top) {
		gs_render_start(true);
		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);

		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);
		gs_vertex2f(left, y_rect_top);
		gs_render_stop(GS_TRIS);
	}

	// Bottom cap (rounded)
	{
		const int segments = std::max(capSegments, 4);
		const float step = (float)M_PI / (float)segments;

		gs_render_start(true);
		for (int i = 0; i < segments; ++i) {
			const float theta0 = step * (float)i;
			const float theta1 = step * (float)(i + 1);

			const float cx0 = cx;
			const float cy0 = y_cap_center;

			const float x1 = cx + cosf(theta0) * radius;
			const float y1 = y_cap_center + sinf(theta0) * radius;

			const float x2 = cx + cosf(theta1) * radius;
			const float y2 = y_cap_center + sinf(theta1) * radius;

			gs_vertex2f(cx0, cy0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);
		}
		gs_render_stop(GS_TRIS);
	}
}

// ─────────────────────────────────────────────
// Theme data
// ─────────────────────────────────────────────

struct rounded_bars_theme_data {
	std::vector<float> value;    // current displayed extra half-height
	std::vector<float> velocity; // spring velocity per bar
	bool initialized = false;

	float db_floor = -50.0f;
	float db_target = -10.0f;
	float db_react = -40.0f; // from which dB to start reacting

	uint32_t bars = 32;

	// mapped from wobble intensity slider
	float wobble_stiffness = 0.20f;
	float wobble_damping = 0.80f;

	bool mirror_vertical = false;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void rounded_bars_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_int_slider(props, RB_PROP_DB_FLOOR, "Floor dB (silence)", -60, 0, 1);
	obs_properties_add_int_slider(props, RB_PROP_DB_REACT, "React from (dB)", -60, 0, 1);
	obs_properties_add_int_slider(props, RB_PROP_DB_TARGET, "Full Extra Height dB", -60, 0, 1);

	obs_properties_add_int_slider(props, RB_PROP_BAR_COUNT, "Bars", 8, 128, 1);

	obs_properties_add_int_slider(props, RB_PROP_WOBBLE_INT, "Wobble Intensity", 0, 100, 1);

	obs_properties_add_bool(props, RB_PROP_MIRROR_VERT, "Mirror Vertically");
}

static void rounded_bars_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	int db_floor = aw_get_int_default(settings, RB_PROP_DB_FLOOR, -50);
	int db_target = aw_get_int_default(settings, RB_PROP_DB_TARGET, -10);
	int db_react = aw_get_int_default(settings, RB_PROP_DB_REACT, -40);

	db_floor = std::clamp(db_floor, -60, 0);
	db_target = std::clamp(db_target, -60, 0);
	db_react = std::clamp(db_react, -60, 0);

	// ensure sensible ordering: floor <= react < target
	if (db_react < db_floor)
		db_react = db_floor;
	if (db_target <= db_react)
		db_target = db_react + 1;

	int bars = aw_get_int_default(settings, RB_PROP_BAR_COUNT, 32);
	bars = std::clamp(bars, 8, 128);

	int wobble_int = aw_get_int_default(settings, RB_PROP_WOBBLE_INT, 60);
	wobble_int = std::clamp(wobble_int, 0, 100);

	bool mirror_vertical = obs_data_get_bool(settings, RB_PROP_MIRROR_VERT);

	// map wobble_int -> spring params
	// more intensity -> stronger spring & less damping (more wobble)
	float stiffness = 0.10f + (float)wobble_int * 0.004f; // ~0.10 .. 0.50
	float damping = 0.95f - (float)wobble_int * 0.004f;   // ~0.95 .. 0.55

	auto *d = static_cast<rounded_bars_theme_data *>(s->theme_data);
	if (!d) {
		d = new rounded_bars_theme_data{};
		s->theme_data = d;
	}

	d->db_floor = (float)db_floor;
	d->db_target = (float)db_target;
	d->db_react = (float)db_react;
	d->bars = (uint32_t)bars;
	d->wobble_stiffness = stiffness;
	d->wobble_damping = damping;
	d->mirror_vertical = mirror_vertical;

	d->initialized = false;
}

// ─────────────────────────────────────────────
// Drawing (centered, true vertical mirror, no background)
// ─────────────────────────────────────────────

static void rounded_bars_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<rounded_bars_theme_data *>(s->theme_data);
	if (!d) {
		d = new rounded_bars_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;

	const uint32_t bars = std::max(d->bars, 8u);

	
	// ensure buffers
	if (d->value.size() != bars) {
		d->value.assign(bars, 0.0f);
		d->velocity.assign(bars, 0.0f);
		d->initialized = false;
	}

	// layout: centered horizontally
	const float marginX = w * 0.05f;
	const float usableW = w - marginX * 2.0f;
	const float gapRatio = 0.20f; // 20% of slot width as gap

	const float slotW = usableW / (float)bars;
	float barWidth = slotW * (1.0f - gapRatio);
	if (barWidth < 1.0f)
		barWidth = 1.0f;

	const float gap = slotW - barWidth;
	const float totalW = (float)bars * (barWidth + gap) - gap;
	const float startX = (w - totalW) * 0.5f;

	// vertically centered system
	const float centerY = h * 0.5f;
	const float maxHalfHeight = h * 0.4f; // max extension from center per side

	// base half-height (always visible)
	const float baseFraction = 0.20f; // 20% of maxHalfHeight as base
	const float baseHalfHeight = maxHalfHeight * baseFraction;
	const float maxExtraHalf = maxHalfHeight - baseHalfHeight;

	gs_matrix_push();

	// Per-bar target extra half-height from audio
	std::vector<float> targetExtraHalf(bars);

	for (uint32_t i = 0; i < bars; ++i) {
		float u = (float)i / (float)(bars - 1);
		size_t idx = (size_t)(u * (float)(frames - 1));

		float a = (idx < frames) ? s->wave[idx] : 0.0f;
		if (a < 1e-6f)
			a = 0.0f;

		float db = rb_db_from_amp(a);

		float extraNorm = 0.0f;

		// Only start reacting above db_react
		if (db > d->db_react) {
			float t = (db - d->db_react) / (d->db_target - d->db_react + 1e-3f);
			t = rb_clamp01(t);
			extraNorm = t;
		}

		// Optional global curve (same as other themes)
		extraNorm = audio_wave_apply_curve(s, extraNorm);

		targetExtraHalf[i] = extraNorm * maxExtraHalf;
	}

	// Spring / wobble update on extra half-height
	for (uint32_t i = 0; i < bars; ++i) {
		float value = d->value[i]; // extra half-height
		float vel = d->velocity[i];
		float target = targetExtraHalf[i];

		if (!d->initialized) {
			value = target;
			vel = 0.0f;
		}

		float acc = (target - value) * d->wobble_stiffness;
		vel = vel * d->wobble_damping + acc;
		value += vel;

		// keep in range
		if (value < 0.0f)
			value = 0.0f;
		if (value > maxExtraHalf)
			value = maxExtraHalf;

		d->value[i] = value;
		d->velocity[i] = vel;
	}

	d->initialized = true;

	// Draw bars (base + extra), centered, optional true vertical mirror
	if (color_param)
		{
			const float tcol = (bar_count <= 1) ? 0.0f : ((float)i / (float)(bar_count - 1));
			audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, tcol));
		}

	const int capSegments = 12;

	for (uint32_t i = 0; i < bars; ++i) {
		const float centerX = startX + (float)i * (barWidth + gap) + barWidth * 0.5f;
		const float extraHalf = d->value[i];

		// base + extra for each side from center
		const float halfHeightSide = baseHalfHeight + extraHalf;

		if (halfHeightSide <= 0.5f)
			continue;

		if (!d->mirror_vertical) {
			// Non-mirrored: single bar extending UP from center with both ends rounded
			const float upperBottomY = centerY;
			const float fullHeight = halfHeightSide;
			rb_draw_rounded_bar(centerX, upperBottomY, fullHeight, barWidth, capSegments);
		} else {
			// Mirrored: upper half and lower half, both sharing a flat edge at center
			rb_draw_rounded_bar_half_up(centerX, centerY, halfHeightSide, barWidth, capSegments);
			rb_draw_rounded_bar_half_down(centerX, centerY, halfHeightSide, barWidth, capSegments);
		}
	}

	gs_matrix_pop();
}

// ─────────────────────────────────────────────
// Destroy
// ─────────────────────────────────────────────

static void rounded_bars_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<rounded_bars_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

// ─────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────

static const audio_wave_theme k_rounded_bars_theme = {
	k_theme_id_rounded_bars,   k_theme_name_rounded_bars, rounded_bars_theme_add_properties,
	rounded_bars_theme_update, rounded_bars_theme_draw,   rounded_bars_theme_destroy_data,
};

void audio_wave_register_rounded_bars_theme()
{
	audio_wave_register_theme(&k_rounded_bars_theme);
}
