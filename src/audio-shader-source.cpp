#include "includes/audio-shader-source.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <complex>
#include <cstring>
#include <fstream>

#include <util/platform.h>

#define BLOG(level, fmt, ...) blog(level, "[audio-shader-engine] " fmt, ##__VA_ARGS__)

static const char *kSourceId = "audio_shader_engine_source";
static const char *kSourceName = "Audio Shader Engine";

static const char *S_AUDIO_SOURCE = "audio_source";
static const char *S_WIDTH = "width";
static const char *S_HEIGHT = "height";
static const char *S_USE_OBS_CANVAS = "use_obs_canvas";
static const char *S_EFFECT_PATH = "effect_path";
static const char *S_REACT_DB = "react_db";
static const char *S_PEAK_DB = "peak_db";
static const char *S_ATTACK_MS = "attack_ms";
static const char *S_RELEASE_MS = "release_ms";
static const char *S_FFT_SIZE = "fft_size";
static const char *S_BAND_COUNT = "band_count";
static const char *S_OPTION_PREFIX = "option";
static const char *S_COLOR_PREFIX = "color";

static obs_source_info g_source_info = {};

static inline float clamp01(float v)
{
	return std::max(0.0f, std::min(1.0f, v));
}

static inline float amp_to_db(float amp)
{
	amp = std::max(amp, 0.000001f);
	return 20.0f * std::log10(amp);
}

static inline float db_to_norm(float db, float react_db, float peak_db)
{
	if (peak_db <= react_db)
		peak_db = react_db + 0.1f;
	return clamp01((db - react_db) / (peak_db - react_db));
}

static inline int clamp_pow2(int value, int min_value, int max_value)
{
	value = std::clamp(value, min_value, max_value);
	int p = 1;
	while (p < value)
		p <<= 1;
	int lower = p >> 1;
	if (lower < min_value)
		return p;
	if (p > max_value)
		return lower;
	return (value - lower) < (p - value) ? lower : p;
}

static bool is_pow2(size_t n)
{
	return n >= 2 && (n & (n - 1)) == 0;
}

static void fft_inplace(std::vector<std::complex<float>> &a)
{
	const size_t n = a.size();
	if (!is_pow2(n))
		return;

	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}

	const float pi = 3.14159265358979323846f;
	for (size_t len = 2; len <= n; len <<= 1) {
		const float ang = -2.0f * pi / (float)len;
		const std::complex<float> wlen(std::cos(ang), std::sin(ang));
		for (size_t i = 0; i < n; i += len) {
			std::complex<float> w(1.0f, 0.0f);
			for (size_t j = 0; j < len / 2; ++j) {
				const std::complex<float> u = a[i + j];
				const std::complex<float> v = a[i + j + len / 2] * w;
				a[i + j] = u + v;
				a[i + j + len / 2] = u - v;
				w *= wlen;
			}
		}
	}
}

static void color_to_vec4(uint32_t color, vec4 *out)
{
	const float r = float(color & 0xFFu) / 255.0f;
	const float g = float((color >> 8) & 0xFFu) / 255.0f;
	const float b = float((color >> 16) & 0xFFu) / 255.0f;
	vec4_set(out, r, g, b, 1.0f);
}

static std::string trim_copy(const std::string &v)
{
	size_t a = 0;
	while (a < v.size() && std::isspace((unsigned char)v[a]))
		++a;
	size_t b = v.size();
	while (b > a && std::isspace((unsigned char)v[b - 1]))
		--b;
	return v.substr(a, b - a);
}

struct effect_metadata {
	std::string name;
	std::array<std::string, 8> option_labels{};
	std::array<std::string, 4> color_labels{};
};

static std::string default_effect_path_string()
{
	char *path = obs_module_file("effects/pulse-ring.effect");
	if (!path)
		return {};
	std::string result = path;
	bfree(path);
	return result;
}

