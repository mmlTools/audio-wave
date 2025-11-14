// src/audio-wave.cpp
//
// Simple audio wave visualizer source for OBS Studio.
// Module entry is in plugin-main.cpp (this file only defines the source).
//
// - Lets the user select another source as "Audio Source"
// - Uses obs_source_add_audio_capture_callback() to receive audio
// - Renders a simple wave line or bars with libobs graphics API (no textures)

#include <obs-module.h>
#include <graphics/graphics.h>

#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#define BLOG(log_level, format, ...) \
	blog(log_level, "[audio-wave] " format, ##__VA_ARGS__)

// -----------------------------------------------------------------------------
// Constants / forward declarations
// -----------------------------------------------------------------------------

static const char *kSourceId   = "audio_wave_source";
static const char *kSourceName = "Audio Wave (Simple)";

static const char *SETTING_AUDIO_SOURCE     = "audio_source";
static const char *SETTING_COLOR            = "wave_color";
static const char *SETTING_COLOR2           = "wave_color2";
static const char *SETTING_GRADIENT_ENABLE  = "use_gradient";
static const char *SETTING_WIDTH            = "width";
static const char *SETTING_HEIGHT           = "height";
static const char *SETTING_MODE             = "draw_mode";   // 0 = Wave, 1 = Bars
static const char *SETTING_AMPLITUDE        = "amplitude";   // percent (10..400)
static const char *SETTING_MIRROR           = "mirror_wave"; // bool

struct audio_wave_source;

// Single global source info; we fill it in register_audio_wave_source()
static struct obs_source_info audio_wave_source_info;

// Forward declarations of callbacks
static void *audio_wave_create(obs_data_t *settings, obs_source_t *source);
static void  audio_wave_destroy(void *data);
static void  audio_wave_update(void *data, obs_data_t *settings);
static void  audio_wave_get_defaults(obs_data_t *settings);
static obs_properties_t *audio_wave_get_properties(void *data);
static void  audio_wave_show(void *data);
static void  audio_wave_hide(void *data);
static uint32_t audio_wave_get_width(void *data);
static uint32_t audio_wave_get_height(void *data);
static void  audio_wave_video_render(void *data, gs_effect_t *effect);

// gradient visibility callback (OBS 32 signature)
static bool gradient_modified(obs_properties_t *props,
                              obs_property_t *p,
                              obs_data_t *settings);

// -----------------------------------------------------------------------------
// Per-instance data
// -----------------------------------------------------------------------------

struct audio_wave_source {
	obs_source_t *self = nullptr;

	// Settings
	std::string audio_source_name;
	uint32_t color  = 0xFFFFFF; // OBS color props are 0x00BBGGRR (BGR)
	uint32_t color2 = 0x00FF00; // second color for gradient
	int width       = 800;
	int height      = 200;
	int mode        = 0;        // 0 = wave, 1 = bars
	bool use_gradient = false;
	float gain      = 2.0f;     // amplitude multiplier
	bool mirror     = false;    // mirror wave below mid-line

	// Weak ref to selected audio source
	obs_weak_source_t *audio_weak = nullptr;

	// Audio data
	std::mutex audio_mutex;
	std::vector<float> samples_left;
	std::vector<float> samples_right;
	size_t num_samples = 0;

	// Render buffer: mono wave [0..1]
	std::vector<float> wave;
};

// -----------------------------------------------------------------------------
// Helpers: weak ref management
// -----------------------------------------------------------------------------

static void release_audio_weak(audio_wave_source *s)
{
	if (!s || !s->audio_weak)
		return;

	obs_weak_source_release(s->audio_weak);
	s->audio_weak = nullptr;
}

static void attach_to_audio_source(audio_wave_source *s);

// Enumerate sources to fill property list
static bool enum_audio_sources(void *data, obs_source_t *source)
{
	obs_property_t *prop = (obs_property_t *)data;

	// Skip our own source type
	const char *id = obs_source_get_id(source);
	if (id && std::strcmp(id, kSourceId) == 0)
		return true;

	// Only list sources that actually have audio
	if (!obs_source_audio_active(source))
		return true;

	const char *name = obs_source_get_name(source);
	if (!name)
		return true;

	obs_property_list_add_string(prop, name, name);
	return true;
}

// -----------------------------------------------------------------------------
// Audio capture callback
// -----------------------------------------------------------------------------

static void audio_capture_cb(void *param, obs_source_t *, const struct audio_data *audio,
                             bool muted)
{
	auto *s = static_cast<audio_wave_source *>(param);
	if (!s || !audio)
		return;

	static uint64_t dbg_calls = 0;

	if (muted || audio->frames == 0 || !audio->data[0]) {
		if (dbg_calls < 10) {
			BLOG(LOG_INFO, "audio_capture_cb: muted=%d frames=%d (no data)",
			     (int)muted, (int)audio->frames);
			dbg_calls++;
		}
		return;
	}

	const size_t frames = audio->frames;
	const uint8_t *data0 = audio->data[0];
	const uint8_t *data1 = audio->data[1];

	const float *left  = reinterpret_cast<const float *>(data0);
	const float *right = data1 ? reinterpret_cast<const float *>(data1) : nullptr;

	std::lock_guard<std::mutex> lock(s->audio_mutex);

	s->samples_left.resize(frames);
	s->samples_right.resize(frames);

	for (size_t i = 0; i < frames; ++i) {
		const float l = left[i];
		const float r = right ? right[i] : l;
		s->samples_left[i]  = l;
		s->samples_right[i] = r;
	}

	s->num_samples = frames;

	if (dbg_calls < 20) {
		BLOG(LOG_INFO, "audio_capture_cb: frames=%d firstL=%f firstR=%f",
		     (int)frames,
		     frames > 0 ? left[0] : 0.0f,
		     (frames > 0 && right) ? right[0] : left[0]);
		dbg_calls++;
	}
}

// (Re)attach callback to selected audio source
static void attach_to_audio_source(audio_wave_source *s)
{
	if (!s)
		return;

	release_audio_weak(s);

	if (s->audio_source_name.empty())
		return;

	obs_source_t *target = obs_get_source_by_name(s->audio_source_name.c_str());
	if (!target) {
		BLOG(LOG_WARNING, "Audio source '%s' not found",
		     s->audio_source_name.c_str());
		return;
	}

	s->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, s);

	obs_source_release(target);

	BLOG(LOG_INFO, "Attached to audio source '%s'",
	     s->audio_source_name.c_str());
}

