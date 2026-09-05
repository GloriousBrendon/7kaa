/*
 * Seven Kingdoms: Ancient Adversaries
 *
 * Copyright 1997,1998 Enlight Software Ltd.
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

//Filename    : OAIHARN.CPP
//Description : Phase 4 AI harness reporting (scripts/phase4_ai_harness.sh)

#include <stdio.h>

#include <OAIHARN.h>
#include <CmdLine.h>
#include <ConfigAdv.h>
#include <OCONFIG.h>
#include <OINFO.h>
#include <ONATION.h>
#include <ONATIONA.h>
#include <OFIRM.h>
#include <OFIRMA.h>
#include <OFIRMID.h>
#include <OFIRMRES.h>
#include <OTOWN.h>
#include <OUNIT.h>
#include <OCRC_STO.h>

AiHarness ai_harness;

// The AI think dispatcher in Nation::process_ai_main() (src/OAI_MAIN.cpp)
// selects one of twelve think categories per day with
//     (info.game_date - nation_recno*4) % intervalDays
// where intervalDays comes from this table, indexed by
// config.ai_aggressiveness - OPTION_LOW. Mirrored here purely to report the
// value in the header, so a baseline records how much AI a given day count
// actually observes; the AI file itself is not touched.
static const short AI_THINK_INTERVAL_DAYS[] = { 90, 30, 15, 15 };
enum { AI_THINK_CATEGORY_COUNT = 12 };


// Column names for the AIH_N / AIH_S rows, emitted once into the run header so
// the series is self-describing and a reader never has to count fields.
//
// Two caveats a reader has to know, both properties of the game rather than of
// this harness:
//
//  - REFRESH RATE IS NOT UNIFORM. cash, food, units, humans, weapons, ships,
//    firms, at_war and profit are maintained as they change, so they move
//    daily. pop, jobless, spies and the four *_rating columns are recomputed in
//    NationArray::update_statistic(), which runs from next_month() - so those
//    step once a month and are flat in between. A 500-day run samples them ~16
//    times, which is enough to trend but not to date a change precisely.
//
//  - "units" IS NOT unit_array. NationBase::total_unit_count counts soldiers
//    assigned into firms as well as mobile units (src/OFIRM.cpp:2136 increments
//    it for firm workers) and excludes kings (src/OUNIT.cpp:1928). "humans" is
//    total_human_count, the figure NationBase::has_people() - and therefore
//    check_lose() - actually tests.
//
static const char AIH_NATION_COLUMNS[] =
	"day nation cash food pop jobless units humans generals weapons ships "
	"firms spies reputation pop_rating mil_rating eco_rating overall_rating "
	"at_war peace_days last_build_firm_day profit "
	"f_base f_factory f_inn f_market f_camp f_mine f_research f_warfact "
	"f_harbor f_monster";


AiHarness::AiHarness()
{
	active = 0;
	start_date = 0;
	last_date = 0;
	requested_days = 0;
}


int AiHarness::days_elapsed() const
{
	return active ? last_date - start_date : 0;
}


//------- Begin of function AiHarness::begin --------//
//
// Emit the run header: every pinned knob, recorded rather than inherited,
// so a baseline states the conditions it was measured under.
//
void AiHarness::begin(int requestedDays)
{
	active = 1;
	start_date = info.game_date;
	last_date = info.game_date;
	requested_days = requestedDays;

	int aggIndex = config.ai_aggressiveness - OPTION_LOW;
	int thinkInterval = 0;
	if( aggIndex >= 0 &&
		 aggIndex < (int)(sizeof(AI_THINK_INTERVAL_DAYS)/sizeof(*AI_THINK_INTERVAL_DAYS)) )
		thinkInterval = AI_THINK_INTERVAL_DAYS[aggIndex];

	int nationCount = 0;
	for( int n = 1; n <= nation_array.size(); ++n )
		if( !nation_array.is_deleted(n) )
			nationCount++;

	printf("AIH_VERSION=1\n");
	printf("AIH_SEED=%d\n", info.random_seed);
	printf("AIH_DAYS_REQUESTED=%d\n", requested_days);
	printf("AIH_START_DATE=%d\n", start_date);
	printf("AIH_AI_NATION_COUNT=%d\n", (int)config.ai_nation_count);
	printf("AIH_NATIONS_CREATED=%d\n", nationCount);
	printf("AIH_PLAYER_RECNO=%d\n", (int)nation_array.player_recno);
	printf("AIH_AI_AGGRESSIVENESS=%d\n", (int)config.ai_aggressiveness);
	printf("AIH_AI_THINK_INTERVAL_DAYS=%d\n", thinkInterval);
	printf("AIH_AI_THINK_CATEGORIES=%d\n", (int)AI_THINK_CATEGORY_COUNT);
	printf("AIH_DIFFICULTY_LEVEL=%d\n", (int)config.difficulty_level);
	printf("AIH_DISABLE_AI=%d\n", (int)config.disable_ai_flag);
	printf("AIH_FOG_OF_WAR=%d\n", (int)config.fog_of_war);
	printf("AIH_BLACKEN_MAP=%d\n", (int)config.blacken_map);
	printf("AIH_KING_UNDIE=%d\n", (int)config.king_undie_flag);
	printf("AIH_FAST_BUILD=%d\n", (int)config.fast_build);
	printf("AIH_RANDOM_START_UP=%d\n", (int)config.random_start_up);
	printf("AIH_EXPLORE_WHOLE_MAP=%d\n", (int)config.explore_whole_map);
	printf("AIH_START_UP_CASH=%d\n", (int)config.start_up_cash);
	printf("AIH_AI_START_UP_CASH=%d\n", (int)config.ai_start_up_cash);
	printf("AIH_TERRAIN_SET=%d\n", (int)config.terrain_set);
	printf("AIH_LATITUDE=%d\n", (int)config.latitude);
	printf("AIH_LAND_MASS=%d\n", (int)config.land_mass);
	printf("AIH_START_UP_INDEPENDENT_TOWN=%d\n", (int)config.start_up_independent_town);
	printf("AIH_START_UP_RAW_SITE=%d\n", (int)config.start_up_raw_site);
	printf("AIH_START_UP_HAS_MINE_NEARBY=%d\n", (int)config.start_up_has_mine_nearby);
	printf("AIH_INDEPENDENT_TOWN_RESISTANCE=%d\n", (int)config.independent_town_resistance);
	printf("AIH_WEATHER_EFFECT=%d\n", (int)config.weather_effect);
	printf("AIH_RANDOM_EVENT_FREQUENCY=%d\n", (int)config.random_event_frequency);
	printf("AIH_MONSTER_TYPE=%d\n", (int)config.monster_type);
	printf("AIH_NEW_NATION_EMERGE=%d\n", (int)config.new_nation_emerge);
	printf("AIH_NEW_INDEPENDENT_TOWN_EMERGE=%d\n", (int)config.new_independent_town_emerge);
	printf("AIH_GOAL_DESTROY_MONSTER=%d\n", (int)config.goal_destroy_monster);
	printf("AIH_GOAL_POPULATION_FLAG=%d\n", (int)config.goal_population_flag);
	printf("AIH_GOAL_ECONOMIC_SCORE_FLAG=%d\n", (int)config.goal_economic_score_flag);
	printf("AIH_GOAL_TOTAL_SCORE_FLAG=%d\n", (int)config.goal_total_score_flag);
	printf("AIH_GOAL_YEAR_LIMIT_FLAG=%d\n", (int)config.goal_year_limit_flag);
	printf("AIH_FRAME_SPEED=%d\n", (int)config.frame_speed);
	// Which config.txt this run actually read. ConfigAdv::load() falls back to
	// a bare relative path that resolves inside the data dir, so a run that
	// silently picked up a stray config.txt must be visible, not guessed at.
	printf("AIH_CONFIG_PATH=%s\n",
			 config_adv.loaded_path[0] ? config_adv.loaded_path : "(none)");
	printf("AIH_COLUMNS=%s\n", AIH_NATION_COLUMNS);
	printf("AIH_HEADER_END=1\n");
	fflush(stdout);
}
//--------- End of function AiHarness::begin ---------//


//------- Begin of function AiHarness::emit_nation_line --------//
//
// One row per nation. Everything here is read back out of NationBase and
// firm_res after the fact - no AI file is touched, and nothing in the AI knows
// this is running.
//
void AiHarness::emit_nation_line(const char* tag, int dayIndex, int nationRecno)
{
	Nation* nationPtr = nation_array[nationRecno];

	printf("%s %d %d %.2f %.2f %d %d %d %d %d %d %d %d %d %.2f %d %d %d %d %d %d %d %.2f",
			 tag,
			 dayIndex,
			 nationRecno,
			 nationPtr->cash,
			 nationPtr->food,
			 nationPtr->total_population,
			 nationPtr->total_jobless_population,
			 nationPtr->total_unit_count,
			 nationPtr->total_human_count,
			 nationPtr->total_general_count,
			 nationPtr->total_weapon_count,
			 nationPtr->total_ship_count,
			 nationPtr->total_firm_count,
			 nationPtr->total_spy_count,
			 nationPtr->reputation,
			 nationPtr->population_rating,
			 nationPtr->military_rating,
			 nationPtr->economic_rating,
			 nationPtr->overall_rating,
			 (int)nationPtr->is_at_war_today,
			 nationPtr->peaceful_days(),
			 nationPtr->last_build_firm_date - start_date,
			 nationPtr->cur_year_profit);

	// Per-firm-type counts, from FirmInfo::nation_firm_count_array. This is the
	// column that makes think_close_camp() measurable: a camp opened and closed
	// again inside one month is invisible in a monthly total but shows here as
	// f_camp stepping up and back down on consecutive days.
	for( int firmId = 1; firmId <= MAX_FIRM_TYPE; ++firmId )
		printf(" %d", (int)firm_res[firmId]->nation_firm_count_array[nationRecno-1]);

	printf("\n");
}
//--------- End of function AiHarness::emit_nation_line ---------//


//------- Begin of function AiHarness::record_day --------//
//
// Called once per new in-game day, after Sys::process() has returned, so the
// day being reported is complete.
//
void AiHarness::record_day()
{
	if( !active || info.game_date == last_date )
		return;

	last_date = info.game_date;
	int dayIndex = last_date - start_date;

	// (a) the determinism guard. Same aggregate CrcStore uses for multiplayer
	// desync detection, sampled once per day instead of once per sync frame.
	crc_store.record_all();
	printf("AIH_D %d %u\n", dayIndex, (unsigned)crc_store.frame_check_num);

	// (b) the behaviour verdict.
	for( int n = 1; n <= nation_array.size(); ++n )
	{
		if( nation_array.is_deleted(n) )
			continue;
		emit_nation_line("AIH_N", dayIndex, n);
	}
}
//--------- End of function AiHarness::record_day ---------//


//------- Begin of function AiHarness::finish --------//
//
void AiHarness::finish(int gameOver, uint32_t wallMs, uint32_t busyMs,
							  uint32_t frameIters, uint32_t loopIters)
{
	if( !active )
		return;

	int nationsAlive = 0;
	for( int n = 1; n <= nation_array.size(); ++n )
		if( !nation_array.is_deleted(n) )
			nationsAlive++;

	crc_store.record_all();

	printf("AIH_SUMMARY_BEGIN=1\n");
	printf("AIH_DAYS=%d\n", days_elapsed());
	printf("AIH_GAME_OVER=%d\n", gameOver ? 1 : 0);
	printf("AIH_NATIONS_ALIVE=%d\n", nationsAlive);
	printf("AIH_FINAL_CRC=%u\n", (unsigned)crc_store.frame_check_num);
	// packed_size(), not size(): DynArray::size() is the high-water mark of the
	// slot array and keeps counting slots freed by deletion. These are also
	// world totals, so they include independent towns and monster firms - they
	// will not agree with the sum of the per-nation columns, and are not meant
	// to.
	printf("AIH_WORLD_UNITS=%d\n", unit_array.packed_size());
	printf("AIH_WORLD_FIRMS=%d\n", firm_array.packed_size());
	printf("AIH_WORLD_TOWNS=%d\n", town_array.packed_size());
	printf("AIH_WALLTIME_MS=%u\n", (unsigned)wallMs);
	printf("AIH_BUSY_MS=%u\n", (unsigned)busyMs);
	printf("AIH_FRAME_ITERS=%u\n", (unsigned)frameIters);
	printf("AIH_LOOP_ITERS=%u\n", (unsigned)loopIters);

	// (c) final per-nation state, same columns as the AIH_N series.
	for( int n = 1; n <= nation_array.size(); ++n )
	{
		if( nation_array.is_deleted(n) )
			continue;
		emit_nation_line("AIH_S", days_elapsed(), n);
	}

	// Firms standing at the end, broken down by nation and firm type. This is
	// what think_build_firm() actually produced, read back out of firm_array
	// rather than counted inside the AI.
	for( int n = 1; n <= nation_array.size(); ++n )
	{
		if( nation_array.is_deleted(n) )
			continue;

		for( int firmId = 1; firmId <= MAX_FIRM_TYPE; ++firmId )
		{
			int count = 0;
			for( int f = 1; f <= firm_array.size(); ++f )
			{
				if( firm_array.is_deleted(f) )
					continue;
				Firm* firmPtr = firm_array[f];
				if( firmPtr->nation_recno == n && firmPtr->firm_id == firmId )
					count++;
			}
			if( count )
				printf("AIH_FIRM %d %d %d %s\n", n, firmId, count,
						 firm_res[firmId]->name);
		}
	}

	printf("AIH_SUMMARY_END=1\n");
	fflush(stdout);
}
//--------- End of function AiHarness::finish ---------//