static effect_metadata load_effect_metadata(const std::string &effect_path)
{
	effect_metadata meta;
	if (effect_path.empty())
		return meta;

	const std::string ini_path = effect_path + ".ini";
	std::ifstream file(ini_path);
	if (!file.is_open())
		return meta;

	std::string section;
	std::string line;
	while (std::getline(file, line)) {
		line = trim_copy(line);
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		if (line.front() == '[' && line.back() == ']') {
			section = trim_copy(line.substr(1, line.size() - 2));
			std::transform(section.begin(), section.end(), section.begin(),
				       [](unsigned char c) { return (char)std::tolower(c); });
			continue;
		}

		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		std::string key = trim_copy(line.substr(0, eq));
		std::string value = trim_copy(line.substr(eq + 1));
		if (value.empty())
			continue;

		if (section == "effect" && key == "name") {
			meta.name = value;
		} else if (section == "options" && key.rfind("option", 0) == 0) {
			const int idx = std::atoi(key.c_str() + 6);
			if (idx >= 1 && idx <= 8 && value.rfind("Custom Option", 0) != 0)
				meta.option_labels[(size_t)idx - 1] = value;
		} else if (section == "colors" && key.rfind("color", 0) == 0) {
			const int idx = std::atoi(key.c_str() + 5);
			if (idx >= 1 && idx <= 4)
				meta.color_labels[(size_t)idx - 1] = value;
		}
	}

	return meta;
}

static void rebuild_effect_controls(obs_properties_t *props, const std::string &effect_path)
{
	if (!props)
		return;

	obs_properties_remove_by_name(props, "shader_options");

	effect_metadata meta = load_effect_metadata(effect_path);
	obs_properties_t *shader_opts = obs_properties_create();
	bool any_control = false;

	for (int i = 1; i <= 8; ++i) {
		const std::string &label = meta.option_labels[(size_t)i - 1];
		if (label.empty())
			continue;
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_OPTION_PREFIX, i);
		obs_properties_add_float_slider(shader_opts, key, label.c_str(), 0.0, 1.0, 0.001);
		any_control = true;
	}

	for (int i = 1; i <= 4; ++i) {
		const std::string &label = meta.color_labels[(size_t)i - 1];
		if (label.empty())
			continue;
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_COLOR_PREFIX, i);
		obs_properties_add_color(shader_opts, key, label.c_str());
		any_control = true;
	}

	if (!any_control) {
		obs_properties_add_text(
			shader_opts, "no_effect_controls",
			"This effect has no named controls. Add a matching .effect.ini file to expose sliders/colors.",
			OBS_TEXT_INFO);
	}

	std::string group_name = meta.name.empty() ? "Effect Controls" : (meta.name + " Controls");
	obs_properties_add_group(props, "shader_options", group_name.c_str(), OBS_GROUP_NORMAL, shader_opts);
}

static void get_obs_canvas_size(uint32_t *width, uint32_t *height)
{
	obs_video_info ovi = {};
	if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
		*width = ovi.base_width;
		*height = ovi.base_height;
		return;
	}

	*width = 1920;
	*height = 1080;
}

static uint32_t valid_dimension(int64_t value, uint32_t fallback)
{
	if (value < 16)
		return fallback;
	if (value > 8192)
		return 8192;
	return static_cast<uint32_t>(value);
}

static void set_source_dimensions(audio_shader_source *s, uint32_t width, uint32_t height)
{
	if (!s)
		return;

	width = std::clamp<uint32_t>(width, 16u, 8192u);
	height = std::clamp<uint32_t>(height, 16u, 8192u);

	if (s->width == width && s->height == height)
		return;

	s->width = width;
	s->height = height;
	s->render_logged_ok = false;

	if (s->self)
		obs_source_update_properties(s->self);

	BLOG(LOG_INFO, "Source canvas size changed to %ux%u", s->width, s->height);
}

static void release_audio_weak(audio_shader_source *s)
{
	if (!s || !s->audio_weak)
		return;
	obs_weak_source_release(s->audio_weak);
	s->audio_weak = nullptr;
}

