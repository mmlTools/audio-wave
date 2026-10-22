#include "includes/audio-pulse-filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#define BLOG(level, fmt, ...) blog(level, "[audio-pulse-filter] " fmt, ##__VA_ARGS__)

static const char *kFilterId = "audio_pulse_filter";
static const char *kFilterName = "Audio Pulse";

static const char *S_AUDIO_SOURCE = "audio_source";
static const char *S_BAND = "pulse_band";
static const char *S_MODE = "pulse_mode";
static const char *S_REACT_DB = "react_db";
static const char *S_PEAK_DB = "peak_db";
static const char *S_ATTACK_MS = "attack_ms";
static const char *S_RELEASE_MS = "release_ms";
static const char *S_BEAT_SENSITIVITY = "beat_sensitivity";
static const char *S_BEAT_COOLDOWN_MS = "beat_cooldown_ms";
static const char *S_SCALE_POWER = "scale_power";
static const char *S_SHRINK = "shrink";
static const char *S_ANCHOR = "anchor";
static const char *S_BRIGHTNESS_POWER = "brightness_power";
static const char *S_OPACITY_FLOOR = "opacity_floor";

static obs_source_info g_filter_info = {};

static inline float clamp01(float v)
{
	return std::max(0.0f, std::min(1.0f, v));
}

static inline float rms_to_norm(double sum_sq, size_t frames, float react_db, float peak_db)
{
	if (frames == 0 || peak_db <= react_db)
		return 0.0f;
	const float rms = float(std::sqrt(sum_sq / double(frames)));
	const float db = 20.0f * std::log10(std::max(rms, 0.000001f));
	return clamp01((db - react_db) / (peak_db - react_db));
}

static inline float one_pole_alpha(float cutoff_hz, int sample_rate)
{
	return 1.0f - std::exp(-2.0f * 3.14159265358979323846f * cutoff_hz / float(std::max(sample_rate, 1)));
}

static void audio_capture_cb(void *param, obs_source_t *, const audio_data *audio, bool muted)
{
	auto *f = static_cast<audio_pulse_filter *>(param);
	if (!f || !audio || audio->frames == 0)
		return;

	std::lock_guard<std::mutex> lock(f->audio_mutex);
	f->acc_frames += audio->frames;

	// Muted audio counts as silence so the pulse decays instead of freezing.
	if (muted || !audio->data[0])
		return;

	const float *left = reinterpret_cast<const float *>(audio->data[0]);
	const float *right = audio->data[1] ? reinterpret_cast<const float *>(audio->data[1]) : nullptr;

	for (size_t i = 0; i < audio->frames; ++i) {
		const float mono = right ? 0.5f * (left[i] + right[i]) : left[i];

		f->lp_bass += f->alpha_bass * (mono - f->lp_bass);
		f->lp_mid_low += f->alpha_mid_low * (mono - f->lp_mid_low);
		f->lp_mid_high += f->alpha_mid_high * (mono - f->lp_mid_high);

		const float mid = f->lp_mid_high - f->lp_mid_low;
		const float treble = mono - f->lp_mid_high;

		f->acc_level_sq += double(mono) * mono;
		f->acc_bass_sq += double(f->lp_bass) * f->lp_bass;
		f->acc_mid_sq += double(mid) * mid;
		f->acc_treble_sq += double(treble) * treble;
		f->acc_peak = std::max(f->acc_peak, std::fabs(mono));
	}
}

static void detach_audio(audio_pulse_filter *f)
{
	if (!f || !f->audio_weak)
		return;

	obs_source_t *target = obs_weak_source_get_source(f->audio_weak);
	if (target) {
		obs_source_remove_audio_capture_callback(target, audio_capture_cb, f);
		obs_source_release(target);
	}
	obs_weak_source_release(f->audio_weak);
	f->audio_weak = nullptr;
}

static void attach_audio(audio_pulse_filter *f)
{
	if (!f || f->audio_weak)
		return;

	obs_source_t *target = nullptr;
	if (f->audio_source_name.empty()) {
		obs_source_t *parent = obs_filter_get_parent(f->self);
		target = parent ? obs_source_get_ref(parent) : nullptr;
	} else {
		target = obs_get_source_by_name(f->audio_source_name.c_str());
	}
	if (!target)
		return;

	if ((obs_source_get_output_flags(target) & OBS_SOURCE_AUDIO) == 0) {
		obs_source_release(target);
		return;
	}

	f->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, f);
	BLOG(LOG_INFO, "Filter '%s' listening to '%s'", obs_source_get_name(f->self), obs_source_get_name(target));
	obs_source_release(target);
}

static bool enum_audio_sources(void *data, obs_source_t *source)
{
	obs_property_t *prop = static_cast<obs_property_t *>(data);
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0)
		return true;
	const char *name = obs_source_get_name(source);
	if (name && *name)
		obs_property_list_add_string(prop, name, name);
	return true;
}

