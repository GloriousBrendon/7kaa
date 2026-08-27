/*
 * Seven Kingdoms: Ancient Adversaries
 *
 * Copyright 1997,1998 Enlight Software Ltd.
 * Copyright 2010,2015 Jesse Allen
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

//Filename    : OVGA.cpp
//Description : VGA management class (SDL version)

#include <OVGA.h>
#include <OMOUSE.h>
#include <OCOLTBL.h>
#include <OSYS.h>
#include <dbglog.h>
#include <version.h>
#include <FilePath.h>
#include <ConfigAdv.h>
#include <OWORLDMT.h>   // ZOOM_LEGACY_* and the runtime layout variables it declares
#include <CmdLine.h>

DBGLOG_DEFAULT_CHANNEL(Vga);

//--------- Declare static functions ---------//

static void init_dpi();
static int init_window_flags();
static void init_window_size();

//------ Frame presentation pacing ---------//

// Used only when the display's real refresh rate can't be read. It is a
// fallback for missing information, not a target: any value here is wrong
// on some display, which is exactly why the old hardcoded ~17ms gate was
// wrong on all of them.
#define PRESENT_FALLBACK_HZ         60

// How often flip() re-derives the interval, so moving the window to a
// second monitor or changing the desktop mode is picked up without wiring
// up display-hotplug events (SDL_WINDOWEVENT_DISPLAY_CHANGED needs 2.0.18;
// this build supports back to 2.0.4).
#define PRESENT_INTERVAL_RECHECK_MS 1000

//------ Define the runtime buffer size ---------//

// Defaults to the legacy fixed resolution; Vga::init() is what may widen it.
// Statically initialised because VgaBuf and MouseCursor globals are
// constructed before any of our init code runs.
int vga_buf_width  = VGA_LEGACY_WIDTH;
int vga_buf_height = VGA_LEGACY_HEIGHT;

//-------- Begin of function vga_init_layout ----------//
//
// Decide the render buffer size and where the HUD chrome sits in it.
//
// Legacy mode reproduces the 1997 layout exactly: an 800x600 buffer with the
// map viewport at 0,56 - 575,599.
//
// Wide mode renders into a buffer the size of the window, so sprites stay at
// native scale and the viewport simply covers more tiles. The chrome is
// docked rather than stretched: the top strip stays at the top left where its
// baked-in buttons are, and the sidebar (minimap + info panel) moves to the
// right edge. The map viewport is everything left between them, rounded DOWN
// to a whole number of 32px tiles -- Matrix::init_var() derives disp_x_loc by
// truncating image_width/loc_width, so a viewport that is not a tile multiple
// would leave its last column unpainted. The <32px remainder is left to the
// chrome, which fills it along with the rest of the gap.
//
// <int> winWidth, winHeight = the window size to fill in wide mode
//
static void vga_init_layout(int winWidth, int winHeight)
{
   int w = VGA_LEGACY_WIDTH;
   int h = VGA_LEGACY_HEIGHT;

   if( config_adv.vga_wide_viewport )
   {
      // Never go below the legacy size -- the HUD chrome is a fixed 800x600
      // bitmap and has nowhere to shrink to.
      w = MAX(winWidth,  VGA_LEGACY_WIDTH);
      h = MAX(winHeight, VGA_LEGACY_HEIGHT);
      w = MIN(w, VGA_MAX_WIDTH);
      h = MIN(h, VGA_MAX_HEIGHT);
   }

   vga_buf_width  = w;
   vga_buf_height = h;

   // Sidebar keeps its 224px width and its internal y layout; it just moves
   // right so its right edge lands on the buffer's right edge.
   hud_sidebar_x_offset = w - VGA_LEGACY_WIDTH;

   zoom_win_x1 = ZOOM_LEGACY_X1;
   zoom_win_y1 = ZOOM_LEGACY_Y1;

   int availWidth  = (ZOOM_LEGACY_X2 - ZOOM_LEGACY_X1 + 1) + hud_sidebar_x_offset;
   int availHeight = h - ZOOM_LEGACY_Y1;

   zoom_win_width  = (availWidth  / ZOOM_LOC_WIDTH ) * ZOOM_LOC_WIDTH;
   zoom_win_height = (availHeight / ZOOM_LOC_HEIGHT) * ZOOM_LOC_HEIGHT;

   zoom_win_x2 = zoom_win_x1 + zoom_win_width  - 1;
   zoom_win_y2 = zoom_win_y1 + zoom_win_height - 1;
}
//-------- End of function vga_init_layout ----------//

//-------- Begin of function vga_is_wide_viewport ----------//
//
// Whether the buffer is larger than the legacy 800x600, i.e. whether the HUD
// chrome has to be docked and its gaps filled rather than blitted whole.
//
int vga_is_wide_viewport()
{
   return vga_buf_width > VGA_LEGACY_WIDTH || vga_buf_height > VGA_LEGACY_HEIGHT;
}
//-------- End of function vga_is_wide_viewport ----------//

//------ Define static class member vars ---------//

char    Vga::use_back_buf = 0;
char    Vga::opaque_flag  = 0;
VgaBuf* Vga::active_buf   = &vga_front;      // default: front buffer

//-------- Begin of function Vga::Vga ----------//

Vga::Vga()
{
   memset(game_pal, 0, sizeof(SDL_Color)*VGA_PALETTE_SIZE);
   custom_pal = NULL;
   vga_color_table = NULL;

   target = NULL;
   texture = NULL;
   renderer = NULL;
   window = NULL;
   vsync_active = 0;
   present_interval_ms = 1000 / PRESENT_FALLBACK_HZ;
   screenshot_frames_left = 0;
   present_legacy = 0;
}
//-------- End of function Vga::Vga ----------//


//-------- Begin of function Vga::~Vga ----------//

Vga::~Vga()
{
   deinit();
}
//-------- End of function Vga::~Vga ----------//


//-------- Begin of function Vga::init ----------//

int Vga::init()
{
   SDL_Surface *icon;

#ifdef USE_WINDOWS
   init_dpi();
#endif

   win_grab_forced = 0;
   win_grab_user_mode = 0;
   mouse_mode = MOUSE_INPUT_ABS;
   boundary_set = 0;

   if (SDL_Init(SDL_INIT_VIDEO))
      return 0;

   init_window_size();

   // Must run before anything below allocates from VGA_WIDTH/VGA_HEIGHT.
   vga_init_layout(config_adv.vga_window_width, config_adv.vga_window_height);

   // Number of presented frames to let go by before the screenshot dump, so
   // the screen being captured has actually finished drawing.
   screenshot_frames_left = cmd_line.screenshot_path ? 16 : 0;

   // Save the mouse position to restore after mode change. If we don't do
   // this, then the old position gets recalculated, with the mode change
   // affecting the location, causing a jump.
   int mouse_x, mouse_y;
   SDL_GetGlobalMouseState(&mouse_x, &mouse_y);

   window = SDL_CreateWindow(WIN_TITLE,
                             SDL_WINDOWPOS_UNDEFINED,
                             SDL_WINDOWPOS_UNDEFINED,
                             config_adv.vga_window_width,
                             config_adv.vga_window_height,
                             init_window_flags());
   if( !window )
      return 0;

   if( config_adv.vga_full_screen )
      set_window_grab(WINGRAB_ON);

   // Request vsync when configured. SDL2 may refuse it outright (renderer
   // creation fails) or accept the call but not actually grant the flag,
   // depending on the driver -- handle both: retry without the flag if
   // creation failed, then read back what was really granted.
   renderer = SDL_CreateRenderer(window, -1,
                                 config_adv.vga_vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
   if( !renderer && config_adv.vga_vsync )
      renderer = SDL_CreateRenderer(window, -1, 0);
   if( !renderer )
      return 0;

   vsync_active = is_vsync_granted();
   update_present_interval();

   SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, ConfigAdv::vga_scale_quality_name(config_adv.vga_scale_quality));
   SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1");
   if( config_adv.vga_keep_aspect_ratio )
      SDL_RenderSetLogicalSize(renderer, VGA_WIDTH, VGA_HEIGHT);

   SDL_WarpMouseGlobal(mouse_x, mouse_y); // warp to initialize mouse by event queue

   Uint32 window_pixel_format = SDL_GetWindowPixelFormat(window);
   if (window_pixel_format == SDL_PIXELFORMAT_UNKNOWN)
   {
      ERR("Unknown pixel format: %s\n", SDL_GetError());
      return 0;
   }

   // Cannot use SDL_PIXELFORMAT_INDEX8:
   //   Palettized textures are not supported
   texture = SDL_CreateTexture(renderer,
                               window_pixel_format,
                               SDL_TEXTUREACCESS_STREAMING,
                               VGA_WIDTH,
                               VGA_HEIGHT);
   if (!texture)
   {
      ERR("Could not create texture: %s\n", SDL_GetError());
      return 0;
   }

   int desktop_bpp = 0;
   if (SDL_PIXELTYPE(window_pixel_format) == SDL_PIXELTYPE_PACKED32)
   {
      desktop_bpp = 32;
   }
   else if (SDL_PIXELTYPE(window_pixel_format) == SDL_PIXELTYPE_PACKED16)
   {
      desktop_bpp = 16;
   }
   else if (SDL_PIXELTYPE(window_pixel_format) == SDL_PIXELTYPE_PACKED8)
   {
      desktop_bpp = 8;
   }
   else
   {
      ERR("Unsupported pixel type\n");
      return 0;
   }

   target = SDL_CreateRGBSurface(0,
                                 VGA_WIDTH,
                                 VGA_HEIGHT,
                                 desktop_bpp,
                                 0, 0, 0, 0);
   if (!target)
   {
      return 0;
   }

   FilePath icon_path(sys.dir_image);
   icon_path += "7K_ICON.BMP";
   icon = SDL_LoadBMP(icon_path);
   if (icon)
   {
      Uint32 colorkey;
      colorkey = SDL_MapRGB(icon->format, 0, 0, 0);
      SDL_SetColorKey(icon, SDL_TRUE, colorkey);
      SDL_SetWindowIcon(window, icon);
      SDL_FreeSurface(icon);
   }

   return 1;
}
//-------- End of function Vga::init ----------//


//-------- Begin of function Vga::set_scale_quality ----------//
//
// Change the render scale-quality filter (nearest/linear/best) live, with
// no app restart. SDL_HINT_RENDER_SCALE_QUALITY is read only at texture
// creation time, so this destroys and recreates the streaming texture
// (not the target surface, whose format/size don't depend on scale
// quality) with the new hint applied.
//
void Vga::set_scale_quality(char mode)
{
   if( !renderer || !texture )
      return;

   Uint32 window_pixel_format = SDL_GetWindowPixelFormat(window);
   if( window_pixel_format == SDL_PIXELFORMAT_UNKNOWN )
      return;

   SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, ConfigAdv::vga_scale_quality_name(mode));

   SDL_Texture *newTexture = SDL_CreateTexture(renderer,
                               window_pixel_format,
                               SDL_TEXTUREACCESS_STREAMING,
                               VGA_WIDTH,
                               VGA_HEIGHT);
   if( !newTexture )
   {
      ERR("Could not recreate texture with new scale quality: %s\n", SDL_GetError());
      return;
   }

   SDL_DestroyTexture(texture);
   texture = newTexture;
   config_adv.vga_scale_quality = mode;

   // next flip() re-uploads target->pixels into the new texture unconditionally
}
//-------- End of function Vga::set_scale_quality ----------//


//-------- Begin of function Vga::is_vsync_granted ----------//
//
// Whether the renderer actually presents on vblank right now, as opposed to
// what was merely asked for. Same capability check save_status_report() has
// always used to report vsync state.
//
char Vga::is_vsync_granted()
{
   SDL_RendererInfo info;

   if( !renderer || SDL_GetRendererInfo(renderer, &info) != 0 )
      return 0;

   return (info.flags & SDL_RENDERER_PRESENTVSYNC) ? 1 : 0;
}
//-------- End of function Vga::is_vsync_granted ----------//


//-------- Begin of function Vga::set_vsync ----------//
//
// Toggle vsync live. SDL_RenderSetVSync() exists only from SDL 2.0.18; on
// older SDL the new setting is only stored, taking effect on the next run,
// since the flag can otherwise be chosen only at renderer-creation time.
//
// Returns 1 if vsync is in the requested state now, 0 if the change needs a
// restart or the driver refused it. The setting is stored either way -- an
// unsupported request must not break rendering, and it should still survive
// to a machine/driver that can honor it.
//
int Vga::set_vsync(char enable)
{
   config_adv.vga_vsync = enable ? 1 : 0;

   if( !renderer )
      return 0;

#if SDL_VERSION_ATLEAST(2, 0, 18)
   // Deliberately NOT is_vsync_granted() here: SDL_GetRendererInfo()'s
   // PRESENTVSYNC flag is a snapshot of the creation-time flags and is not
   // refreshed by SDL_RenderSetVSync() (verified against SDL 2.32 -- the
   // flag still reads "on" after a successful SDL_RenderSetVSync(r, 0)).
   // The call's own return code is the only trustworthy answer at runtime.
   if( SDL_RenderSetVSync(renderer, config_adv.vga_vsync) == 0 )
   {
      vsync_active = config_adv.vga_vsync;
      return 1;
   }
   ERR("Could not change vsync: %s\n", SDL_GetError());
#endif

   // No runtime support, or the driver refused: leave vsync_active reporting
   // what creation time actually granted. The stored setting still applies
   // on the next startup.
   vsync_active = is_vsync_granted();
   return 0;
}
//-------- End of function Vga::set_vsync ----------//


//-------- Begin of function Vga::deinit ----------//

void Vga::deinit()
{
   SDL_SetRelativeMouseMode(SDL_FALSE);
   mouse_mode = MOUSE_INPUT_ABS;

   vga_back.deinit();
   if (sys.debug_session)
      vga_true_front.deinit();
   vga_front.deinit();

   if (vga_color_table)
      delete vga_color_table;
   vga_color_table = NULL;
   if( custom_pal )
      mem_del(custom_pal);
   custom_pal = NULL;

   if( target )
      SDL_FreeSurface(target);
   target = NULL;
   if( texture )
      SDL_DestroyTexture(texture);
   texture = NULL;
   if( renderer )
      SDL_DestroyRenderer(renderer);
   renderer = NULL;
   if( window )
      SDL_DestroyWindow(window);
   window = NULL;

   SDL_Quit();
}
//-------- End of function Vga::deinit ----------//


//--------- Start of function Vga::load_pal ----------//
//
// Loads the default game palette specified by fileName. Creates the ddraw
// palette.
//
int Vga::load_pal(const char* fileName)
{
   char palBuf[VGA_PALETTE_SIZE][3];
   File palFile;

   palFile.file_open(fileName);
   palFile.file_seek(8);               // bypass the header info
   palFile.file_read(palBuf, VGA_PALETTE_SIZE*3);
   palFile.file_close();

   for (int i = 0; i < VGA_PALETTE_SIZE; i++)
   {
      game_pal[i].r = palBuf[i][0];
      game_pal[i].g = palBuf[i][1];
      game_pal[i].b = palBuf[i][2];
   }

   //----- initialize interface color table -----//

   PalDesc palDesc( (unsigned char*) game_pal, sizeof(SDL_Color), VGA_PALETTE_SIZE, 8);
   vga_color_table = new ColorTable;
   vga_color_table->generate_table( MAX_BRIGHTNESS_ADJUST_DEGREE, palDesc, ColorTable::bright_func );

   return 1;
}
//----------- End of function Vga::load_pal ----------//


//-------- Begin of function Vga::activate_pal ----------//
//
// we are getting the palette focus, select our palette
//
void Vga::activate_pal(VgaBuf* vgaBufPtr)
{
   vgaBufPtr->activate_pal(game_pal);
}
//--------- End of function Vga::activate_pal ----------//


//-------- Begin of function Vga::set_custom_palette ----------//
//
// Read the custom palette specified by fileName and set to display.
//
int Vga::set_custom_palette(char *fileName)
{
   if (!custom_pal)
      custom_pal = (SDL_Color*)mem_add(sizeof(SDL_Color)*VGA_PALETTE_SIZE);

   char palBuf[VGA_PALETTE_SIZE][3];
   File palFile;

   palFile.file_open(fileName);
   palFile.file_seek(8);     				// bypass the header info
   palFile.file_read(palBuf, VGA_PALETTE_SIZE*3);
   palFile.file_close();

   for(int i=0; i<VGA_PALETTE_SIZE; i++)
   {
      custom_pal[i].r = palBuf[i][0];
      custom_pal[i].g = palBuf[i][1];
      custom_pal[i].b = palBuf[i][2];
   }

   vga_front.activate_pal(custom_pal);

   return 1;
}
//--------- End of function Vga::set_custom_palette ----------//


//--------- Begin of function Vga::free_custom_palette ----------//
//
// Frees the custom palette and restores the game palette.
//
void Vga::free_custom_palette()
{
   if (custom_pal)
   {
      mem_del(custom_pal);
      custom_pal = NULL; 
   }
   vga_front.activate_pal(game_pal);
}
//--------- End of function Vga::free_custom_palette ----------//


//-------- Begin of function Vga::adjust_brightness ----------//
//
// <int> changeValue - the value to add to the RGB values of
//                     all the colors in the palette.
//                     the value can be from -255 to 255.
//
// <int> preserveContrast - whether preserve the constrast or not
//
void Vga::adjust_brightness(int changeValue)
{
   //---- find out the maximum rgb value can change without affecting the contrast ---//

   int          i;
   int          newRed, newGreen, newBlue;
   SDL_Color palBuf[VGA_PALETTE_SIZE];

   //------------ change palette now -------------//

   for( i=0 ; i<VGA_PALETTE_SIZE ; i++ )
   {
      newRed   = (int)game_pal[i].r + changeValue;
      newGreen = (int)game_pal[i].g + changeValue;
      newBlue  = (int)game_pal[i].b + changeValue;

      palBuf[i].r = MIN(255, MAX(newRed,0));
      palBuf[i].g = MIN(255, MAX(newGreen,0));
      palBuf[i].b = MIN(255, MAX(newBlue,0));
   }

   //------------ set palette ------------//

   vga_front.temp_unlock();

   vga_front.activate_pal(palBuf);

   vga_front.temp_restore_lock();
}
//--------- End of function Vga::adjust_brightness ----------//


//-------- Begin of function Vga::handle_messages --------//
void Vga::handle_messages()
{
   SDL_Event event;

   while( SDL_PollEvent(&event) )
   {
      switch (event.type)
      {
      case SDL_QUIT:
         sys.signal_exit_flag = 1;
         break;
      case SDL_MULTIGESTURE:
         if (event.mgesture.numFingers == 2) {
            mouse.process_scroll(event.mgesture.x, event.mgesture.y);
         }
         break;
      case SDL_FINGERDOWN:
         mouse.end_scroll();
         break;
      case SDL_MOUSEWHEEL:
          mouse.process_scroll(event.wheel.x, event.wheel.y * -1);
          break;
      case SDL_WINDOWEVENT:
         switch (event.window.event)
         {
            case SDL_WINDOWEVENT_EXPOSED:
            case SDL_WINDOWEVENT_RESIZED:
               sys.need_redraw_flag = 1;
               update_mouse_pos();
               boundary_set = 0;
               break;

            //case SDL_WINDOWEVENT_ENTER: // Do not respond to mouse focus
            case SDL_WINDOWEVENT_FOCUS_GAINED:
            case SDL_WINDOWEVENT_RESTORED:
               sys.need_redraw_flag = 1;
               if( !sys.is_mp_game && config_adv.vga_pause_on_focus_loss )
                  sys.unpause();

               // update ctrl/shift/alt key state
               mouse.update_skey_state();
               SDL_ShowCursor(SDL_DISABLE);
               break;

            //case SDL_WINDOWEVENT_LEAVE: // Do not respond to mouse focus
            case SDL_WINDOWEVENT_FOCUS_LOST:
            case SDL_WINDOWEVENT_MINIMIZED:
               if( !sys.is_mp_game && config_adv.vga_pause_on_focus_loss )
                  sys.pause();
               // turn the system cursor back on to get around a fullscreen
               // mouse grabbed problem on windows
               SDL_ShowCursor(SDL_ENABLE);
               break;
         }
         break;

      case SDL_MOUSEMOTION:
         if( mouse_mode == MOUSE_INPUT_ABS )
         {
            int logical_x, logical_y;
            if( config_adv.vga_keep_aspect_ratio )
            {
               logical_x = event.motion.x;
               logical_y = event.motion.y;
            }
            else
            {
               float xscale, yscale;
               get_window_scale(&xscale, &yscale);
               logical_x = ((float)event.motion.x / xscale);
               logical_y = ((float)event.motion.y / yscale);
            }
            if( win_grab_user_mode || win_grab_forced )
            {
               int real_x, real_y, do_warp;
               SDL_GetMouseState(&real_x, &real_y);
               do_warp = 0;
               if( !boundary_set )
                  update_boundary();
               if( real_x < bound_x1 )
               {
                  do_warp = 1;
                  real_x = bound_x1;
                  logical_x = mouse.bound_x1;
               }
               else if( real_x > bound_x2 )
               {
                  do_warp = 1;
                  real_x = bound_x2;
                  logical_x = mouse.bound_x2;
               }
               if( real_y < bound_y1 )
               {
                  do_warp = 1;
                  real_y = bound_y1;
                  logical_y = mouse.bound_y1;
               }
               else if( real_y > bound_y2 )
               {
                  do_warp = 1;
                  real_y = bound_y2;
                  logical_y = mouse.bound_y2;
               }
               if( do_warp )
               {
                  SDL_WarpMouseInWindow(window, real_x, real_y);
               }
            }
            mouse.process_mouse_motion(logical_x, logical_y);
         }
         else
         {
            mouse.process_mouse_motion(event.motion.xrel, event.motion.yrel);
         }
         break;
      case SDL_MOUSEBUTTONDOWN:
         if( event.button.button == SDL_BUTTON_LEFT )
         {
            mouse.add_event(LEFT_BUTTON);
         }
         else if( event.button.button == SDL_BUTTON_RIGHT )
         {
            mouse.add_event(RIGHT_BUTTON);
         }
         set_window_grab(WINGRAB_FORCE);
         break;
      case SDL_MOUSEBUTTONUP:
         if( event.button.button == SDL_BUTTON_LEFT )
         {
            mouse.add_event(LEFT_BUTTON_RELEASE);
            mouse.reset_boundary();
         }
         else if( event.button.button == SDL_BUTTON_RIGHT )
         {
            mouse.add_event(RIGHT_BUTTON_RELEASE);
         }
         set_window_grab(WINGRAB_RESTORE);
         break;
      case SDL_KEYDOWN:
      {
         int bypass = 0;
         int mod = event.key.keysym.mod &
            (KMOD_CTRL|KMOD_SHIFT|KMOD_ALT);
         if( mod == KMOD_LALT || mod == KMOD_RALT )
         {
            if( event.key.keysym.sym == SDLK_RETURN )
            {
               bypass = 1;
               sys.toggle_full_screen_flag = 1;
            }
            else if( event.key.keysym.sym == SDLK_F4 )
            {
               bypass = 1;
               sys.signal_exit_flag = 1;
            }
            else if( event.key.keysym.sym == SDLK_TAB )
            {
               bypass = 1;
               SDL_Window *window = SDL_GetWindowFromID(event.key.windowID);
               SDL_MinimizeWindow(window);
            }
         }
         else if( mod == KMOD_LCTRL || mod == KMOD_RCTRL )
         {
            if( event.key.keysym.sym == SDLK_g )
            {
               bypass = 1;
               set_window_grab(WINGRAB_TOGGLE);
            }
            else if( event.key.keysym.sym == SDLK_m )
            {
               bypass = 1;
               if( mouse_mode == MOUSE_INPUT_ABS && !is_input_grabbed() )
                  set_mouse_mode( MOUSE_INPUT_REL_WARP );
               else if( mouse_mode != MOUSE_INPUT_ABS )
                  set_mouse_mode( MOUSE_INPUT_ABS );
            }
         }
         if( SDL_IsTextInputActive() && event.key.keysym.sym >= SDLK_SPACE && event.key.keysym.sym <= SDLK_z )
		bypass = 1;
         if( !bypass )
         {
            mouse.update_skey_state();
            mouse.add_key_event(event.key.keysym.sym, misc.get_time());
         }
         break;
      }
      case SDL_KEYUP:
         mouse.update_skey_state();
         break;
      case SDL_TEXTINPUT:
         mouse.add_typing_event(event.text.text, misc.get_time());
         break;
      case SDL_RENDER_TARGETS_RESET:
         sys.need_redraw_flag = 1;
         break;
      case SDL_TEXTEDITING:
      case SDL_JOYAXISMOTION:
      case SDL_JOYBALLMOTION:
      case SDL_JOYHATMOTION:
      case SDL_JOYBUTTONDOWN:
      case SDL_JOYBUTTONUP:
      default:
         MSG("unhandled event %x\n", event.type);
         break;
      }
   }
}
//-------- End of function Vga::handle_messages --------//

//-------- Begin of function Vga::flag_redraw --------//
void Vga::flag_redraw()
{
}
//-------- End of function Vga::flag_redraw ----------//


//-------- Begin of function Vga::is_full_screen --------//
//
int Vga::is_full_screen()
{
   return ((SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0);
}
//-------- End of function Vga::is_full_screen ----------//


//-------- Begin of function Vga::is_input_grabbed --------//
//
int Vga::is_input_grabbed()
{
   return ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_GRABBED) != 0);
}
//-------- End of function Vga::is_input_grabbed ----------//


//-------- Begin of function Vga::update_boundary --------//
// Uses logical boundary coordinates and scales them to the actual boundary in
// the scaled window.
void Vga::update_boundary()
{
   float xscale, yscale;
   SDL_Rect rect;
   get_window_scale(&xscale, &yscale);
   SDL_RenderGetViewport(renderer, &rect);
   bound_x1 = ((float)(mouse.bound_x1 + rect.x) * xscale);
   bound_x2 = ((float)(mouse.bound_x2 + rect.x) * xscale);
   bound_y1 = ((float)(mouse.bound_y1 + rect.y) * yscale);
   bound_y2 = ((float)(mouse.bound_y2 + rect.y) * yscale);
   boundary_set = 1;
}
//-------- End of function Vga::update_boundary --------//


//-------- Begin of function Vga::update_mouse_pos ----------//
// Updates logical mouse position according to actual mouse state.
void Vga::update_mouse_pos()
{
   float xscale, yscale;
   SDL_Rect rect;
   int logical_x, logical_y, win_x, win_y, mouse_x, mouse_y;

   if( !window )
      return;

   get_window_scale(&xscale, &yscale);
   SDL_RenderGetViewport(renderer, &rect);
   SDL_GetWindowPosition(window, &win_x, &win_y);
   SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
   logical_x = ((float)(mouse_x - win_x) / xscale - rect.x);
   logical_y = ((float)(mouse_y - win_y) / yscale - rect.y);

   mouse.process_mouse_motion(logical_x, logical_y);
}
//---------- End of function Vga::update_mouse_pos ----------//


//-------- Begin of function Vga::set_full_screen_mode --------//
//
// mode -1: toggle
// mode  0: windowed
// mode  1: full screen without display mode change (stretched to desktop)
void Vga::set_full_screen_mode(int mode)
{
   int result, mouse_x, mouse_y;
   uint32_t flags = config_adv.vga_full_screen_desktop ?
      SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;

   switch (mode)
   {
      case -1:
         if( is_full_screen() )
            flags = 0;
         break;
      case 0:
         flags = 0;
         break;
      case 1:
         break;
      default:
         err_now("invalid mode");
   }

   // Save the mouse position to restore after mode change. If we don't do
   // this, then the old position gets recalculated, with the mode change
   // affecting the location, causing a jump.
   SDL_GetGlobalMouseState(&mouse_x, &mouse_y);

   result = SDL_SetWindowFullscreen(window, flags);
   if (result < 0) {
      ERR("Could not toggle fullscreen: %s\n", SDL_GetError());
      return;
   }

   sys.need_redraw_flag = 1;
   boundary_set = 0;
   if( flags ) // went full screen
      set_window_grab(WINGRAB_ON);
   else
      set_window_grab(WINGRAB_OFF);

   SDL_WarpMouseGlobal(mouse_x, mouse_y);
}
//-------- End of function Vga::set_full_screen_mode ----------//


//-------- Begin of function Vga::set_mouse_mode --------//
void Vga::set_mouse_mode(MouseInputMode mode)
{
   switch( mode )
   {
   case MOUSE_INPUT_REL:
      if( mouse_mode == MOUSE_INPUT_ABS )
         SDL_SetRelativeMouseMode(SDL_TRUE);
      SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
      mouse_mode = MOUSE_INPUT_REL;
      break;
   case MOUSE_INPUT_REL_WARP:
      if( mouse_mode == MOUSE_INPUT_ABS )
         SDL_SetRelativeMouseMode(SDL_TRUE);
      SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1");
      mouse_mode = MOUSE_INPUT_REL_WARP;
      break;
   default:
      // absolute mode
      if( mouse_mode != MOUSE_INPUT_ABS )
         SDL_SetRelativeMouseMode(SDL_FALSE);
      mouse_mode = MOUSE_INPUT_ABS;
   }
}
//-------- End of function Vga::set_mouse_mode --------//


//-------- Begin of function Vga::set_window_grab --------//
//
// WINGRAB_OFF = Turn window grab off, except when forced on
// WINGRAB_ON = Turn window grab on
// WINGRAB_TOGGLE = Toggle window grab, used for the grab key
// WINGRAB_FORCE = Force grab on, even if user has it off
// WINGRAB_RESTORE = Disable forcing, and user's option
void Vga::set_window_grab(WinGrab mode)
{
   switch( mode )
   {
   case WINGRAB_OFF:
      if( win_grab_user_mode )
      {
         win_grab_user_mode = 0;
         if( !win_grab_forced )
         {
            SDL_SetWindowGrab(window, SDL_FALSE);
            if( mouse_mode != MOUSE_INPUT_ABS )
               set_mouse_mode(MOUSE_INPUT_ABS);
         }
      }
      break;
   case WINGRAB_ON:
      if( !win_grab_user_mode )
      {
         win_grab_user_mode = 1;
         if( !win_grab_forced )
         {
            SDL_SetWindowGrab(window, SDL_TRUE);
         }
      }
      break;
   case WINGRAB_TOGGLE:
      if( win_grab_user_mode )
      {
         win_grab_user_mode = 0;
         if( !win_grab_forced )
         {
            SDL_SetWindowGrab(window, SDL_FALSE);
            if( mouse_mode != MOUSE_INPUT_ABS )
               set_mouse_mode(MOUSE_INPUT_ABS);
         }
      }
      else
      {
         win_grab_user_mode = 1;
         if( !win_grab_forced )
         {
            SDL_SetWindowGrab(window, SDL_TRUE);
         }
      }
      break;
   case WINGRAB_FORCE:
      if( !win_grab_forced )
      {
         win_grab_forced = 1;
         if( !win_grab_user_mode )
         {
            SDL_SetWindowGrab(window, SDL_TRUE);
         }
      }
      break;
   case WINGRAB_RESTORE:
      if( win_grab_forced )
      {
         win_grab_forced = 0;
         if( !win_grab_user_mode )
         {
            SDL_SetWindowGrab(window, SDL_FALSE);
            if( mouse_mode != MOUSE_INPUT_ABS )
               set_mouse_mode(MOUSE_INPUT_ABS);
         }
      }
      break;
   default:
      err_now("invalid mode");
   }
}
//-------- End of function Vga::set_window_grab ----------//


//-------- Beginning of function Vga::update_present_interval ----------//
//
// Derive flip()'s presentation throttle from the display the window is
// actually on. Replaces a hardcoded ~17ms (~58.8fps) gate that matched no
// real display's refresh interval.
//
// Applied whether or not vsync is on, deliberately. The obvious alternative
// -- drop the throttle entirely and let SDL_RenderPresent() block on vblank
// -- assumes a granted PRESENTVSYNC actually blocks, and that does not hold:
// measured on the reference machine (SDL 2.32, Wayland, 165Hz), a renderer
// reporting vsync ON ran 230972 presents in 5s (~46000fps) through a
// *visible* window, and 13.8M through a hidden one. A vsync that silently
// declines to block would turn an unthrottled flip() into a spin.
//
// Because the interval is truncated below the true one it can never throttle
// under the display's refresh rate, so where vsync does block it stays in
// charge and this is an inert backstop.
//
void Vga::update_present_interval()
{
   SDL_DisplayMode mode;
   int display_index;

   display_index = window ? SDL_GetWindowDisplayIndex(window) : -1;

   if( display_index >= 0 &&
       SDL_GetCurrentDisplayMode(display_index, &mode) == 0 &&
       mode.refresh_rate > 0 )
   {
      // Integer division truncates, so the interval is never longer than
      // the real one -- presentation can end up marginally ahead of the
      // display, never throttled below it (e.g. 165Hz -> 6ms -> 166fps,
      // 75Hz -> 13ms, 60Hz -> 16ms).
      present_interval_ms = 1000 / mode.refresh_rate;
   }
   else
   {
      present_interval_ms = 1000 / PRESENT_FALLBACK_HZ;
   }
}
//-------- End of function Vga::update_present_interval ----------//


//-------- Beginning of function Vga::flip ----------//
void Vga::flip()
{
   static Uint32 ticks = 0;
   static Uint32 recheck_ticks = 0;
   Uint32 cur_ticks;

   if( !is_inited() )
      return;

   cur_ticks = SDL_GetTicks();

   // Unsigned subtraction throughout, so the ~49-day SDL_GetTicks() wrap
   // reads as one large delta (present now) instead of needing the special
   // case the old gate carried.

   if( cur_ticks - recheck_ticks >= PRESENT_INTERVAL_RECHECK_MS )
   {
      recheck_ticks = cur_ticks;
      update_present_interval();
   }

   // 0 only for an implausibly fast display (>1000Hz), where there is
   // nothing left to throttle.
   if( present_interval_ms && cur_ticks - ticks < (Uint32)present_interval_ms )
      return;

   ticks = cur_ticks;
   SDL_BlitSurface(vga_front.surface, NULL, target, NULL);
   SDL_UpdateTexture(texture, NULL, target->pixels, target->pitch);
   SDL_RenderClear(renderer);

   if( present_legacy )
   {
      SDL_Rect srcRect = { 0, 0, VGA_LEGACY_WIDTH, VGA_LEGACY_HEIGHT };
      SDL_RenderCopy(renderer, texture, &srcRect, NULL);
   }
   else
   {
      SDL_RenderCopy(renderer, texture, NULL, NULL);
   }

   SDL_RenderPresent(renderer);

   // -headless-screenshot: let a few frames present so whichever screen is up
   // is fully drawn, then dump it and quit. Hooked here rather than in a
   // specific loop so it captures menus, which run their own loops, as well
   // as gameplay.
   if( screenshot_frames_left > 0 && --screenshot_frames_left == 0 )
   {
      printf("SCREENSHOT=%s %dx%d %s\n",
             cmd_line.screenshot_path, present_width(), present_height(),
             save_screenshot(cmd_line.screenshot_path) ? "ok" : "FAILED");
      fflush(stdout);
      sys.signal_exit_flag = 1;
   }
}
//-------- End of function Vga::flip ----------//


//-------- Begin of function Vga::set_legacy_present ----------//
//
// Choose whether flip() presents the whole buffer or just the legacy corner.
//
// The main menu, the option screens, the encyclopedia and the report screens
// were all laid out against a fixed 800x600 and hit-test their widgets
// against those same coordinates, so they cannot simply be moved: drawing
// them anywhere but the buffer's top-left corner would put the pixels and
// the click targets out of step. Instead they keep drawing where they always
// did and only that corner is presented, scaled to fill the window -- which
// is exactly what the game did at every resolution before the wide viewport
// existed. Gameplay presents the whole buffer and so stays at native scale.
//
// Driven from the two points every screen passes through:
// spread_full_screen_image() (OIMGRES.cpp) turns it on, and Sys::disp_frame()
// turns it off on every gameplay frame -- deliberately every frame rather
// than only on a redraw, so no path back from a menu can leave it stuck on.
//
// A no-op in legacy mode, where the two regions are the same size.
//
// <int> on = 1 to present the legacy corner only
//
void Vga::set_legacy_present(int on)
{
   if( !renderer || !vga_is_wide_viewport() )
      return;

   if( present_legacy == (char)(on != 0) )
      return;

   present_legacy = (on != 0);

   if( config_adv.vga_keep_aspect_ratio )
      SDL_RenderSetLogicalSize(renderer, present_width(), present_height());

   update_boundary();
}
//-------- End of function Vga::set_legacy_present ----------//


//-------- Begin of function Vga::save_screenshot ----------//
//
// Write the current front buffer out as a palettised BMP.
//
// Renders through the same path a display would, so it works under
// SDL_VIDEODRIVER=dummy -- which is the point: it lets the HUD layout be
// reviewed at an arbitrary buffer size with no display attached.
//
// <char*> filePath = where to write the .bmp
//
// return : 1 on success, 0 on failure
//
int Vga::save_screenshot(const char* filePath)
{
   if( !vga_front.surface )
      return 0;

   // Dump what is actually presented, not the whole buffer -- while a legacy
   // full-screen screen is up that is just the 800x600 corner.
   int shotWidth  = present_width();
   int shotHeight = present_height();

   SDL_Surface *shot = SDL_CreateRGBSurface(0, shotWidth, shotHeight, VGA_BPP, 0, 0, 0, 0);
   if( !shot )
      return 0;

   SDL_SetPaletteColors(shot->format->palette, game_pal, 0, VGA_PALETTE_SIZE);

   for( int y = 0; y < shotHeight; ++y )
      memcpy( (char*)shot->pixels + shot->pitch*y,
              vga_front.buf_ptr() + vga_front.buf_pitch()*y,
              shotWidth );

   int ok = SDL_SaveBMP(shot, filePath) == 0;
   SDL_FreeSurface(shot);
   return ok;
}
//-------- End of function Vga::save_screenshot ----------//


//-------- Beginning of function Vga::save_status_report ----------//
void Vga::save_status_report()
{
   FilePath path(sys.dir_config);
   FILE *file;
   int num, i;
   const char *s;
   SDL_version ver;

   path += "sdl.txt";
   if( path.error_flag )
      return;

   file = fopen(path, "w");
   if( !file )
      return;

   fprintf(file, "=== Seven Kingdoms " SKVERSION " ===\n");
   s = SDL_GetPlatform();
   fprintf(file, "Platform: %s\n", s);
   if( SDL_BYTEORDER == SDL_BIG_ENDIAN )
      fprintf(file, "Big endian\n");
   else
      fprintf(file, "Little endian\n");
#ifndef HAVE_KNOWN_BUILD
   fprintf(file, "Binary built using unsupported configuration\n");
#endif

   s = SDL_GetCurrentVideoDriver();
   fprintf(file, "Current SDL video driver: %s\n", s);
   SDL_GetVersion(&ver);
   fprintf(file, "SDL version: %d.%d.%d\n", ver.major, ver.minor, ver.patch);
   SDL_VERSION(&ver);
   fprintf(file, "Compiled SDL version: %d.%d.%d\n\n", ver.major, ver.minor, ver.patch);

   fprintf(file, "-- Video drivers --\n");
   num = SDL_GetNumVideoDrivers();
   for( i=0; i<num; i++ )
   {
      s = SDL_GetVideoDriver(i);
      fprintf(file, "%d: %s\n", i, s);
   }
   fprintf(file, "\n");

   if( window )
   {
      int x, y, w, h = 0;
      SDL_RendererInfo info;
      SDL_Renderer *r = SDL_GetRenderer(window);

      fprintf(file, "-- Current window --\n");
      fprintf(file, "Active on display: %d\n", SDL_GetWindowDisplayIndex(window));
      SDL_GetWindowPosition(window, &x, &y);
      SDL_GetWindowSize(window, &w, &h);
      fprintf(file, "Geometry: %dx%d @ (%d, %d)\n", w, h, x, y);
      fprintf(file, "Pixel format: %s\n", SDL_GetPixelFormatName(SDL_GetWindowPixelFormat(window)));
      fprintf(file, "Full screen: %s\n", is_full_screen() ? "yes" : "no");
      fprintf(file, "Input grabbed: %s\n\n", SDL_GetWindowGrab(window) ? "yes" : "no");
      if( r )
      {
         SDL_RendererInfo info;
         float xscale, yscale;
         SDL_Rect rect;

         SDL_GetRendererInfo(r, &info);
         get_window_scale(&xscale, &yscale);
         SDL_RenderGetViewport(renderer, &rect);
         SDL_RenderGetLogicalSize(renderer, &w, &h);

         fprintf(file, "-- Current renderer: %s --\n", info.name);
         fprintf(file, "Viewport: x=%d,y=%d,w=%d,h=%d\n", rect.x, rect.y, rect.w, rect.h);
         fprintf(file, "Scale: xscale=%f,yscale=%f\n", xscale, yscale);
         fprintf(file, "Logical size: w=%d, h=%d\n", w, h);
         fprintf(file, "Capabilities: %s\n", info.flags & SDL_RENDERER_ACCELERATED ? "hardware accelerated" : "software fallback");
         // vsync_active, not info.flags: SDL_GetRendererInfo() reports the
         // flags the renderer was CREATED with and does not refresh
         // PRESENTVSYNC after SDL_RenderSetVSync(), so the flag goes stale
         // as soon as the Options toggle changes vsync at runtime.
         fprintf(file, "V-sync: %s\n", vsync_active ? "on" : "off");
         fprintf(file, "Presentation interval: %dms (display-derived)\n", present_interval_ms);
         fprintf(file, "Rendering to texture supported: %s\n", info.flags & SDL_RENDERER_TARGETTEXTURE ? "yes" : "no");
         if( info.max_texture_width || info.max_texture_height )
            fprintf(file, "Maximum texture size: %dx%d\n", info.max_texture_width, info.max_texture_height);
         fprintf(file, "Pixel formats:\n");
         for( unsigned i=0; i<info.num_texture_formats; i++ )
            fprintf(file, "\t%s\n", SDL_GetPixelFormatName(info.texture_formats[i]));
      }
   }
   fprintf(file, "\n");

   if( texture )
   {
      uint32_t format = 0;
      int w, h = 0;
      SDL_QueryTexture(texture, &format, NULL, &w, &h);
      fprintf(file, "-- Streaming texture --\n");
      fprintf(file, "Size: %dx%d\n", w, h);
      fprintf(file, "Pixel format: %s\n\n", SDL_GetPixelFormatName(format));
   }

   num = SDL_GetNumVideoDisplays();
   for( i=0; i<num; i++ )
   {
      SDL_Rect rect;
      SDL_DisplayMode mode;
      float ddpi, hdpi, vdpi;
      int num_modes, cur_mode, j;
      num_modes = SDL_GetNumDisplayModes(i);
      cur_mode = SDL_GetCurrentDisplayMode(i, NULL);
      fprintf(file, "-- Display %d using mode %d--\n", i, cur_mode);
      for( j=0; j<num_modes; j++ )
      {
          if( !SDL_GetDisplayMode(i, j, &mode) )
              fprintf(file, "Mode %d: %dx%dx%ubpp %dHz format=%s driver=%p\n", j, mode.w, mode.h, SDL_BITSPERPIXEL(mode.format), mode.refresh_rate, SDL_GetPixelFormatName(mode.format), mode.driverdata);
      }
      if( !SDL_GetDisplayDPI(i, &ddpi, &hdpi, &vdpi) )
         fprintf(file, "DPI: diag=%f horiz=%f vert=%f\n", ddpi, hdpi, vdpi);
      if( !SDL_GetDisplayBounds(i, &rect) )
         fprintf(file, "Bounds: x=%d y=%d w=%d h=%d\n", rect.x, rect.y, rect.w, rect.h);
#if SDL_VERSION_ATLEAST(2, 0, 5)
      if( !SDL_GetDisplayUsableBounds(i, &rect) ) // Note: requires SDL 2.0.5+
         fprintf(file, "Usable bounds: x=%d y=%d w=%d h=%d\n", rect.x, rect.y, rect.w, rect.h);
#endif
      fprintf(file, "\n");
   }

   num = SDL_GetNumRenderDrivers();
   for( i=0; i<num; i++ )
   {
      SDL_RendererInfo info;
      SDL_GetRenderDriverInfo(i, &info);
      fprintf(file, "-- Renderer %s (%d) --\n", info.name, i);
      fprintf(file, "Capabilities: %s\n", info.flags & SDL_RENDERER_ACCELERATED ? "hardware accelerated" : "software fallback");
      fprintf(file, "V-sync capable: %s\n", info.flags & SDL_RENDERER_PRESENTVSYNC ? "on" : "off");
      fprintf(file, "Rendering to texture supported: %s\n", info.flags & SDL_RENDERER_TARGETTEXTURE ? "yes" : "no");
      if( info.max_texture_width || info.max_texture_height )
         fprintf(file, "Maximum texture size: %dx%d\n", info.max_texture_width, info.max_texture_height);
      fprintf(file, "Pixel formats:\n");
      for( unsigned j=0; j<info.num_texture_formats; j++ )
         fprintf(file, "\t%s\n", SDL_GetPixelFormatName(info.texture_formats[j]));
      fprintf(file, "\n");
   }

   fclose(file);
   return;
}
//-------- End of function Vga::save_status_report ----------//


//-------- Beginning of function Vga::get_window_scale ----------//
void Vga::get_window_scale(float *xscale, float *yscale)
{
   if( config_adv.vga_keep_aspect_ratio )
   {
      SDL_RenderGetScale(renderer, xscale, yscale);
   }
   else
   {
      int w, h;
      SDL_GetWindowSize(window, &w, &h);
      *xscale = (float)w / (float)present_width();
      *yscale = (float)h / (float)present_height();
   }
}
//-------- End of function Vga::get_window_scale ----------//


#ifdef USE_WINDOWS

#include <windows.h>

typedef enum PROCESS_DPI_AWARENESS {
   PROCESS_DPI_UNAWARE = 0,
   PROCESS_SYSTEM_DPI_AWARE = 1,
   PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;

BOOL(WINAPI *pSetProcessDPIAware)(void); // Vista and later
HRESULT(WINAPI *pSetProcessDpiAwareness)(PROCESS_DPI_AWARENESS dpiAwareness); // Windows 8.1 and later

// Based on the example provided by Eric Wasylishen
// https://discourse.libsdl.org/t/sdl-getdesktopdisplaymode-resolution-reported-in-windows-10-when-using-app-scaling/22389
static void init_dpi()
{
   void* userDLL;
   void* shcoreDLL;

   shcoreDLL = SDL_LoadObject("SHCORE.DLL");
   if (shcoreDLL)
   {
      pSetProcessDpiAwareness = (HRESULT(WINAPI *)(PROCESS_DPI_AWARENESS)) SDL_LoadFunction(shcoreDLL, "SetProcessDpiAwareness");
   }

   if (pSetProcessDpiAwareness)
   {
      /* Try Windows 8.1+ version */
      HRESULT result = pSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
      return;
   }

   userDLL = SDL_LoadObject("USER32.DLL");
   if (userDLL)
   {
      pSetProcessDPIAware = (BOOL(WINAPI *)(void)) SDL_LoadFunction(userDLL, "SetProcessDPIAware");
   }

   if (pSetProcessDPIAware)
   {
      /* Try Vista - Windows 8 version.
      This has a constant scale factor for all monitors.
      */
      BOOL success = pSetProcessDPIAware();
   }
}