static void audio_capture_cb(void *param, obs_source_t *, const audio_data *audio, bool muted)
{
	auto *s = static_cast<audio_shader_source *>(param);
	if (!s || !audio || !s->alive.load(std::memory_order_acquire))
		return;

	s->audio_cb_inflight.fetch_add(1, std::memory_order_acq_rel);

	if (muted || audio->frames == 0 || !audio->data[0]) {
		// When muted: push silence frames into the ring so the FFT sees silence,
		// and zero raw values so the exponential smoothers decay to 0.
		if (muted && audio && audio->frames > 0) {
			std::lock_guard<std::mutex> lock(s->audio_mutex);
			const size_t n = s->mono_ring.size();
			if (n > 0) {
				const size_t fill = std::min(static_cast<size_t>(audio->frames), n);
				for (size_t i = 0; i < fill; ++i) {
					s->mono_ring[s->mono_pos] = 0.0f;
					s->mono_pos = (s->mono_pos + 1) % n;
					if (s->mono_count < n)
						++s->mono_count;
				}
			}
			s->raw_level = 0.0f;
			s->raw_peak = 0.0f;
		}
		s->audio_cb_inflight.fetch_sub(1, std::memory_order_acq_rel);
		return;
	}

	const size_t frames = audio->frames;
	const float *left = reinterpret_cast<const float *>(audio->data[0]);
	const float *right = audio->data[1] ? reinterpret_cast<const float *>(audio->data[1]) : nullptr;

	float sum_sq = 0.0f;
	float peak = 0.0f;

	std::lock_guard<std::mutex> lock(s->audio_mutex);
	if ((int)s->mono_ring.size() != s->fft_size) {
		s->mono_ring.assign((size_t)s->fft_size, 0.0f);
		s->mono_pos = 0;
		s->mono_count = 0;
	}

	for (size_t i = 0; i < frames; ++i) {
		const float l = left[i];
		const float r = right ? right[i] : l;
		const float mono = 0.5f * (l + r);
		sum_sq += mono * mono;
		peak = std::max(peak, std::fabs(mono));

		s->mono_ring[s->mono_pos] = mono;
		s->mono_pos = (s->mono_pos + 1) % s->mono_ring.size();
		if (s->mono_count < s->mono_ring.size())
			++s->mono_count;
	}

	s->raw_level = std::sqrt(sum_sq / std::max<size_t>(1, frames));
	s->raw_peak = peak;

	s->audio_cb_inflight.fetch_sub(1, std::memory_order_acq_rel);
}

static void detach_audio(audio_shader_source *s)
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

static void attach_audio(audio_shader_source *s)
{
	if (!s || s->audio_source_name.empty())
		return;

	obs_source_t *target = obs_get_source_by_name(s->audio_source_name.c_str());
	if (!target) {
		BLOG(LOG_WARNING, "Audio source '%s' not found", s->audio_source_name.c_str());
		return;
	}

	s->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, s);
	obs_source_release(target);
}

static bool enum_audio_sources(void *data, obs_source_t *source)
{
	obs_property_t *prop = static_cast<obs_property_t *>(data);
	const char *id = obs_source_get_id(source);
	if (id && std::strcmp(id, kSourceId) == 0)
		return true;
	if (!obs_source_audio_active(source))
		return true;
	const char *name = obs_source_get_name(source);
	if (name)
		obs_property_list_add_string(prop, name, name);
	return true;
}