static void update_pulse(audio_pulse_filter *f, float seconds)
{
	double level_sq = 0.0, bass_sq = 0.0, mid_sq = 0.0, treble_sq = 0.0;
	float peak = 0.0f;
	size_t frames = 0;
	{
		std::lock_guard<std::mutex> lock(f->audio_mutex);
		level_sq = f->acc_level_sq;
		bass_sq = f->acc_bass_sq;
		mid_sq = f->acc_mid_sq;
		treble_sq = f->acc_treble_sq;
		peak = f->acc_peak;
		frames = f->acc_frames;
		f->acc_level_sq = f->acc_bass_sq = f->acc_mid_sq = f->acc_treble_sq = 0.0;
		f->acc_peak = 0.0f;
		f->acc_frames = 0;
	}

	// Audio packets arrive less often than video frames; hold the last target between packets.
	if (frames > 0) {
		switch (f->band) {
		case pulse_band::peak: {
			const float db = 20.0f * std::log10(std::max(peak, 0.000001f));
			f->target = f->peak_db > f->react_db ? clamp01((db - f->react_db) / (f->peak_db - f->react_db))
							     : 0.0f;
			break;
		}
		case pulse_band::bass:
			f->target = rms_to_norm(bass_sq, frames, f->react_db, f->peak_db);
			break;
		case pulse_band::mid:
			f->target = rms_to_norm(mid_sq, frames, f->react_db, f->peak_db);
			break;
		case pulse_band::treble:
			f->target = rms_to_norm(treble_sq, frames, f->react_db, f->peak_db);
			break;
		default:
			f->target = rms_to_norm(level_sq, frames, f->react_db, f->peak_db);
			break;
		}
	}

	auto smooth = [seconds](float current, float target, float attack_ms, float release_ms) {
		const float tau = (target > current ? attack_ms : release_ms) / 1000.0f;
		if (tau <= 0.000001f)
			return target;
		return current + (target - current) * (1.0f - std::exp(-seconds / tau));
	};

	if (f->mode == pulse_mode::beat) {
		// An onset is a jump well above the recent average; each onset kicks the envelope,
		// which then falls back with the release time.
		f->beat_cooldown_s = std::max(0.0f, f->beat_cooldown_s - seconds);
		const float threshold = 0.5f - 0.45f * f->beat_sensitivity;
		const bool onset = f->target > 0.05f && f->target - f->beat_average > threshold;
		if (onset && f->beat_cooldown_s <= 0.0f) {
			f->envelope = std::max(f->envelope, clamp01(0.4f + 0.6f * f->target));
			f->beat_cooldown_s = f->beat_cooldown_ms / 1000.0f;
		} else {
			f->envelope = smooth(f->envelope, 0.0f, f->attack_ms, f->release_ms);
		}
		f->beat_average = smooth(f->beat_average, f->target, 250.0f, 500.0f);
	} else {
		f->envelope = smooth(f->envelope, f->target, f->attack_ms, f->release_ms);
	}
	f->envelope = clamp01(f->envelope);
}

static const char *filter_name(void *)
{
	return kFilterName;
}

static bool mode_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool beat = obs_data_get_int(settings, S_MODE) == int(pulse_mode::beat);
	obs_property_set_visible(obs_properties_get(props, S_BEAT_SENSITIVITY), beat);
	obs_property_set_visible(obs_properties_get(props, S_BEAT_COOLDOWN_MS), beat);
	return true;
}

