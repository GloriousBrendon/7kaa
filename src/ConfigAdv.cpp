/*
 * Seven Kingdoms: Ancient Adversaries
 *
 * Copyright 2019 Jesse Allen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

//Filename    : ConfigAdv.cpp
//Description : Advanced Config

#include <ConfigAdv.h>
#include <FilePath.h>
#include <ONATIONB.h>
#include <OFILETXT.h>
#include <OMISC.h>
#include <OMOUSE.h>
#include <OSYS.h>
#include <posix_string_compat.h>
#include <version.h>
#include <errno.h>
#include <string>
#include "gettext.h"

#define CHECK_BOUND(n,x,y) n<x || n>y

union KeyEventMap
{
	int index;
	KeyEventType type;
};
static const char *keyevent_map[] = {
	"KEYEVENT_UNSET",

	"KEYEVENT_FIRM_BUILD",
	"KEYEVENT_FIRM_PATROL",

	"KEYEVENT_TOWN_RECRUIT",
	"KEYEVENT_TOWN_TRAIN",

	"KEYEVENT_UNIT_BUILD",
	"KEYEVENT_UNIT_RETURN",
	"KEYEVENT_UNIT_SETTLE",
	"KEYEVENT_UNIT_UNLOAD",

	"KEYEVENT_BUILD_BASE",
	"KEYEVENT_BUILD_CAMP",
	"KEYEVENT_BUILD_FACTORY",
	"KEYEVENT_BUILD_HARBOR",
	"KEYEVENT_BUILD_INN",
	"KEYEVENT_BUILD_MARKET",
	"KEYEVENT_BUILD_MINE",
	"KEYEVENT_BUILD_MONSTER",
	"KEYEVENT_BUILD_RESEARCH",
	"KEYEVENT_BUILD_WAR_FACTORY",

	"KEYEVENT_MAP_MODE_CYCLE",
	"KEYEVENT_MAP_MODE0",
	"KEYEVENT_MAP_MODE1",
	"KEYEVENT_MAP_MODE2",
	"KEYEVENT_REPORT_OPAQUE_TOGGLE",
	"KEYEVENT_CLEAR_NEWS",
	"KEYEVENT_OPEN_DIPLOMATIC_MSG",
	"KEYEVENT_OPEN_OPTION_MENU",

	"KEYEVENT_TUTOR_PREV",
	"KEYEVENT_TUTOR_NEXT",

	"KEYEVENT_SAVE_GAME",
	"KEYEVENT_LOAD_GAME",

	"KEYEVENT_OBJECT_PREV",
	"KEYEVENT_OBJECT_NEXT",
	"KEYEVENT_NATION_OBJECT_PREV",
	"KEYEVENT_NATION_OBJECT_NEXT",

	"KEYEVENT_GOTO_RAW",
	"KEYEVENT_GOTO_KING",
	"KEYEVENT_GOTO_GENERAL",
	"KEYEVENT_GOTO_SPY",
	"KEYEVENT_GOTO_SHIP",
	"KEYEVENT_GOTO_CAMP",

	"KEYEVENT_CHEAT_ENABLE1",
	"KEYEVENT_CHEAT_ENABLE2",
	"KEYEVENT_CHEAT_ENABLE3",

	"KEYEVENT_MANUF_QUEUE_UP",
	"KEYEVENT_MANUF_QUEUE_DOWN",
	"KEYEVENT_MANUF_QUEUE_ADD",
	"KEYEVENT_MANUF_QUEUE_ADD_BATCH",
	"KEYEVENT_MANUF_QUEUE_REMOVE",
	"KEYEVENT_MANUF_QUEUE_REMOVE_BATCH",

	"KEYEVENT_MAX"
};

static int read_int(char *in, int *out);
static int read_bool(char *in, char *out);
static int read_key(char *in, char **out, KeyEventMap *event);

//--------- Begin of function ConfigAdv::ConfigAdv -----------//

ConfigAdv::ConfigAdv()
{
	checksum = 0;
	flags = 0;
	#ifdef DEBUG
		flags |= FLAG_DEBUG_VER;
	#endif
	#ifdef DEV_VERSION
		flags |= FLAG_DEVEL_VER;
	#endif
	#ifndef HAVE_KNOWN_BUILD
		flags |= FLAG_UNKNOWN_BUILD;
	#endif

	// this is set on program load for LocaleRes
	locale[0] = 0;
}
//--------- End of function ConfigAdv::ConfigAdv --------//


//--------- Begin of function ConfigAdv::~ConfigAdv -----------//

ConfigAdv::~ConfigAdv()
{
}
//--------- End of function ConfigAdv::ConfigAdv --------//


//--------- Begin of function ConfigAdv::init -------------//
//
int ConfigAdv::init()
{
	char filename[] = "config.txt";
	reset();
	if( !load(filename) )
	{
		reset();
		return 0;
	}
	return 1;
}

//--------- Begin of function ConfigAdv::load -------------//
//
int ConfigAdv::load(char *filename)
{
	FilePath full_path(sys.dir_config);
	full_path += filename;
	if( full_path.error_flag )
		return 0;
	if( !misc.is_file_exist(full_path) )
	{
		full_path = filename;
		if( full_path.error_flag || !misc.is_file_exist(full_path) )
			return 0;
	}

	FileTxt fileTxt(full_path);
	int line = 0;

	while( !fileTxt.is_eof() )
	{
		char *name;
		char *value;
		char save;

		line++;

		fileTxt.match_chars(" \t");

		// Skip whole-line comments. Without this a '#' line has no '=' and
		// takes the err_out path below, failing the entire load -- and
		// init() responds to a failed load by reset()ing every advanced
		// setting back to its default, so one comment silently discarded
		// the whole file. Trailing comments after a value are still not
		// supported; the value would swallow them.
		if( *fileTxt.data_ptr == '#' )
		{
			fileTxt.next_line();
			continue;
		}

		name = fileTxt.data_ptr;
		if( !fileTxt.match_chars_ex("= \t\r\n\x1a") )
		{
			fileTxt.next_line();
			continue;
		}
		fileTxt.match_chars(" \t");

		if( *fileTxt.data_ptr != '=' )
			goto err_out;
		*fileTxt.data_ptr = 0;
		fileTxt.data_ptr++;

		fileTxt.match_chars(" \t");
		value = fileTxt.data_ptr;
		if( !fileTxt.match_chars_ex("\r\n\x1a") )
			goto err_out;

		// Preserve any newline/return so next_line() can know how to correctly position.
		save = *fileTxt.data_ptr;
		*fileTxt.data_ptr = 0;

		misc.rtrim(name);
		misc.rtrim(value);
		if( !set(name, value) )
			goto err_out;

		*fileTxt.data_ptr = save;
		fileTxt.next_line();
	}

	return 1;

err_out:
	String error_msg;
	error_msg.catf(_("Error in %s at line %d"), filename, line);
	sys.show_error_dialog(error_msg);
	return 0;
}
//--------- End of function ConfigAdv::load -------------//


//--------- Begin of function ConfigAdv::vga_scale_quality_name -------------//
//
// Also the exact string SDL_HINT_RENDER_SCALE_QUALITY expects, so this is
// used both for config.txt values and for setting the SDL hint.
//
const char* ConfigAdv::vga_scale_quality_name(char mode)
{
	switch( mode )
	{
	case VGA_SCALE_NEAREST:
		return "nearest";
	case VGA_SCALE_BEST:
		return "best";
	default:
		return "linear";
	}
}
//--------- End of function ConfigAdv::vga_scale_quality_name -------------//


//--------- Begin of function ConfigAdv::persist_setting -------------//
//
// Rewrites just the "<key> = ..." line of config.txt (adding it if missing),
// leaving every other line -- including hand-written comments on unrelated
// settings -- untouched. Only the handful of fields the options menu can
// change are written back this way; every other field is load()-only,
// hand-edited.
//
int ConfigAdv::persist_setting(const char *key, const char *value)
{
	char filename[] = "config.txt";
	FilePath full_path(sys.dir_config);
	full_path += filename;
	if( full_path.error_flag )
		return 0;
	if( !misc.is_file_exist(full_path) )
	{
		// mirror load()'s fallback so we write back to wherever the file
		// actually came from, rather than creating a second copy
		full_path = filename;
		if( full_path.error_flag )
			return 0;
	}

	std::string content;
	File configFile;
	if( configFile.file_open(full_path, 0) )
	{
		long size = configFile.file_size();
		if( size > 0 )
		{
			content.resize(size);
			if( !configFile.file_read(&content[0], size) )
				content.clear();
		}
		configFile.file_close();
	}

	std::string new_line = std::string(key) + " = " + value;
	size_t key_len = strlen(key);

	std::string result;
	result.reserve(content.size() + new_line.size() + 1);

	bool replaced = false;
	size_t pos = 0;
	while( pos < content.size() )
	{
		size_t eol = content.find('\n', pos);
		size_t line_end = (eol == std::string::npos) ? content.size() : eol;

		size_t key_pos = content.find_first_not_of(" \t", pos);
		if( !replaced && key_pos != std::string::npos && key_pos < line_end &&
			 content.compare(key_pos, key_len, key) == 0 &&
			 ( key_pos+key_len == line_end || content[key_pos+key_len] == ' ' ||
			   content[key_pos+key_len] == '\t' || content[key_pos+key_len] == '=' ) )
		{
			result += new_line;
			replaced = true;
		}
		else
		{
			result.append(content, pos, line_end-pos);
		}

		if( eol != std::string::npos )
			result += '\n';

		pos = (eol == std::string::npos) ? content.size() : eol+1;
	}

	if( !replaced )
	{
		if( !result.empty() && result[result.size()-1] != '\n' )
			result += '\n';
		result += new_line;
		result += '\n';
	}

	File outFile;
	if( !outFile.file_create(full_path, 0) )
		return 0;
	int ok = result.empty() ? 1 : outFile.file_write((void*)result.data(), (unsigned)result.size());
	outFile.file_close();
	return ok;
}
//--------- End of function ConfigAdv::persist_setting -------------//


//--------- Begin of function ConfigAdv::persist_vga_scale_quality -------------//
//
int ConfigAdv::persist_vga_scale_quality()
{
	return persist_setting("vga_scale_quality", vga_scale_quality_name(vga_scale_quality));
}
//--------- End of function ConfigAdv::persist_vga_scale_quality -------------//


//--------- Begin of function ConfigAdv::persist_vga_vsync -------------//
//
int ConfigAdv::persist_vga_vsync()
{
	return persist_setting("vga_vsync", vga_vsync ? "true" : "false");
}
//--------- End of function ConfigAdv::persist_vga_vsync -------------//


//--------- Begin of function ConfigAdv::persist_vga_wide_viewport -------------//
//
int ConfigAdv::persist_vga_wide_viewport()
{
	return persist_setting("vga_wide_viewport", vga_wide_viewport ? "true" : "false");
}
//--------- End of function ConfigAdv::persist_vga_wide_viewport -------------//


//--------- Begin of function ConfigAdv::persist_scroll_frame_align -------------//
//
int ConfigAdv::persist_scroll_frame_align()
{
	return persist_setting("scroll_frame_align", scroll_frame_align ? "true" : "false");
}
//--------- End of function ConfigAdv::persist_scroll_frame_align -------------//


//--------- Begin of function ConfigAdv::persist_scroll_sub_pixel -------------//
//
int ConfigAdv::persist_scroll_sub_pixel()
{
	return persist_setting("scroll_sub_pixel", scroll_sub_pixel ? "true" : "false");
}
//--------- End of function ConfigAdv::persist_scroll_sub_pixel -------------//


//--------- Begin of function ConfigAdv::reset ---------//
//
void ConfigAdv::reset()
{
	firm_mobilize_civilian_aggressive = 0;
	firm_migrate_stricter_rules = 1;

	fix_path_blocked_by_team = 1;
	fix_recruit_dec_loyalty = 1;
	fix_sea_travel_final_move = 1;
	fix_town_unjob_worker = 1;

	locale[0] = 0;

	mine_unlimited_reserve = 0;

	monster_alternate_attack_curve = 0;
	monster_attack_divisor = 4;

	nation_ai_unite_min_relation_level = NATION_NEUTRAL;
	nation_start_god_level = 0;
	nation_start_tech_inc_all_level = 0;

	race_random_list_max = MAX_RACE;
	for (int i = 0; i < race_random_list_max; i++)
		race_random_list[i] = i+1;

	remote_compare_object_crc = 1;
	remote_compare_random_seed = 1;

	scenario_config = 1;

	// On by default. The scroll step period, 500/(scroll_speed+1) ms, is not
	// a whole multiple of the interval Vga::flip() presents at, so the two
	// clocks beat against each other and the gap between scroll steps keeps
	// switching between two different frame counts. Simulated at 1ms poll
	// granularity over 60s at the default scroll_speed=5: at a 60Hz present
	// interval (16ms) an 83ms period puts 81% of steps 5 frames apart and
	// 19% of them 6 frames apart -- a 20% longer pause about twice a second.
	// Snapping the period to the nearest whole number of present intervals
	// (80ms at 60Hz, 84ms at 165Hz) makes every gap identical, at a scroll
	// rate within ~4% of the old one. Camera position is client-local and
	// not part of the multiplayer CRC contract, so this changes presentation
	// cadence only. Set false to restore the raw 500/(scroll_speed+1) timer.
	scroll_frame_align = 1;

	// On by default. Frame-aligning the step period (above) makes the jump
	// cadence even but cannot make the jump smaller: top_x_loc is a whole-tile
	// integer, so every step is a 32px teleport of the entire view. Play-test
	// after the alignment change confirmed the remaining chop is that step
	// size, not its timing. This scrolls in pixels instead, at the same
	// average speed, by keeping a sub-tile offset that Sys::disp_zoom() folds
	// into World::view_top_x. Camera position is client-local and absent from
	// src/OMP_CRC.cpp, so this is presentation only. When on, it supersedes
	// scroll_frame_align -- there are no discrete steps left to align. Set
	// false to go back to whole-tile stepping.
	scroll_sub_pixel = 1;

	town_ai_emerge_nation_pop_limit = 60 * MAX_NATION;
	town_ai_emerge_town_pop_limit = 1000;
	town_migration = 1;
	town_loyalty_qol = 1;

	unit_ai_team_help = 1;
	unit_allow_path_power_mode = 0;
	unit_finish_attack_move = 1;
	unit_loyalty_require_local_leader = 1;
	unit_spy_fixed_target_loyalty = 0;
	unit_target_move_range_cycle = 0;

	vga_allow_highdpi = 0;

	// On by default: 1 = legacy. OpenALAudio::yield() constructs a
	// VgaFrontLock, whose ctor unlocks the front buffer, and
	// VgaBuf::unlock_buf() presents a frame on the way out -- so audio
	// housekeeping drives presentation, at whatever rate the firm/sprite
	// processing loops happen to call sys.yield(). Setting this to 0 breaks
	// that coupling and leaves presentation to the render path alone.
	//
	// It stays opt-in because how much it matters is driver-dependent, and
	// the two stacks measured disagree sharply. On SDL2 proper (the Phase 1e
	// reference machine) SDL_RenderPresent() did not block, so flip()'s
	// presentation-interval gate absorbed the extra calls and this reads as
	// a tidy-up. On sdl2-compat 2.32 over SDL3 (this machine), with
	// vga_vsync and vga_wide_viewport both on -- two settings the Options
	// menu offers -- every present blocks for a full presentation interval,
	// and because these presents are reached from inside Sys::process() they
	// block the simulation rather than the renderer. Headless fast-mode
	// medians there: 73134ms vs 154ms at 100 days, 876867ms vs 302ms at 200.
	// Same code, three orders of magnitude apart -- so it needs to stay
	// A/B-testable rather than being flipped silently for everyone.
	// docs/remaster/FINDINGS.md carries the full curve.
	vga_audio_yield_flip = 1;

	vga_full_screen = 1;
	vga_full_screen_desktop = 1;
	// On by default: both the main loop and the menu loops used to spin a
	// core at ~100% with nothing to do (measured headless: 10.9M main-loop
	// iterations to advance 550 frames; 99% of a core sitting at the main
	// menu). The nap is bounded well below the shortest frame budget and
	// never gates simulation advancement -- Sys::should_next_frame() remains
	// the sole authority on that. Set false to restore the old busy-spin if
	// a platform's sleep granularity turns out to be too coarse.
	vga_idle_sleep = 1;
	vga_keep_aspect_ratio = 1;
	vga_pause_on_focus_loss = 0;
	vga_scale_quality = VGA_SCALE_LINEAR;   // matches today's unconditional bilinear default
	// Still off by default after Phase 1e replaced flip()'s hardcoded ~17ms
	// gate with a display-derived interval. The reason changed: presentation
	// is no longer capped below the refresh rate, but a granted PRESENTVSYNC
	// was measured NOT to block at all on the reference machine (SDL 2.32,
	// Wayland, 165Hz -- see Vga::update_present_interval()), so turning it on
	// buys nothing there and the display-derived interval does the pacing
	// either way. Flip this default only after play-testing on hardware where
	// vsync demonstrably blocks.
	vga_vsync = 0;

	// Off by default: the wide viewport shows the player substantially more of
	// the map at once, which is a balance-relevant change (AI reaction to what
	// the player can see, scouting pressure) as much as a rendering one. It
	// stays opt-in until play-testing says otherwise, per the project rule
	// about flipping legacy defaults in a separate change.
	vga_wide_viewport = 0;

	vga_window_width = 0;
	vga_window_height = 0;

	wall_building_allowed = 0;

	// after applying defaults, checksum is not required
	checksum = 0;
	flags &= ~FLAG_CKSUM_REQ;
}
//--------- End of function ConfigAdv::reset ---------//


//--------- Begin of function ConfigAdv::set -------------//
//
// After any user-forced setting that modifies gameplay, update_check_sum().
// Non-gameplay settings will not require a checksum.
int ConfigAdv::set(char *name, char *value)
{
	if( !strcmp(name, "bindkey") )
	{
		KeyEventMap event;
		char *key;
		if( !read_key(value, &key, &event) || !mouse.bind_key(event.type, key) )
			return 0;
	}
	else if( !strcmp(name, "firm_mobilize_civilian_aggressive") )
	{
		if( !read_bool(value, &firm_mobilize_civilian_aggressive) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "firm_migrate_stricter_rules") )
	{
		if( !read_bool(value, &firm_migrate_stricter_rules) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "fix_path_blocked_by_team") )
	{
		if( !read_bool(value, &fix_path_blocked_by_team) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "fix_recruit_dec_loyalty") )
	{
		if( !read_bool(value, &fix_recruit_dec_loyalty) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "fix_sea_travel_final_move") )
	{
		if( !read_bool(value, &fix_sea_travel_final_move) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "fix_town_unjob_worker") )
	{
		if( !read_bool(value, &fix_town_unjob_worker) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "locale") )
	{
		strncpy(locale, value, LOCALE_LEN);
		locale[LOCALE_LEN] = 0;
	}
	else if( !strcmp(name, "mine_unlimited_reserve") )
	{
		if( !read_bool(value, &mine_unlimited_reserve) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "monster_alternate_attack_curve") )
	{
		if( !read_bool(value, &monster_alternate_attack_curve) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "monster_attack_divisor") )
	{
		if( !read_int(value, &monster_attack_divisor) )
			return 0;
		if( CHECK_BOUND(monster_attack_divisor, 1, 6) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "nation_ai_unite_min_relation_level") )
	{
		if( !strcmpi(value, "hostile") )
			nation_ai_unite_min_relation_level = NATION_HOSTILE;
		else if( !strcmpi(value, "tense") )
			nation_ai_unite_min_relation_level = NATION_TENSE;
		else if( !strcmpi(value, "neutral") )
			nation_ai_unite_min_relation_level = NATION_NEUTRAL;
		else if( !strcmpi(value, "friendly") )
			nation_ai_unite_min_relation_level = NATION_FRIENDLY;
		else if( !strcmpi(value, "alliance") )
			nation_ai_unite_min_relation_level = NATION_ALLIANCE;
		else if( !strcmpi(value, "off") )
			nation_ai_unite_min_relation_level = NATION_ALLIANCE+1; // disables
		else
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "nation_start_god_level") )
	{
		if( !read_int(value, &nation_start_god_level) )
			return 0;
		if( CHECK_BOUND(nation_start_god_level, 0, 2) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "nation_start_tech_inc_all_level") )
	{
		if( !read_int(value, &nation_start_tech_inc_all_level) )
			return 0;
		if( CHECK_BOUND(nation_start_tech_inc_all_level, 0, 2) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "race_random_list") )
	{
		// the game defaults to all
		if( !strcmpi(value, "original") )
		{
			race_random_list_max = 7;
			for (int i = 0; i < race_random_list_max; i++)
				race_random_list[i] = i+1;
			update_check_sum(name, value);
		}
		else
		{
			return 0;
		}
	}
	else if( !strcmp(name, "remote_compare_object_crc") )
	{
		if( !read_bool(value, &remote_compare_object_crc) )
			return 0;
	}
	else if( !strcmp(name, "remote_compare_random_seed") )
	{
		if( !read_bool(value, &remote_compare_random_seed) )
			return 0;
	}
	else if( !strcmp(name, "scenario_config") )
	{
		if( !read_bool(value, &scenario_config) )
			return 0;
	}
	else if( !strcmp(name, "scroll_frame_align") )
	{
		if( !read_bool(value, &scroll_frame_align) )
			return 0;
	}
	else if( !strcmp(name, "scroll_sub_pixel") )
	{
		if( !read_bool(value, &scroll_sub_pixel) )
			return 0;
	}
	else if( !strcmp(name, "town_ai_emerge_nation_pop_limit") )
	{
		if( !read_int(value, &town_ai_emerge_nation_pop_limit) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "town_ai_emerge_town_pop_limit") )
	{
		if( !read_int(value, &town_ai_emerge_town_pop_limit) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "town_migration") )
	{
		if( !read_bool(value, &town_migration) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "town_loyalty_qol") )
	{
		if( !read_bool(value, &town_loyalty_qol) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "unit_ai_team_help") )
	{
		if( !read_bool(value, &unit_ai_team_help) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "unit_finish_attack_move") )
	{
		if( !read_bool(value, &unit_finish_attack_move) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "unit_loyalty_require_local_leader") )
	{
		if( !read_bool(value, &unit_loyalty_require_local_leader) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "unit_allow_path_power_mode") )
	{
		if( !read_bool(value, &unit_allow_path_power_mode) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "unit_spy_fixed_target_loyalty") )
	{
		if( !read_bool(value, &unit_spy_fixed_target_loyalty) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "unit_target_move_range_cycle") )
	{
		if( !read_bool(value, &unit_target_move_range_cycle) )
			return 0;
		update_check_sum(name, value);
	}
	else if( !strcmp(name, "vga_allow_highdpi") )
	{
		if( !read_bool(value, &vga_allow_highdpi) )
			return 0;
	}
	else if( !strcmp(name, "vga_audio_yield_flip") )
	{
		if( !read_bool(value, &vga_audio_yield_flip) )
			return 0;
	}
	else if( !strcmp(name, "vga_full_screen") )
	{
		if( !read_bool(value, &vga_full_screen) )
			return 0;
	}
	else if( !strcmp(name, "vga_full_screen_desktop") )
	{
		if( !read_bool(value, &vga_full_screen_desktop) )
			return 0;
	}
	else if( !strcmp(name, "vga_idle_sleep") )
	{
		if( !read_bool(value, &vga_idle_sleep) )
			return 0;
	}
	else if( !strcmp(name, "vga_keep_aspect_ratio") )
	{
		if( !read_bool(value, &vga_keep_aspect_ratio) )
			return 0;
	}
	else if( !strcmp(name, "vga_pause_on_focus_loss") )
	{
		if( !read_bool(value, &vga_pause_on_focus_loss) )
			return 0;
	}
	else if( !strcmp(name, "vga_scale_quality") )
	{
		if( !strcmpi(value, "nearest") )
			vga_scale_quality = VGA_SCALE_NEAREST;
		else if( !strcmpi(value, "linear") )
			vga_scale_quality = VGA_SCALE_LINEAR;
		else if( !strcmpi(value, "best") )
			vga_scale_quality = VGA_SCALE_BEST;
		else
			return 0;
	}
	else if( !strcmp(name, "vga_vsync") )
	{
		if( !read_bool(value, &vga_vsync) )
			return 0;
	}
	else if( !strcmp(name, "vga_wide_viewport") )
	{
		if( !read_bool(value, &vga_wide_viewport) )
			return 0;
	}
	else if( !strcmp(name, "vga_window_height") )
	{
		if( !read_int(value, &vga_window_height) )
			return 0;
	}
	else if( !strcmp(name, "vga_window_width") )
	{
		if( !read_int(value, &vga_window_width) )
			return 0;
	}
	else if( !strcmp(name, "wall_building_allowed") )
	{
		if( !read_bool(value, &wall_building_allowed) )
			return 0;
	}
	else
	{
		return 0;
	}
	return 1;
}
//--------- End of function ConfigAdv::set -------------//


//--------- Begin of function ConfigAdv::update_check_sum -------------//
//
void ConfigAdv::update_check_sum(char *name, char *value)
{
	checksum += misc.check_sum(name);
	checksum += misc.check_sum(value);
	flags |= FLAG_CKSUM_REQ;
}
//--------- End of function ConfigAdv::update_check_sum -------------//


static int read_int(char *in, int *out)
{
	char *endptr;
	int tmp = strtol(in, &endptr, 10);
	if( endptr == in || *endptr )
		return 0;
	*out = tmp;
	return 1;
}


static int read_bool(char *in, char *out)
{
	if( !strcmpi(in, "true") )
		*out = 1;
	else if( !strcmpi(in, "false") )
		*out = 0;
	else
		return 0;
	return 1;
}


static int read_key(char *in, char **out, KeyEventMap *event)
{
	char *p = strchr(in, ',');
	if( !p )
		return 0;
	*p = 0;
	p++;
	if( !*p )
		return 0;
	*out = p;
	int i;
	for( i=0; i<(int)KEYEVENT_MAX; i++ )
		if( strcmp(keyevent_map[i], in)==0 )
			break;
	if( i>=(int)KEYEVENT_MAX )
		return 0;
	event->index = i;
	return 1;
}