// Detach callback
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

// -----------------------------------------------------------------------------
// Settings / properties
// -----------------------------------------------------------------------------

static bool gradient_modified(obs_properties_t *props,
                              obs_property_t *p,
                              obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	bool enabled = obs_data_get_bool(settings, SETTING_GRADIENT_ENABLE);
	obs_property_t *c2 = obs_properties_get(props, SETTING_COLOR2);
	if (c2)
		obs_property_set_visible(c2, enabled);

	return true;
}

static obs_properties_t *audio_wave_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	// Audio source selector
	obs_property_t *p_list = obs_properties_add_list(
		props, SETTING_AUDIO_SOURCE, "Audio Source",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, p_list);

	// Display mode: Wave or Bars
	obs_property_t *mode =
		obs_properties_add_list(props, SETTING_MODE, "Display Mode",
		                        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, "Wave", 0);
	obs_property_list_add_int(mode, "Bars", 1);

	// Color & gradient controls
	obs_property_t *grad =
		obs_properties_add_bool(props, SETTING_GRADIENT_ENABLE, "Use Gradient");
	obs_property_set_modified_callback(grad, gradient_modified);

	obs_properties_add_color(props, SETTING_COLOR,  "Wave Color");
	obs_property_t *c2 =
		obs_properties_add_color(props, SETTING_COLOR2, "Wave Color 2");

	// Initially hide color2 until gradient is enabled
	obs_property_set_visible(c2, false);

	// Dimensions
	obs_properties_add_int(props, SETTING_WIDTH,  "Width",  64, 4096, 1);
	obs_properties_add_int(props, SETTING_HEIGHT, "Height", 32, 2048, 1);

	// Amplitude (percentage)
	obs_properties_add_int_slider(props, SETTING_AMPLITUDE,
	                              "Amplitude (%)", 10, 400, 10);

	// Mirror setting
	obs_properties_add_bool(props, SETTING_MIRROR,
	                        "Mirror wave horizontally");

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
	obs_data_set_default_int(settings, SETTING_MODE, 0);        // Wave
	obs_data_set_default_int(settings, SETTING_AMPLITUDE, 200); // 200% (gain=2.0)
	obs_data_set_default_bool(settings, SETTING_MIRROR, false);
}