static obs_properties_t *filter_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *audio = obs_properties_add_list(props, S_AUDIO_SOURCE, "Audio Source", OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(audio, "(This source's own audio)", "");
	obs_enum_sources(enum_audio_sources, audio);

	obs_property_t *band =
		obs_properties_add_list(props, S_BAND, "React To", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(band, "Overall Level", int(pulse_band::level));
	obs_property_list_add_int(band, "Peak", int(pulse_band::peak));
	obs_property_list_add_int(band, "Bass", int(pulse_band::bass));
	obs_property_list_add_int(band, "Mids", int(pulse_band::mid));
	obs_property_list_add_int(band, "Treble", int(pulse_band::treble));

	obs_property_t *mode =
		obs_properties_add_list(props, S_MODE, "Pulse Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, "Follow Volume", int(pulse_mode::follow));
	obs_property_list_add_int(mode, "Beat Kick", int(pulse_mode::beat));
	obs_property_set_modified_callback(mode, mode_modified);

	obs_property_t *p = obs_properties_add_int_slider(props, S_BEAT_SENSITIVITY, "Beat Sensitivity", 0, 100, 1);
	obs_property_int_set_suffix(p, " %");
	p = obs_properties_add_int_slider(props, S_BEAT_COOLDOWN_MS, "Minimum Time Between Beats", 0, 1000, 1);
	obs_property_int_set_suffix(p, " ms");

	obs_properties_add_float_slider(props, S_REACT_DB, "React at dB", -90.0, -1.0, 1.0);
	obs_properties_add_float_slider(props, S_PEAK_DB, "Peak at dB", -60.0, 0.0, 1.0);
	p = obs_properties_add_int_slider(props, S_ATTACK_MS, "Attack", 0, 500, 1);
	obs_property_int_set_suffix(p, " ms");
	p = obs_properties_add_int_slider(props, S_RELEASE_MS, "Release", 0, 2000, 1);
	obs_property_int_set_suffix(p, " ms");

	p = obs_properties_add_int_slider(props, S_SCALE_POWER, "Pulse Power (Size)", 0, 100, 1);
	obs_property_int_set_suffix(p, " %");
	obs_properties_add_bool(props, S_SHRINK, "Shrink instead of grow");

	obs_property_t *anchor =
		obs_properties_add_list(props, S_ANCHOR, "Pulse Anchor", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(anchor, "Center", int(pulse_anchor::center));
	obs_property_list_add_int(anchor, "Bottom", int(pulse_anchor::bottom));
	obs_property_list_add_int(anchor, "Top", int(pulse_anchor::top));

	p = obs_properties_add_int_slider(props, S_BRIGHTNESS_POWER, "Brightness Flash", 0, 200, 1);
	obs_property_int_set_suffix(p, " %");
	p = obs_properties_add_int_slider(props, S_OPACITY_FLOOR, "Opacity When Silent", 0, 100, 1);
	obs_property_int_set_suffix(p, " %");

	return props;
}

static void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, S_AUDIO_SOURCE, "");
	obs_data_set_default_int(settings, S_BAND, int(pulse_band::level));
	obs_data_set_default_int(settings, S_MODE, int(pulse_mode::follow));
	obs_data_set_default_int(settings, S_BEAT_SENSITIVITY, 35);
	obs_data_set_default_int(settings, S_BEAT_COOLDOWN_MS, 120);
	obs_data_set_default_double(settings, S_REACT_DB, -45.0);
	obs_data_set_default_double(settings, S_PEAK_DB, -6.0);
	obs_data_set_default_int(settings, S_ATTACK_MS, 15);
	obs_data_set_default_int(settings, S_RELEASE_MS, 220);
	obs_data_set_default_int(settings, S_SCALE_POWER, 15);
	obs_data_set_default_bool(settings, S_SHRINK, false);
	obs_data_set_default_int(settings, S_ANCHOR, int(pulse_anchor::center));
	obs_data_set_default_int(settings, S_BRIGHTNESS_POWER, 0);
	obs_data_set_default_int(settings, S_OPACITY_FLOOR, 100);
}

static void filter_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<audio_pulse_filter *>(data);
	if (!f)
		return;

	std::lock_guard<std::mutex> lock(f->state_mutex);

	const char *audio_name = obs_data_get_string(settings, S_AUDIO_SOURCE);
	std::string next_audio = audio_name ? audio_name : "";
	if (next_audio != f->audio_source_name || !f->audio_weak) {
		detach_audio(f);
		f->audio_source_name = next_audio;
		attach_audio(f);
	}

	f->band = pulse_band(std::clamp<int>((int)obs_data_get_int(settings, S_BAND), 0, 4));
	f->mode = pulse_mode(std::clamp<int>((int)obs_data_get_int(settings, S_MODE), 0, 1));
	f->anchor = pulse_anchor(std::clamp<int>((int)obs_data_get_int(settings, S_ANCHOR), 0, 2));
	f->react_db = float(obs_data_get_double(settings, S_REACT_DB));
	f->peak_db = float(obs_data_get_double(settings, S_PEAK_DB));
	f->attack_ms = float(obs_data_get_int(settings, S_ATTACK_MS));
	f->release_ms = float(obs_data_get_int(settings, S_RELEASE_MS));
	f->beat_sensitivity = clamp01(float(obs_data_get_int(settings, S_BEAT_SENSITIVITY)) / 100.0f);
	f->beat_cooldown_ms = float(obs_data_get_int(settings, S_BEAT_COOLDOWN_MS));
	f->scale_power = clamp01(float(obs_data_get_int(settings, S_SCALE_POWER)) / 100.0f);
	f->shrink = obs_data_get_bool(settings, S_SHRINK);
	f->brightness_power = std::clamp(float(obs_data_get_int(settings, S_BRIGHTNESS_POWER)) / 100.0f, 0.0f, 2.0f);
	f->opacity_floor = clamp01(float(obs_data_get_int(settings, S_OPACITY_FLOOR)) / 100.0f);
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new (std::nothrow) audio_pulse_filter{};
	if (!f)
		return nullptr;

	f->self = source;

	int sample_rate = 48000;
	obs_audio_info ai;
	if (obs_get_audio_info(&ai) && ai.samples_per_sec > 0)
		sample_rate = int(ai.samples_per_sec);
	f->alpha_bass = one_pole_alpha(150.0f, sample_rate);
	f->alpha_mid_low = one_pole_alpha(250.0f, sample_rate);
	f->alpha_mid_high = one_pole_alpha(4000.0f, sample_rate);

	char *effect_path = obs_module_file("filters/audio-pulse.effect");
	obs_enter_graphics();
	char *error = nullptr;
	f->effect = effect_path ? gs_effect_create_from_file(effect_path, &error) : nullptr;
	obs_leave_graphics();
	if (!f->effect)
		BLOG(LOG_ERROR, "Could not load pulse effect '%s': %s", effect_path ? effect_path : "(missing)",
		     error ? error : "unknown error");
	bfree(error);
	bfree(effect_path);

	filter_update(f, settings);
	return f;
}