static void calculate_audio_state(audio_shader_source *s)
{
	float raw_level = 0.0f;
	float raw_peak = 0.0f;
	std::vector<float> ring;
	size_t pos = 0;
	size_t count = 0;

	{
		std::lock_guard<std::mutex> lock(s->audio_mutex);
		raw_level = s->raw_level;
		raw_peak = s->raw_peak;
		ring = s->mono_ring;
		pos = s->mono_pos;
		count = s->mono_count;
	}

	const float target_level = db_to_norm(amp_to_db(raw_level), s->react_db, s->peak_db);
	const float target_peak = db_to_norm(amp_to_db(raw_peak), s->react_db, s->peak_db);

	const uint64_t now = os_gettime_ns();
	float dt = 1.0f / 60.0f;
	if (s->last_ts_ns != 0 && now > s->last_ts_ns)
		dt = float(double(now - s->last_ts_ns) / 1000000000.0);
	s->last_ts_ns = now;

	auto smooth = [dt](float current, float target, float attack_ms, float release_ms) {
		const float tau = (target > current ? attack_ms : release_ms) / 1000.0f;
		if (tau <= 0.000001f)
			return target;
		const float a = 1.0f - std::exp(-dt / tau);
		return current + (target - current) * a;
	};

	s->level = clamp01(smooth(s->level, target_level, s->attack_ms, s->release_ms));
	s->peak = clamp01(smooth(s->peak, target_peak, s->attack_ms * 0.5f, s->release_ms * 1.5f));

	std::array<float, 64> raw_bands{};
	s->bass = s->mid = s->treble = 0.0f;
	if (ring.empty() || count < ring.size() / 2) {
		for (float &band : s->bands)
			band = clamp01(smooth(band, 0.0f, s->attack_ms, s->release_ms));
		return;
	}

	const size_t n = ring.size();
	std::vector<std::complex<float>> fft(n);
	for (size_t i = 0; i < n; ++i) {
		const size_t idx = (pos + i) % n;
		const float Hann = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * float(i) / float(n - 1));
		fft[i] = std::complex<float>(ring[idx] * Hann, 0.0f);
	}
	fft_inplace(fft);

	const int usable_bins = int(n / 2);
	const int bands = std::clamp(s->band_count, 1, 64);
	for (int b = 0; b < bands; ++b) {
		const float t0 = float(b) / float(bands);
		const float t1 = float(b + 1) / float(bands);
		const int bin0 = std::max(1, int(std::pow(t0, 2.0f) * usable_bins));
		const int bin1 = std::max(bin0 + 1, int(std::pow(t1, 2.0f) * usable_bins));
		float mag = 0.0f;
		int c = 0;
		for (int bin = bin0; bin < std::min(bin1, usable_bins); ++bin) {
			mag += std::abs(fft[(size_t)bin]);
			++c;
		}
		mag = c > 0 ? mag / float(c) : 0.0f;
		raw_bands[(size_t)b] = db_to_norm(amp_to_db(mag / float(n)), s->react_db, s->peak_db);
	}

	// Smooth the frequency data before it reaches the shader.
	// This prevents a single FFT bucket from producing one isolated tall bar/line.
	// Neighboring visual elements receive part of the energy, producing a continuous motion.
	for (int b = 0; b < bands; ++b) {
		float weighted = 0.0f;
		float weight_sum = 0.0f;

		for (int o = -3; o <= 3; ++o) {
			const int idx = std::clamp(b + o, 0, bands - 1);
			const float w = 4.0f - float(std::abs(o));
			weighted += raw_bands[(size_t)idx] * w;
			weight_sum += w;
		}

		const float spatial_target = weight_sum > 0.0f ? weighted / weight_sum : raw_bands[(size_t)b];
		s->bands[(size_t)b] = clamp01(smooth(s->bands[(size_t)b], spatial_target, s->attack_ms, s->release_ms));
	}

	auto avg_range = [&](int a, int b) {
		float sum = 0.0f;
		int c = 0;
		for (int i = a; i < b && i < bands; ++i) {
			sum += s->bands[(size_t)i];
			++c;
		}
		return c ? sum / float(c) : 0.0f;
	};

	s->bass = avg_range(0, bands / 4);
	s->mid = avg_range(bands / 4, bands * 2 / 3);
	s->treble = avg_range(bands * 2 / 3, bands);
}

static void destroy_effect(audio_shader_source *s)
{
	if (s && s->effect) {
		gs_effect_destroy(s->effect);
		s->effect = nullptr;
	}
}

static void load_effect_if_needed(audio_shader_source *s)
{
	if (!s || !s->reload_effect)
		return;

	s->reload_effect = false;
	destroy_effect(s);
	s->effect_error.clear();

	if (s->effect_path.empty()) {
		BLOG(LOG_WARNING, "No .effect file selected");
		return;
	}

	BLOG(LOG_INFO, "Loading effect: %s", s->effect_path.c_str());
	char *error = nullptr;
	s->effect = gs_effect_create_from_file(s->effect_path.c_str(), &error);
	if (!s->effect) {
		s->effect_error = error ? error : "Unknown shader compile error";
		BLOG(LOG_ERROR, "Could not load effect '%s': %s", s->effect_path.c_str(), s->effect_error.c_str());
	} else {
		BLOG(LOG_INFO, "Effect loaded successfully: %s", s->effect_path.c_str());
	}
	if (error)
		bfree(error);
}

