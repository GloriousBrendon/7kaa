/*
 * Seven Kingdoms: Ancient Adversaries
 *
 * Copyright 2018 Jesse Allen
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

//Filename    : CmdLine.cpp
//Description : Command line processing

#include <CmdLine.h>
#include <ConfigAdv.h>
#include <OCONFIG.h>
#include <OSYS.h>
#include <gettext.h>

CmdLine::CmdLine()
{
	enable_audio = 1;
	enable_if = 1;
	screenshot_path = NULL;
	rnd = 0;
	game_speed = -1;
	startup_mode = STARTUP_NORMAL;
	join_host = NULL;
	harness_days = 0;
	ai_harness_days = 0;
}

CmdLine::~CmdLine()
{
}

static int have_arg(int i, int argc, const char *option)
{
	if( i >= argc - 1 )
	{
		sys.show_error_dialog(_("The command line option %s requires an argument."), option);
		return 0;
	}
	return 1;
}

static int set_startup_mode(StartupMode mode)
{
	if( cmd_line.startup_mode != STARTUP_NORMAL )
	{
		sys.show_error_dialog(_("Multiple startup mode options present on the command line."));
		return 0;
	}
	cmd_line.startup_mode = mode;
	return 1;
}

// Command line paramters:
// -demo
//   Start a new game in observer mode
// -host
//   Begin the program by hosting a multiplayer match
// -join <named or ip address>
//   Begin the program by attempting to connect to the specified address.
// -name <player name>
//   Set the name you wish to be known as.
// -speed <game speed>
//   Set the initial game speed (not for multiplayer)
// -headless-test-days <n>
//   Run the built-in test scenario (Battle::run_test) and exit after n
//   in-game days, printing a HARNESS_CRC/HARNESS_WALLTIME_MS summary line.
//   Used by scripts/phase0_harness.sh for the deterministic regression check.
// -headless-ai-days <n>
//   Run the all-AI economy scenario (Battle::run_ai_test) and exit after n
//   in-game days, printing a per-day CRC series, a per-day per-nation metric
//   series and an end-of-run summary. Used by scripts/phase4_ai_harness.sh to
//   measure AI behaviour. Separate from -headless-test-days on purpose: that
//   scenario and its baseline are the determinism anchor and never change.
int CmdLine::init(int argc, char **argv)
{
	const char *lobbyJoinOption = "-join";
	const char *lobbyHostOption = "-host";
	const char *lobbyNameOption = "-name";
	const char *demoOption = "-demo";
	const char *noAudioOption = "-noaudio";
	const char *noIfOption = "-noif";
	const char *rndOption = "-rnd";
	const char *speedOption = "-speed";
	const char *windowOption = "-win";
	const char *harnessDaysOption = "-headless-test-days";
	const char *aiHarnessDaysOption = "-headless-ai-days";
	// Companion to the above for layout review: renders the test scenario and
	// writes the frame to a BMP so the HUD layout can be inspected at a given
	// buffer size without a display attached.
	const char *screenshotOption = "-headless-screenshot";
	for( int i = 1; i < argc; i++ )
	{
		if( !strcmp(argv[i], lobbyJoinOption) )
		{
			if( !have_arg(i, argc, lobbyJoinOption) )
				return 0;
			set_startup_mode(STARTUP_MULTI_PLAYER);
			join_host = argv[++i];
		}
		else if( !strcmp(argv[i], lobbyHostOption) )
		{
			set_startup_mode(STARTUP_MULTI_PLAYER);
		}
		else if( !strcmp(argv[i], lobbyNameOption) )
		{
			if( !have_arg(i, argc, lobbyNameOption) )
				return 0;
			strncpy(config.player_name, argv[++i], HUMAN_NAME_LEN);
			config.player_name[HUMAN_NAME_LEN] = 0;
		}
		else if( !strcmp(argv[i], demoOption) )
		{
			set_startup_mode(STARTUP_DEMO);
		}
		else if( !strcmp(argv[i], screenshotOption) )
		{
			if( !have_arg(i, argc, screenshotOption) )
				return 0;
			screenshot_path = argv[++i];
		}
		else if( !strcmp(argv[i], noAudioOption) )
		{
			enable_audio = 0;
		}
		else if( !strcmp(argv[i], noIfOption) )
		{
			if( cmd_line.startup_mode == STARTUP_DEMO )
			{
				enable_audio = 0;
				enable_if = 0;
			}
		}
		else if( !strcmp(argv[i], rndOption) )
		{
			if( !have_arg(i, argc, rndOption) )
				return 0;
			rnd = atoi(argv[++i]);
		}
		else if( !strcmp(argv[i], speedOption) )
		{
			if( !have_arg(i, argc, speedOption) )
				return 0;
			game_speed = atoi(argv[++i]);
		}
		else if( !strcmp(argv[i], windowOption) )
		{
			config_adv.vga_full_screen = 0;
		}
		else if( !strcmp(argv[i], harnessDaysOption) )
		{
			if( !have_arg(i, argc, harnessDaysOption) )
				return 0;
			if( !set_startup_mode(STARTUP_TEST) )
				return 0;
			harness_days = atoi(argv[++i]);
		}
		else if( !strcmp(argv[i], aiHarnessDaysOption) )
		{
			if( !have_arg(i, argc, aiHarnessDaysOption) )
				return 0;
			if( !set_startup_mode(STARTUP_AI_HARNESS) )
				return 0;
			ai_harness_days = atoi(argv[++i]);
		}
	}
	return 1;
}