#endif

static int init_window_flags()
{
   int flags = 0;
   if( config_adv.vga_full_screen )
      flags |= config_adv.vga_full_screen_desktop ?
         SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
   if( config_adv.vga_allow_highdpi )
      flags |= SDL_WINDOW_ALLOW_HIGHDPI;
   return flags;
}

static void init_window_size()
{
   if( !config_adv.vga_full_screen_desktop && !config_adv.vga_wide_viewport )
   {
      // must match game's native resolution
      config_adv.vga_window_width = 800;
      config_adv.vga_window_height = 600;
      return;
   }

   if( config_adv.vga_window_width && config_adv.vga_window_height )
      return;

   // The wide viewport renders 1:1 into a buffer this size, so "bigger" means
   // more tiles on screen rather than a larger upscale factor: take the whole
   // usable display area instead of the fixed 1024x768 / 800x600 / 640x480
   // ladder below, which only ever existed to pick an upscale target.
   if( config_adv.vga_wide_viewport )
   {
#if SDL_VERSION_ATLEAST(2, 0, 5)
      SDL_Window *probe_win = SDL_CreateWindow(WIN_TITLE, 0, 0, 1, 1, 0);
      if( probe_win )
      {
         int probe_idx = SDL_GetWindowDisplayIndex(probe_win);
         SDL_DestroyWindow(probe_win);

         SDL_Rect probe_rect;
         if( probe_idx >= 0 && SDL_GetDisplayUsableBounds(probe_idx, &probe_rect) == 0
             && probe_rect.w >= VGA_LEGACY_WIDTH && probe_rect.h >= VGA_LEGACY_HEIGHT )
         {
            config_adv.vga_window_width  = probe_rect.w;
            config_adv.vga_window_height = probe_rect.h;
            return;
         }
      }
#endif
      // Display size unreadable, or too small to fit the HUD chrome: fall
      // back to legacy, which always fits.
      config_adv.vga_window_width  = VGA_LEGACY_WIDTH;
      config_adv.vga_window_height = VGA_LEGACY_HEIGHT;
      return;
   }

#if SDL_VERSION_ATLEAST(2, 0, 5)
   int display_idx;
   SDL_Window *size_win = SDL_CreateWindow(WIN_TITLE, 0, 0, 1, 1, 0);
   if( !size_win )
      goto unknown_display;

   display_idx = SDL_GetWindowDisplayIndex(size_win);
   SDL_DestroyWindow(size_win);
   if( display_idx < 0 )
      goto unknown_display;

   SDL_Rect rect;
   if( SDL_GetDisplayUsableBounds(display_idx, &rect)<0 )
      goto unknown_display;

   if( rect.w >= 1024 && rect.h >= 768 )
   {
      config_adv.vga_window_width = 1024;
      config_adv.vga_window_height = 768;
      return;
   }

   if( rect.w >= 800 && rect.h >= 600 )
   {
      config_adv.vga_window_width = 800;
      config_adv.vga_window_height = 600;
      return;
   }

   config_adv.vga_window_width = 640;
   config_adv.vga_window_height = 480;
   return;
#endif

unknown_display:
      config_adv.vga_window_width = 800;
      config_adv.vga_window_height = 600;
      return;
}
