#include "audiowave-themes.hpp"

#include "theme-line.hpp"
#include "theme-star.hpp"
#include "theme-hex.hpp"
#include "theme-square.hpp"
#include "theme-circle.hpp"
#include "theme-line.hpp"
#include "theme-star.hpp"
#include "theme-hex.hpp"
#include "theme-square.hpp"
#include "theme-circle.hpp"
#include "theme-rounded-bars.hpp"
#include "theme-stacked-columns.hpp"

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
	audio_wave_register_rounded_bars_theme();
	audio_wave_register_stacked_columns_theme();
}