static void filter_destroy(void *data)
{
	auto *f = static_cast<audio_pulse_filter *>(data);
	if (!f)
		return;

	// Removing the capture callback waits for any in-flight callback to finish.
	detach_audio(f);

	obs_enter_graphics();
	if (f->effect)
		gs_effect_destroy(f->effect);
	obs_leave_graphics();

	delete f;
}

static void filter_tick(void *data, float seconds)
{
	auto *f = static_cast<audio_pulse_filter *>(data);
	if (!f)
		return;

	std::lock_guard<std::mutex> lock(f->state_mutex);

	if (f->audio_weak && obs_weak_source_expired(f->audio_weak)) {
		obs_weak_source_release(f->audio_weak);
		f->audio_weak = nullptr;
	}

	// The parent isn't known at create time and named sources may load after us, so keep retrying.
	if (!f->audio_weak) {
		f->retry_attach_s -= seconds;
		if (f->retry_attach_s <= 0.0f) {
			f->retry_attach_s = 1.0f;
			attach_audio(f);
		}
	}

	update_pulse(f, seconds);
}

static void filter_render(void *data, gs_effect_t *)
{
	auto *f = static_cast<audio_pulse_filter *>(data);
	if (!f)
		return;

	obs_source_t *target = obs_filter_get_target(f->self);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (!f->effect || width == 0 || height == 0) {
		obs_source_skip_video_filter(f->self);
		return;
	}

	float env, scale, brightness, opacity;
	pulse_anchor anchor;
	{
		std::lock_guard<std::mutex> lock(f->state_mutex);
		env = f->envelope;
		const float amount = env * f->scale_power;
		scale = f->shrink ? 1.0f - std::min(amount, 0.95f) : 1.0f + amount;
		brightness = 1.0f + env * f->brightness_power;
		opacity = f->opacity_floor + (1.0f - f->opacity_floor) * env;
		anchor = f->anchor;
	}

	if (std::fabs(scale - 1.0f) < 0.0001f && brightness < 1.0001f && opacity > 0.9999f) {
		obs_source_skip_video_filter(f->self);
		return;
	}

	if (!obs_source_process_filter_begin(f->self, GS_RGBA, OBS_NO_DIRECT_RENDERING))
		return;

	gs_effect_set_float(gs_effect_get_param_by_name(f->effect, "brightness"), brightness);
	gs_effect_set_float(gs_effect_get_param_by_name(f->effect, "opacity"), opacity);

	const float ax = float(width) * 0.5f;
	const float ay = anchor == pulse_anchor::bottom ? float(height)
			 : anchor == pulse_anchor::top  ? 0.0f
							: float(height) * 0.5f;

	gs_matrix_push();
	gs_matrix_translate3f(ax, ay, 0.0f);
	gs_matrix_scale3f(scale, scale, 1.0f);
	gs_matrix_translate3f(-ax, -ay, 0.0f);
	obs_source_process_filter_end(f->self, f->effect, width, height);
	gs_matrix_pop();
}

extern "C" void register_audio_pulse_filter(void)
{
	std::memset(&g_filter_info, 0, sizeof(g_filter_info));
	g_filter_info.id = kFilterId;
	g_filter_info.type = OBS_SOURCE_TYPE_FILTER;
	g_filter_info.output_flags = OBS_SOURCE_VIDEO;
	g_filter_info.get_name = filter_name;
	g_filter_info.create = filter_create;
	g_filter_info.destroy = filter_destroy;
	g_filter_info.update = filter_update;
	g_filter_info.get_defaults = filter_defaults;
	g_filter_info.get_properties = filter_properties;
	g_filter_info.video_tick = filter_tick;
	g_filter_info.video_render = filter_render;
	obs_register_source(&g_filter_info);
	BLOG(LOG_INFO, "Registered filter '%s'", kFilterId);
}
