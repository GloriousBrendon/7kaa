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

//Filename    : OAIHARN.H
//Description : Phase 4 AI harness reporting (scripts/phase4_ai_harness.sh)

#ifndef __OAIHARN_H
#define __OAIHARN_H

#include <stdint.h>

//
// Emits the three signals the Phase 4 AI harness reads, all of them from
// outside the AI: nothing in this class is called from any OAI_* file, and
// no OAI_* file was changed to support it. Everything reported is read back
// out of NationBase, firm_array/firm_res and CrcStore after the fact.
//
//   (a) a per-day CRC series      - the determinism guard
//   (b) a per-day per-nation      - the behaviour verdict. Load-bearing:
//       metric series               CrcStore only ever hashes the NationBase
//                                   subobject, so the AI's plan (everything
//                                   Nation adds on top) is invisible to (a).
//       	                          See docs/remaster/FINDINGS.md.
//   (c) an end-of-run summary     - the human-readable digest
//
// Every line is prefixed AIH_ so the scenario's own stdout can be grepped
// apart from it. Active only when cmd_line.ai_harness_days > 0.
//
class AiHarness
{
public:
	AiHarness();

	void	begin(int requestedDays);
	void	record_day();
	void	finish(int gameOver, uint32_t wallMs, uint32_t busyMs,
			   uint32_t frameIters, uint32_t loopIters);

	int	is_active() const	{ return active; }
	int	days_elapsed() const;

private:
	void	emit_nation_line(const char* tag, int dayIndex, int nationRecno);

	int		active;
	int		start_date;
	int		last_date;
	int		requested_days;
};

extern AiHarness ai_harness;

#endif