static void set_float_param(gs_effect_t *effect, const char *name, float value)
{
	if (gs_eparam_t *p = gs_effect_get_param_by_name(effect, name))
		gs_effect_set_float(p, value);
}

static void set_vec2_param(gs_effect_t *effect, const char *name, float x, float y)
{
	if (gs_eparam_t *p = gs_effect_get_param_by_name(effect, name)) {
		vec2 v;
		vec2_set(&v, x, y);
		gs_effect_set_vec2(p, &v);
	}
}

static void set_color_param(gs_effect_t *effect, const char *name, uint32_t color)
{
	if (gs_eparam_t *p = gs_effect_get_param_by_name(effect, name)) {
		vec4 v;
		color_to_vec4(color, &v);
		gs_effect_set_vec4(p, &v);
	}
}

static void set_shader_params(audio_shader_source *s)
{
	gs_effect_t *e = s->effect;
	if (!e)
		return;

	set_vec2_param(e, "source_size", float(s->width), float(s->height));
	set_vec2_param(e, "resolution", float(s->width), float(s->height));
	set_float_param(e, "time", float(os_gettime_ns() / 1000000000.0));
	set_float_param(e, "audio_level", s->level);
	set_float_param(e, "audio_peak", s->peak);
	set_float_param(e, "audio_bass", s->bass);
	set_float_param(e, "audio_mid", s->mid);
	set_float_param(e, "audio_treble", s->treble);
	set_float_param(e, "band_count", float(s->band_count));

	if (gs_eparam_t *p = gs_effect_get_param_by_name(e, "audio_bands"))
		gs_effect_set_val(p, s->bands.data(), sizeof(float) * s->bands.size());

	for (size_t i = 0; i < s->options.size(); ++i) {
		char name[32];
		snprintf(name, sizeof(name), "option%zu", i + 1);
		set_float_param(e, name, s->options[i]);
	}
	for (size_t i = 0; i < s->colors.size(); ++i) {
		char name[32];
		snprintf(name, sizeof(name), "color%zu", i + 1);
		set_color_param(e, name, s->colors[i]);
	}
}

static void draw_fullscreen_quad(audio_shader_source *s)
{
	gs_render_start(true);
	gs_texcoord(0.0f, 0.0f, 0);
	gs_vertex2f(0.0f, 0.0f);
	gs_texcoord(1.0f, 0.0f, 0);
	gs_vertex2f(float(s->width), 0.0f);
	gs_texcoord(0.0f, 1.0f, 0);
	gs_vertex2f(0.0f, float(s->height));
	gs_texcoord(1.0f, 1.0f, 0);
	gs_vertex2f(float(s->width), float(s->height));
	gs_render_stop(GS_TRISTRIP);
}

static void source_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (!s)
		return;

	std::lock_guard<std::mutex> lock(s->render_mutex);
	calculate_audio_state(s);
	load_effect_if_needed(s);
	if (!s->effect) {
		if (!s->render_logged_no_effect) {
			BLOG(LOG_WARNING, "Source '%s' has no loaded effect. Selected path='%s'",
			     obs_source_get_name(s->self), s->effect_path.c_str());
			s->render_logged_no_effect = true;
		}
		return;
	}

	set_shader_params(s);

	gs_technique_t *tech = gs_effect_get_technique(s->effect, "Draw");
	if (!tech)
		tech = gs_effect_get_technique(s->effect, "Solid");
	if (!tech)
		tech = gs_effect_get_technique(s->effect, "Default");
	if (!tech) {
		if (!s->render_logged_no_technique) {
			BLOG(LOG_ERROR, "Effect '%s' has no Draw, Solid, or Default technique", s->effect_path.c_str());
			s->render_logged_no_technique = true;
		}
		return;
	}

	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	/* Do not reset the OBS matrix/projection here.
	 * OBS has already positioned/scaled the source item in the scene.
	 * Resetting to identity/ortho draws in absolute canvas space, which makes
	 * the visual appear in the middle of the preview instead of inside the
	 * selected transparent source rectangle.
	 */
	const size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		gs_technique_begin_pass(tech, i);
		draw_fullscreen_quad(s);
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);

	gs_blend_state_pop();

	if (!s->render_logged_ok || s->logged_width != s->width || s->logged_height != s->height) {
		BLOG(LOG_INFO, "Rendering source '%s' with effect '%s' at %ux%u", obs_source_get_name(s->self),
		     s->effect_path.c_str(), s->width, s->height);
		s->render_logged_ok = true;
		s->logged_width = s->width;
		s->logged_height = s->height;
	}
}