static void audio_wave_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	// Detach from old audio source
	detach_from_audio_source(s);

	s->audio_source_name = obs_data_get_string(settings, SETTING_AUDIO_SOURCE);

	s->width   = (int)obs_data_get_int(settings, SETTING_WIDTH);
	s->height  = (int)obs_data_get_int(settings, SETTING_HEIGHT);
	s->color   = (uint32_t)obs_data_get_int(settings, SETTING_COLOR);
	s->color2  = (uint32_t)obs_data_get_int(settings, SETTING_COLOR2);
	s->mode    = (int)obs_data_get_int(settings, SETTING_MODE);
	s->use_gradient = obs_data_get_bool(settings, SETTING_GRADIENT_ENABLE);
	s->mirror  = obs_data_get_bool(settings, SETTING_MIRROR);

	int amp_pct = (int)obs_data_get_int(settings, SETTING_AMPLITUDE);
	if (amp_pct < 10)  amp_pct = 10;
	if (amp_pct > 400) amp_pct = 400;
	s->gain = (float)amp_pct / 100.0f;

	if (s->width < 1)
		s->width = 1;
	if (s->height < 1)
		s->height = 1;

	// Try to attach immediately with new settings
	attach_to_audio_source(s);
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

static void *audio_wave_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new audio_wave_source{};
	s->self = source;

	// Use whatever settings OBS gives us (defaults already applied by OBS)
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

// When the scene/source becomes visible, try to (re)attach.
static void audio_wave_show(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
	attach_to_audio_source(s);
}

// When hidden, detach.
static void audio_wave_hide(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
}

// -----------------------------------------------------------------------------
// Video sizing
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// Wave building & rendering
// -----------------------------------------------------------------------------

static void build_wave(audio_wave_source *s)
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
		s->wave[i] = m; // 0..1
	}
}

// OBS color properties are stored as 0x00BBGGRR (BGR).
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

// Simple lerp between two OBS color values (0x00BBGGRR)
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

