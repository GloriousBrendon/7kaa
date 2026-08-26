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

//Filename    : OWORLDMT.H
//Description : Header file for World Matrix WorldMap & WorldZoom

#ifndef __OWORLDMT_H
#define __OWORLDMT_H

#ifndef __OMATRIX_H
#include <OMATRIX.h>
#endif

#ifndef __ODYNARR_H
#include <ODYNARR.h>
#endif

//-------- World matrix size ------------//

#define MAX_WORLD_X_LOC  (World::max_x_loc)
#define MAX_WORLD_Y_LOC  (World::max_y_loc)

//------------- Map window -------------//

#define MAP_WIDTH       MAX_WORLD_X_LOC
#define MAP_HEIGHT      MAX_WORLD_Y_LOC

#define MAX_MAP_WIDTH	200
#define MAX_MAP_HEIGHT	200

// The minimap rides along with the docked sidebar (see OINFO.h ::
// hud_sidebar_x_offset); its y position is unchanged.
#define MAP_X1          (588+hud_sidebar_x_offset+(MAX_MAP_WIDTH-MAP_WIDTH)/2)
#define MAP_Y1          (56 +(MAX_MAP_HEIGHT-MAP_HEIGHT)/2)
#define MAP_X2          (MAP_X1+MAP_WIDTH-1)
#define MAP_Y2          (MAP_Y1+MAP_HEIGHT-1)

#define MAP_LOC_HEIGHT   1 		// when MAP_VIEW_ENTIRE
#define MAP_LOC_WIDTH    1

#define MAP2_LOC_HEIGHT  2			// when MAP_VIEW_SECTION
#define MAP2_LOC_WIDTH   2

//----------- Zoom window -------------//

// The historical World Zoom Window: 576x544 = 18x17 tiles, with the HUD
// chrome occupying the top strip (y < 56) and the right sidebar (x > 575).
//
// Report and dialog screens draw *inside* this rect and lay themselves out
// with enum tables derived from it (see e.g. OR_TRADE.cpp's
// CARAVAN_BROWSE_X1), so they need it to stay a compile-time constant.
// They keep these ZOOM_LEGACY_* values and therefore keep their exact 1997
// layout no matter how large the buffer gets.
#define ZOOM_LEGACY_X1       0
#define ZOOM_LEGACY_Y1      56
#define ZOOM_LEGACY_X2     575
#define ZOOM_LEGACY_Y2     599

#define ZOOM_LEGACY_WIDTH   576
#define ZOOM_LEGACY_HEIGHT  544

// The live map viewport. Sprite drawing, clipping and mouse picking all use
// these; they grow to fill whatever the buffer leaves between the docked HUD
// chrome, which is what puts more tiles on screen at native sprite scale.
// Always the ZOOM_LEGACY_* values unless the wide-viewport mode is on.
extern int zoom_win_x1, zoom_win_y1, zoom_win_x2, zoom_win_y2;
extern int zoom_win_width, zoom_win_height;

// How far right of its legacy x=576 origin the docked HUD sidebar (minimap +
// info panel) sits. 0 whenever the buffer is the legacy 800 wide.
extern int hud_sidebar_x_offset;

#define ZOOM_X1         zoom_win_x1     // World Zoom Window
#define ZOOM_Y1         zoom_win_y1
#define ZOOM_X2         zoom_win_x2
#define ZOOM_Y2         zoom_win_y2

#define ZOOM_WIDTH      zoom_win_width  // ZOOM_LOC_WIDTH(32)  * 18 = 576 legacy
#define ZOOM_HEIGHT     zoom_win_height // ZOOM_LOC_HEIGHT(32) * 17 = 544 legacy

#define ZOOM_LOC_HEIGHT  32     // in world zoom window
#define ZOOM_LOC_WIDTH   32

#define ZOOM_X_SHIFT_COUNT  5    // x>>5 = xLoc
#define ZOOM_Y_SHIFT_COUNT  5    // y>>5 = yLoc

#define ZOOM_X_PIXELS  (MAX_WORLD_X_LOC * ZOOM_LOC_WIDTH)
#define ZOOM_Y_PIXELS  (MAX_WORLD_Y_LOC * ZOOM_LOC_HEIGHT)

//---------- define map modes -----------//

#define MAP_MODE_COUNT  3

enum { MAP_MODE_TERRAIN=0,
		 MAP_MODE_POWER,
		 MAP_MODE_SPOT,
	  };

//-------- Define class MapMatrix -------//

class MapMatrix : public Matrix
{
public:
	char  last_map_mode;
	char	map_mode;
	char	power_mode;		// 1-also display power regions on the zoom map, 0-only display power regions on the mini map

public:
	MapMatrix();
   ~MapMatrix();

	void init_para();
	void draw();
	void paint();
	void disp();
	void draw_square();
	int  detect();
	void toggle_map_mode(int modeId);
	void cycle_map_mode();

protected:
	void draw_map();
	int  detect_area();

	void disp_mode_button(int putFront=0);
};

//-------- Define class ZoomMatrix -------//

class ZoomMatrix : public Matrix
{
public:
	DynArray land_disp_sort_array;     // an array for displaying objects in a sorted order
	DynArray air_disp_sort_array;
	DynArray land_top_disp_sort_array;
	DynArray land_bottom_disp_sort_array;

	int	init_rain;
	int	rain_channel_id;
	int	wind_channel_id;
	int	fire_channel_id;
	int	last_fire_vol;
	int	init_lightning; // reset on new game, save on save game
	int	init_snow;
	short	last_brightness;
	int	vibration; // reset on new game, save on save game
	short	lightning_x1, lightning_y1, lightning_x2, lightning_y2; // save on save game

	// Sub-tile scroll offset in pixels, always 0 <= sub_x < ZOOM_LOC_WIDTH.
	// The camera's true position is top_x_loc whole tiles PLUS sub_x pixels;
	// Sys::disp_zoom() folds both into World::view_top_x, which is the single
	// pixel-space origin every draw site reads. Deliberately NOT saved:
	// src/OGFILE2.cpp writes zoom matrix fields one at a time and restores
	// the camera as a whole-tile position, so a save round-trip simply lands
	// on a tile boundary with sub_x/sub_y back at 0.
	int	sub_x, sub_y;

public:
   ZoomMatrix();

	void init_para();
	void draw();
	void draw_frame();
	void scroll(int,int);
	// move the camera by a pixel delta, carrying into top_x_loc/top_y_loc
	void scroll_pixel(int xPixel, int yPixel);
	void draw_white_site();
	void put_bitmap_clip(int x, int y, char* bitmapPtr,int compressedFlag=0);
	void put_bitmap_remap_clip(int x, int y, char* bitmapPtr, char* colorRemapTable=NULL,int compressedFlag=0);
	int  detect_bitmap_clip(int x, int y, char* bitmapPtr);
	bool is_bitmap_clip(int x, int y, char* bitmapPtr);

protected:
	void draw_objects();
	void draw_objects_now(DynArray* unitArray, int = 0);

	void draw_weather_effects();

	void draw_build_marker();
	void draw_god_cast_range();

	void blacken_unexplored();
	void blacken_fog_of_war();

	void disp_text();
	void put_center_text(int x, int y, const char* str);
};

//------------------------------------------------//

#endif