static uint32_t source_width(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	return s ? s->width : 0;
}

static uint32_t source_height(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	return s ? s->height : 0;
}

static const char *source_name(void *)
{
	return kSourceName;
}

static bool use_canvas_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool use_canvas = obs_data_get_bool(settings, S_USE_OBS_CANVAS);
	obs_property_t *width = obs_properties_get(props, S_WIDTH);
	obs_property_t *height = obs_properties_get(props, S_HEIGHT);
	if (width)
		obs_property_set_enabled(width, !use_canvas);
	if (height)
		obs_property_set_enabled(height, !use_canvas);
	return true;
}

static bool effect_path_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const char *path = obs_data_get_string(settings, S_EFFECT_PATH);
	std::string effect_path = path && *path ? path : default_effect_path_string();
	rebuild_effect_controls(props, effect_path);
	return true;
}

static bool reload_effect_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
	// OBS 28+ passes obs_properties_get_param(props) as the button callback data.
	// Older OBS passed the source context data directly.
	// Try the explicit param first; fall back to data for compatibility.
	auto *s = static_cast<audio_shader_source *>(obs_properties_get_param(props));
	if (!s)
		s = static_cast<audio_shader_source *>(data);
	if (!s)
		return false;

	// gs_effect_create_from_file and gs_effect_destroy both need the graphics
	// context active. obs_enter_graphics() lets us do the full reload immediately
	// from the UI thread instead of waiting for the next source_render frame,
	// which may be delayed or never arrive if the source is not being rendered.
	obs_enter_graphics();
	{
		std::lock_guard<std::mutex> lock(s->render_mutex);
		s->reload_effect              = true;
		s->render_logged_ok           = false;
		s->render_logged_no_effect    = false;
		s->render_logged_no_technique = false;
		s->effect_error.clear();
		load_effect_if_needed(s); // execute reload now, inside the graphics context
	}
	obs_leave_graphics();

	BLOG(LOG_INFO, "Manual shader reload triggered for '%s'", obs_source_get_name(s->self));
	return true; // true = ask OBS to refresh the properties panel
}