static void draw_wave(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	build_wave(s);

	const size_t frames = s->wave.size();
	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;

	gs_matrix_push();
	// Keep OBS's transform for this source

	// ---------------------------------------------------------
	// No audio yet – draw flat line (optionally gradient)
	// ---------------------------------------------------------
	if (frames < 2) {
		if (!s->use_gradient) {
			if (color_param)
				set_solid_color(color_param, s->color);

			gs_render_start(true);
			for (uint32_t x = 0; x < (uint32_t)w; ++x)
				gs_vertex2f((float)x, mid_y);
			gs_render_stop(GS_LINESTRIP);

			if (s->mirror) {
				// Line is on mid_y already; mirrored line would overlap,
				// so nothing extra needed visually.
			}
		} else {
			// Gradient across width
			for (uint32_t x = 0; x + 1 < (uint32_t)w; x += 2) {
				float t = w > 1.0f ? (float)x / (w - 1.0f) : 0.0f;
				if (color_param)
					set_solid_color(color_param, lerp_color(s->color, s->color2, t));

				gs_render_start(true);
				gs_vertex2f((float)x, mid_y);
				gs_vertex2f((float)(x + 1), mid_y);
				gs_render_stop(GS_LINES);

				if (s->mirror) {
					// Same story: flat line, mirrored overlaps.
				}
			}
		}

		gs_matrix_pop();
		return;
	}

	// ---------------------------------------------------------
	// Solid (non-gradient) modes
	// ---------------------------------------------------------
	if (!s->use_gradient) {
		if (color_param)
			set_solid_color(color_param, s->color);

		if (s->mode == 0) {
			// Wave - continuous line
			// Top line
			gs_render_start(true);
			for (uint32_t x = 0; x < (uint32_t)w; ++x) {
				const size_t idx =
					(size_t)((double)x * (double)(frames - 1) /
					         (double)std::max(1.0f, w - 1.0f));
				const float v = s->wave[idx]; // 0..1
				const float y = mid_y - v * (mid_y - 2.0f);
				gs_vertex2f((float)x, y);
			}
			gs_render_stop(GS_LINESTRIP);

			// Mirrored line (bottom)
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
			// Bars
			const uint32_t step = 3;
			gs_render_start(true);
			for (uint32_t x = 0; x < (uint32_t)w; x += step) {
				const size_t idx =
					(size_t)((double)x * (double)(frames - 1) /
					         (double)std::max(1.0f, w - 1.0f));
				const float v = s->wave[idx];
				const float y = mid_y - v * (mid_y - 4.0f);

				// Top bar
				gs_vertex2f((float)x, mid_y);
				gs_vertex2f((float)x, y);

				// Mirrored bar
				if (s->mirror) {
					const float y_m = mid_y + (mid_y - y);
					gs_vertex2f((float)x, mid_y);
					gs_vertex2f((float)x, y_m);
				}
			}
			gs_render_stop(GS_LINES);
		}
	} else {
		// ---------------------------------------------------------
		// Gradient modes
		// ---------------------------------------------------------
		if (s->mode == 0) {
			// Wave gradient: segment-by-segment
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
					set_solid_color(color_param, lerp_color(s->color, s->color2, t));

				// Top segment
				gs_render_start(true);
				gs_vertex2f(prev_x, prev_y);
				gs_vertex2f((float)x, y_cur);
				gs_render_stop(GS_LINES);

				// Mirrored segment
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
			// Bars gradient: each bar has its own color
			const uint32_t step = 3;
			for (uint32_t x = 0; x < (uint32_t)w; x += step) {
				const size_t idx =
					(size_t)((double)x * (double)(frames - 1) /
					         (double)std::max(1.0f, w - 1.0f));
				const float v = s->wave[idx];
				const float y = mid_y - v * (mid_y - 4.0f);

				float t = w > 1.0f ? (float)x / (w - 1.0f) : 0.0f;
				if (color_param)
					set_solid_color(color_param, lerp_color(s->color, s->color2, t));

				gs_render_start(true);
				// Top bar
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

static void audio_wave_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	// Use SOLID base effect (no textures)
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
		draw_wave(s, color_param);
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);
}

// -----------------------------------------------------------------------------
// Name helper
// -----------------------------------------------------------------------------

static const char *audio_wave_get_name(void *)
{
	return kSourceName;
}

// -----------------------------------------------------------------------------
// Registration function (called from plugin-main.cpp)
// -----------------------------------------------------------------------------

extern "C" void register_audio_wave_source(void)
{
	std::memset(&audio_wave_source_info, 0, sizeof(audio_wave_source_info));

	audio_wave_source_info.id           = kSourceId;
	audio_wave_source_info.type         = OBS_SOURCE_TYPE_INPUT;
	audio_wave_source_info.output_flags = OBS_SOURCE_VIDEO;

	audio_wave_source_info.get_name     = audio_wave_get_name;
	audio_wave_source_info.create       = audio_wave_create;
	audio_wave_source_info.destroy      = audio_wave_destroy;
	audio_wave_source_info.update       = audio_wave_update;
	audio_wave_source_info.get_defaults = audio_wave_get_defaults;
	audio_wave_source_info.get_properties = audio_wave_get_properties;
	audio_wave_source_info.show         = audio_wave_show;
	audio_wave_source_info.hide         = audio_wave_hide;
	audio_wave_source_info.get_width    = audio_wave_get_width;
	audio_wave_source_info.get_height   = audio_wave_get_height;
	audio_wave_source_info.video_render = audio_wave_video_render;

	obs_register_source(&audio_wave_source_info);

	BLOG(LOG_INFO, "Registered Audio Wave source as '%s'", kSourceId);
}
