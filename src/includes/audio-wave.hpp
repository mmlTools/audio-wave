#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

// ─────────────────────────────────────────────
// Core setting keys
// ─────────────────────────────────────────────

#define AW_SETTING_THEME "theme"

// Forward declarations
struct audio_wave_source;
struct audio_wave_theme;

// ─────────────────────────────────────────────
// Named colors
// ─────────────────────────────────────────────

struct audio_wave_named_color {
	std::string label;
	uint32_t value = 0xFFFFFF; // 0xRRGGBB, sRGB
};

// ─────────────────────────────────────────────
// Source state
// ─────────────────────────────────────────────

struct audio_wave_source {
	// OBS source instance
	obs_source_t *self = nullptr;

	// Audio binding
	std::string audio_source_name;
	obs_weak_source_t *audio_weak = nullptr;

	// Lifetime guards for audio callback (prevents use-after-free during destroy)
	std::atomic<bool> alive{true};
	std::atomic<uint32_t> audio_cb_inflight{0};

	std::mutex audio_mutex;
	std::vector<float> samples_left;
	std::vector<float> samples_right;
	size_t num_samples = 0;
	std::vector<float> wave; // normalized 0..1 amplitudes, built from samples

	// Core visual parameters
	int width = 800;
	int height = 200;
	float inset_ratio = 0.08f;
	float gain = 2.0f;        // overall amplitude multiplier
	float curve_power = 1.0f; // curve shaping power
	int frame_density = 100;  // 10..300 (%), interpreted by themes

	// Theme selection + basic state
	std::string theme_id;       // current theme id
	std::string theme_style_id; // optional style inside a theme

	uint32_t color = 0xFFFFFF; // primary color (fallback)
	bool mirror = false;       // optional horizontal mirroring (if used by theme)

	// Generic theme palette: colors[0], colors[1], ...
	std::vector<audio_wave_named_color> colors;

	// Opaque per-instance data owned by the active theme
	void *theme_data = nullptr;
	const audio_wave_theme *theme = nullptr;
};

// Small helper: safe color access with fallback
inline uint32_t audio_wave_get_color(const audio_wave_source *s, size_t index, uint32_t fallback)
{
	if (!s)
		return fallback;
	if (index < s->colors.size())
		return s->colors[index].value;
	return fallback;
}

// ─────────────────────────────────────────────
// Theme interface
// ─────────────────────────────────────────────
//
// Each theme:
//  - defines its own properties (styles, colors, toggles, etc.)
//  - updates source state via update()
//  - optionally draws a background (separate effect, e.g. image)
//  - draws the main geometry via draw() under the Solid effect
//  - manages its own theme_data per source instance
// ─────────────────────────────────────────────

// Add theme-specific properties into a provided obs_properties_t* group.
using audio_wave_theme_add_properties_t = void (*)(obs_properties_t *props);

// Called from audio_wave_update() after core settings are read.
using audio_wave_theme_update_t = void (*)(audio_wave_source *s, obs_data_t *settings);

// Called every frame to draw the main geometry.
// audio_wave_build_wave() is already done.
// Uses the Solid effect's "color" parameter passed in.
using audio_wave_theme_draw_t = void (*)(audio_wave_source *s, gs_eparam_t *color_param);

// Called when theme changes or source is destroyed; should free theme_data.
using audio_wave_theme_destroy_data_t = void (*)(audio_wave_source *s);

// Optional: called once per frame *before* the Solid effect pass.
// Safe place to use OBS_EFFECT_DEFAULT or other effects, e.g. to draw
// an image background or additional passes under the main geometry.
using audio_wave_theme_draw_background_t = void (*)(audio_wave_source *s);

struct audio_wave_theme {
	const char *id;           // internal id, e.g. "line"
	const char *display_name; // UI name, e.g. "Line"

	// Add theme-specific properties into the provided group.
	audio_wave_theme_add_properties_t add_properties = nullptr;

	// Called from audio_wave_update() after core settings are read.
	audio_wave_theme_update_t update = nullptr;

	// Called every frame to draw the main geometry (under Solid effect).
	audio_wave_theme_draw_t draw = nullptr;

	// Called when theme changes or source is destroyed; may be nullptr.
	audio_wave_theme_destroy_data_t destroy_data = nullptr;

	// Optional background pass, called before Solid effect; may be nullptr.
	audio_wave_theme_draw_background_t draw_background = nullptr;
};

// ─────────────────────────────────────────────
// Theme registry API
// ─────────────────────────────────────────────

/**
 * Register a theme with the global registry.
 * Call this from each theme's `audio_wave_register_*_theme()` function.
 */
void audio_wave_register_theme(const audio_wave_theme *theme);

/** Number of registered themes. */
size_t audio_wave_get_theme_count();

/** Theme by index (0..count-1), or nullptr if out of range. */
const audio_wave_theme *audio_wave_get_theme_by_index(size_t index);

/** Find a theme by id; returns default theme if not found or id is null/empty. */
const audio_wave_theme *audio_wave_find_theme(const char *id);

/** Get the default theme (first registered), or nullptr if none. */
const audio_wave_theme *audio_wave_get_default_theme();

/**
 * Called once from core when needed to ensure all built-in themes
 * are registered.
 */
void audio_wave_register_builtin_themes();

inline float aw_get_float_default(obs_data_t *settings, const char *key, float def /* = 0.0f */)
{
	if (!settings || !key || !*key)
		return def;

	if (!obs_data_has_user_value(settings, key))
		return def;

	double v = obs_data_get_double(settings, key);
	return static_cast<float>(v);
}

inline int aw_get_int_default(obs_data_t *settings, const char *key, int def /* = 0 */)
{
	if (!settings || !key || !*key)
		return def;

	if (!obs_data_has_user_value(settings, key))
		return def;

	long long v = obs_data_get_int(settings, key);
	return static_cast<int>(v);
}

// ─────────────────────────────────────────────
// Core helpers visible to themes
// ─────────────────────────────────────────────

/**
 * Build normalized amplitude wave (0..1) from samples_left/right into s->wave.
 * Thread-safe with respect to s->audio_mutex.
 */
void audio_wave_build_wave(audio_wave_source *s);

/**
 * Apply curve_power (gamma-like) shaping to a normalized value [0..1].
 */
float audio_wave_apply_curve(const audio_wave_source *s, float v);

/**
 * Set a Solid effect vec4 color parameter from sRGB 0xRRGGBB.
 * Alpha is always 1.0.
 */
void audio_wave_set_solid_color(gs_eparam_t *param, uint32_t color);

// ─────────────────────────────────────────────
// OBS registration
// ─────────────────────────────────────────────

extern "C" void register_audio_wave_source(void);