static obs_properties_t *source_properties(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, s, nullptr); // expose source ptr to button callbacks

	obs_property_t *audio = obs_properties_add_list(props, S_AUDIO_SOURCE, "Audio Source", OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, audio);

	obs_property_t *effect_path = obs_properties_add_path(props, S_EFFECT_PATH, "HLSL / OBS .effect file",
							      OBS_PATH_FILE, "OBS Effect (*.effect);;All files (*.*)",
							      nullptr);
	obs_property_set_modified_callback(effect_path, effect_path_modified);

	obs_properties_add_button(props, "reload_shader",
		                         "\xe2\x86\xba  Reload Shader",  // ↺
		                         reload_effect_clicked);

	obs_properties_add_text(props, "effect_metadata_help",
				"Effect controls are loaded from a sidecar file named your-shader.effect.ini. "
				"Only named controls are shown here; unnamed option uniforms stay hidden.",
				OBS_TEXT_INFO);

	obs_property_t *use_canvas = obs_properties_add_bool(props, S_USE_OBS_CANVAS, "Use OBS base canvas size");
	obs_property_set_modified_callback(use_canvas, use_canvas_modified);
	obs_properties_add_int(props, S_WIDTH, "Manual Canvas Width", 16, 8192, 1);
	obs_properties_add_int(props, S_HEIGHT, "Manual Canvas Height", 16, 8192, 1);

	obs_properties_add_float_slider(props, S_REACT_DB, "React at dB", -90.0, -1.0, 1.0);
	obs_properties_add_float_slider(props, S_PEAK_DB, "Peak at dB", -60.0, 0.0, 1.0);
	obs_properties_add_int_slider(props, S_ATTACK_MS, "Attack ms", 0, 500, 1);
	obs_properties_add_int_slider(props, S_RELEASE_MS, "Release ms", 0, 2000, 1);

	obs_property_t *fft =
		obs_properties_add_list(props, S_FFT_SIZE, "FFT Size", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fft, "512", 512);
	obs_property_list_add_int(fft, "1024", 1024);
	obs_property_list_add_int(fft, "2048", 2048);
	obs_property_list_add_int(fft, "4096", 4096);
	obs_property_list_add_int(fft, "8192", 8192);
	obs_properties_add_int_slider(props, S_BAND_COUNT, "Shader Bands", 8, 64, 1);

	std::string meta_effect_path = s && !s->effect_path.empty() ? s->effect_path : default_effect_path_string();
	rebuild_effect_controls(props, meta_effect_path);

	return props;
}

static void source_defaults(obs_data_t *settings)
{
	char *default_effect = obs_module_file("effects/pulse-ring.effect");
	if (default_effect) {
		obs_data_set_default_string(settings, S_EFFECT_PATH, default_effect);
		bfree(default_effect);
	}

	uint32_t canvas_w = 1920;
	uint32_t canvas_h = 1080;
	get_obs_canvas_size(&canvas_w, &canvas_h);
	obs_data_set_default_bool(settings, S_USE_OBS_CANVAS, false);
	obs_data_set_default_int(settings, S_WIDTH, 400);
	obs_data_set_default_int(settings, S_HEIGHT, 400);
	obs_data_set_default_double(settings, S_REACT_DB, -55.0);
	obs_data_set_default_double(settings, S_PEAK_DB, -6.0);
	obs_data_set_default_int(settings, S_ATTACK_MS, 25);
	obs_data_set_default_int(settings, S_RELEASE_MS, 180);
	obs_data_set_default_int(settings, S_FFT_SIZE, 2048);
	obs_data_set_default_int(settings, S_BAND_COUNT, 64);
	// OBS color format: ABGR where R is the least-significant byte.
	// Encoding: R | (G << 8) | (B << 16). Alpha is ignored (hardcoded to 1.0 in shader).
	// color1 = white    #FFFFFF  → R=FF G=FF B=FF → 0xFFFFFF
	// color2 = cyan     #00D2FF  → R=00 G=D2 B=FF → 0xFFD200
	// color3 = purple   #9D50BB  → R=9D G=50 B=BB → 0xBB509D
	// color4 = hot-pink #FF3CAC  → R=FF G=3C B=AC → 0xAC3CFF
	obs_data_set_default_int(settings, "color1", 0xFFFFFF);
	obs_data_set_default_int(settings, "color2", 0xFFD200);
	obs_data_set_default_int(settings, "color3", 0xBB509D);
	obs_data_set_default_int(settings, "color4", 0xAC3CFF);
}

