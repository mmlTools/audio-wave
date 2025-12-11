#include "audiowave-themes.hpp"

#include "theme-line.hpp"
#include "theme-star.hpp"
#include "theme-hex.hpp"
#include "theme-square.hpp"
#include "theme-circle.hpp"
#include "theme-abstract.hpp"
#include "theme-doughnut.hpp"
#include "theme-cosmic.hpp"
#include "theme-fluid.hpp"
#include "theme-fluidblob.hpp"
#include "theme-musicmagic.hpp"
#include "theme-magicsquare.hpp"
#include "theme-cartoonframe.hpp"
#include "theme-lightning.hpp"
#include "theme-rounded-bars.hpp"

static bool g_themes_registered = false;

void audio_wave_register_builtin_themes()
{
	if (g_themes_registered)
		return;

	g_themes_registered = true;

	audio_wave_register_line_theme();
	audio_wave_register_star_theme();
	audio_wave_register_hex_theme();
	audio_wave_register_square_theme();
	audio_wave_register_circle_theme();
	audio_wave_register_abstract_theme();
	audio_wave_register_doughnut_theme();
	audio_wave_register_cosmic_theme();
	audio_wave_register_fluid_theme();
	audio_wave_register_fluidblob_theme();
	audio_wave_register_musicmagic_theme();
	audio_wave_register_magicsquare_theme();
	audio_wave_register_cartoonframe_theme();
	audio_wave_register_lightning_theme();
	audio_wave_register_rounded_bars_theme();
}