static void source_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (!s)
		return;

	detach_audio(s);

	std::lock_guard<std::mutex> lock(s->render_mutex);

	s->audio_source_name = obs_data_get_string(settings, S_AUDIO_SOURCE);
	s->use_obs_canvas = obs_data_get_bool(settings, S_USE_OBS_CANVAS);

	uint32_t next_width = 1920;
	uint32_t next_height = 1080;
	if (s->use_obs_canvas) {
		get_obs_canvas_size(&next_width, &next_height);
	} else {
		next_width = valid_dimension(obs_data_get_int(settings, S_WIDTH), s->width ? s->width : 1920);
		next_height = valid_dimension(obs_data_get_int(settings, S_HEIGHT), s->height ? s->height : 1080);
	}
	set_source_dimensions(s, next_width, next_height);
	s->react_db = float(obs_data_get_double(settings, S_REACT_DB));
	s->peak_db = float(obs_data_get_double(settings, S_PEAK_DB));
	if (s->peak_db <= s->react_db)
		s->peak_db = s->react_db + 0.1f;
	s->attack_ms = float(obs_data_get_int(settings, S_ATTACK_MS));
	s->release_ms = float(obs_data_get_int(settings, S_RELEASE_MS));
	s->fft_size = clamp_pow2((int)obs_data_get_int(settings, S_FFT_SIZE), 512, 8192);
	s->band_count = std::clamp<int>((int)obs_data_get_int(settings, S_BAND_COUNT), 1, 64);

	const char *new_effect = obs_data_get_string(settings, S_EFFECT_PATH);
	std::string next_path = new_effect ? new_effect : "";
	if (next_path != s->effect_path) {
		s->effect_path = next_path;
		s->reload_effect = true;
		s->render_logged_ok = false;
		s->render_logged_no_effect = false;
		s->render_logged_no_technique = false;
	}

	for (int i = 1; i <= 8; ++i) {
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_OPTION_PREFIX, i);
		s->options[(size_t)i - 1] = float(obs_data_get_double(settings, key));
	}
	for (int i = 1; i <= 4; ++i) {
		char key[32];
		snprintf(key, sizeof(key), "%s%d", S_COLOR_PREFIX, i);
		s->colors[(size_t)i - 1] = uint32_t(obs_data_get_int(settings, key)) & 0xFFFFFFu;
	}

	{
		std::lock_guard<std::mutex> audio_lock(s->audio_mutex);
		if ((int)s->mono_ring.size() != s->fft_size) {
			s->mono_ring.assign((size_t)s->fft_size, 0.0f);
			s->mono_pos = 0;
			s->mono_count = 0;
		}
	}

	attach_audio(s);
}

static void *source_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new audio_shader_source{};
	s->self = source;
	obs_audio_info ai;
	if (obs_get_audio_info(&ai) && ai.samples_per_sec > 0)
		s->sample_rate = int(ai.samples_per_sec);
	source_update(s, settings);
	return s;
}

static void source_destroy(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (!s)
		return;
	s->alive.store(false, std::memory_order_release);
	detach_audio(s);
	for (int i = 0; i < 2000; ++i) {
		if (s->audio_cb_inflight.load(std::memory_order_acquire) == 0)
			break;
		os_sleep_ms(1);
	}
	destroy_effect(s);
	release_audio_weak(s);
	delete s;
}

static void source_video_tick(void *data, float)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (!s || !s->use_obs_canvas)
		return;

	uint32_t canvas_w = 1920;
	uint32_t canvas_h = 1080;
	get_obs_canvas_size(&canvas_w, &canvas_h);

	std::lock_guard<std::mutex> lock(s->render_mutex);
	set_source_dimensions(s, canvas_w, canvas_h);
}

static void source_show(void *data)
{
	auto *s = static_cast<audio_shader_source *>(data);
	if (s) {
		detach_audio(s);
		attach_audio(s);
	}
}

static void source_hide(void *data)
{
	detach_audio(static_cast<audio_shader_source *>(data));
}

extern "C" void register_audio_shader_source(void)
{
	std::memset(&g_source_info, 0, sizeof(g_source_info));
	g_source_info.id = kSourceId;
	g_source_info.type = OBS_SOURCE_TYPE_INPUT;
	g_source_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	g_source_info.icon_type = OBS_ICON_TYPE_COLOR;
	g_source_info.get_name = source_name;
	g_source_info.create = source_create;
	g_source_info.destroy = source_destroy;
	g_source_info.update = source_update;
	g_source_info.get_defaults = source_defaults;
	g_source_info.get_properties = source_properties;
	g_source_info.get_width = source_width;
	g_source_info.get_height = source_height;
	g_source_info.video_render = source_render;
	g_source_info.video_tick = source_video_tick;
	g_source_info.show = source_show;
	g_source_info.hide = source_hide;
	obs_register_source(&g_source_info);
	BLOG(LOG_INFO, "Registered source '%s'", kSourceId);
}