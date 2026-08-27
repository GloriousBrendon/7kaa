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

//Filename    : OIMGES.CPP
//Description : Object ImageRes

#include <string.h>

#include <ALL.h>
#include <OVGA.h>
#include <OMOUSE.h>
#include <OIMGRES.h>


//--------- Format of RES file ------------//
//
// In the resource file, contain many compressed images, each image
// has the following data
//
// <char[8]> = the name of the image
// <int>     = the widht of the image
// <int>     = the height of the image
// <char...> = the bitmap of the image
//
//--------------------------------------------//


//------- Start of function ImageRes::ImageRes -------//
//
// <char*> resName   = name of the resource file (e.g. "GIF.RES")
// [int]   readAll   = whether read all data into the buffer or read one each time
//                     (default:0)
// [int]   useCommonBuf = whether use the sys common buffer to store the data or not
//                     (default:0)
//
ImageRes::ImageRes(char* resFile, int readAll, int useCommonBuf) :
					  ResourceIdx(resFile, readAll, useCommonBuf)
{
}
//--------- End of function ImageRes::ImageRes -------//


//-------- Start of function ImageRes::put_front --------//
//
// int 	x,y       = the location of the image
// char* imageName = name of the image
// [int] compressFlag = compress flag
//								(default: 0)
//
void ImageRes::put_front(int x, int y, const char* imageName, int compressFlag)
{
	char* bitmapPtr = ResourceIdx::read(imageName);

	if(!bitmapPtr)
		return;

	mouse.hide_area( x, y, x+*((short*)bitmapPtr)-1, y+*(((short*)bitmapPtr)+1)-1 );

	if( compressFlag )
		vga_front.put_bitmap_trans_decompress( x, y, bitmapPtr );
	else
		vga_front.put_bitmap_trans( x, y, bitmapPtr );

	mouse.show_area();
}
//---------- End of function ImageRes::put_front --------//


//-------- Start of function ImageRes::put_back --------//
//
// int 	x,y       = the location of the image
// char* imageName = name of the image
// [int] compressFlag = compress flag
//								(default: 0)
//
void ImageRes::put_back(int x, int y, const char* imageName, int compressFlag)
{
	char* bitmapPtr = ResourceIdx::read(imageName);

	if( bitmapPtr )
	{
		if( compressFlag )
			vga_back.put_bitmap_trans_decompress( x, y, bitmapPtr );
		else
			vga_back.put_bitmap_trans( x, y, bitmapPtr );
	}
}
//---------- End of function ImageRes::put_back --------//


//-------- Start of function ImageRes::put_front --------//
//
// int x,y      = the location of the image
// int bitmapId = id. of the bitmap
// [int] compressFlag = compress flag
//								(default: 0)
//
void ImageRes::put_front(int x, int y, int bitmapId, int compressFlag)
{
	char* bitmapPtr = ResourceIdx::get_data(bitmapId);

	if( !bitmapPtr )
		return;

	mouse.hide_area( x, y, x+*((short*)bitmapPtr)-1, y+*(((short*)bitmapPtr)+1)-1 );

	if( compressFlag )
		vga_front.put_bitmap_trans_decompress( x, y, bitmapPtr );
	else
		vga_front.put_bitmap_trans( x, y, bitmapPtr );

	mouse.show_area();
}
//---------- End of function ImageRes::put_front --------//


//-------- Start of function ImageRes::put_back --------//
//
// int ,y       = the location of the image
// int bitmapId = id. of the bitmap
// [int] compressFlag = compress flag
//								(default: 0)
//
void ImageRes::put_back(int x, int y, int bitmapId, int compressFlag)
{
	char* bitmapPtr = ResourceIdx::get_data(bitmapId);

	if( bitmapPtr )
	{
		if( compressFlag )
			vga_back.put_bitmap_trans_decompress( x, y, bitmapPtr );
		else
			vga_back.put_bitmap_trans( x, y, bitmapPtr );
	}
}
//---------- End of function ImageRes::put_back --------//


//-------- Start of function ImageRes::put_join --------//
//
// int 	x,y       = the location of the image
// char* imageName = name of the image
//
void ImageRes::put_join(int x, int y, const char* imageName)
{
	char* bitmapPtr = ResourceIdx::read(imageName);

	if( !bitmapPtr )
		return;

	mouse.hide_area( x, y, x+*((short*)bitmapPtr)-1, y+*(((short*)bitmapPtr)+1)-1 );

	if( bitmapPtr )
		IMGjoinTrans( vga_front.buf_ptr(), vga_front.buf_pitch(), 
			vga_back.buf_ptr(), vga_back.buf_pitch(), x, y, bitmapPtr );

	mouse.show_area();
}
//---------- End of function ImageRes::put_join --------//


//-------- Start of function ImageRes::put_large --------//
//
// When a picture file is > 64K, which cannot be read into a single
// memory buffer.
//
// It will call vga.put_pict() which will continously read the file
// and put to the screen until completion.
//
// <VgaBuf*> vgaBuf 	  = the vga buffer for display
// <int>	    x,y       = the location of the image
// <char*>   imageName = name of the image
//
void ImageRes::put_large(VgaBuf* vgaBuf, int x, int y, char* imageName)
{
	int dataSize;

	vgaBuf->put_large_bitmap( x, y, ResourceIdx::get_file(imageName, dataSize) );
}
//---------- End of function ImageRes::put_large --------//


//-------- Start of function ImageRes::put_large --------//
//
// When a picture file is > 64K, which cannot be read into a single
// memory buffer.
//
// It will call vga.put_pict() which will continously read the file
// and put to the screen until completion.
//
// <VgaBuf*> vgaBuf 	  = the vga buffer for display
// <int>	    x,y       = the location of the image
// <int>		 bitmapId  = id. of the bitmap in the bitmap resource file.
//
void ImageRes::put_large(VgaBuf* vgaBuf, int x, int y, int bitmapId)
{
	int dataSize;

	vgaBuf->put_large_bitmap( x, y, ResourceIdx::get_file(bitmapId, dataSize) );
}
//---------- End of function ImageRes::put_large --------//


//-------- Start of function spread_full_screen_image --------//
//
// Lay a full-screen image out inside a VgaBuf that may be larger than it.
//
// put_to_buf() reads the raw pixels of a full-screen image straight into the
// destination buffer as one flat run. The rows then have to be re-spread to
// the buffer's pitch -- and the source rows are VGA_LEGACY_WIDTH wide, since
// that is the resolution these images were authored at, regardless of how
// large the buffer now is.
//
// The image is anchored top-left rather than centred: every one of these
// screens hit-tests its widgets against hardcoded 800x600 coordinates, so
// moving the pixels without moving the hit rects would put the two out of
// step. Whatever the image does not cover is cleared to black.
//
static void spread_full_screen_image(VgaBuf* vgaBufPtr)
{
	// A full-screen image landing on a *screen* buffer means a legacy 800x600
	// screen is now what the player is looking at, so present the legacy
	// corner only until the next gameplay frame turns it back off. Loading one
	// into a scratch buffer is just asset handling and means no such thing --
	// Info::disp_panel_docked() does exactly that to cut up the HUD chrome.
	if( vgaBufPtr == &vga_back || vgaBufPtr == &vga_front )
		vga.set_legacy_present(1);

	int p = vgaBufPtr->buf_pitch();
	int w = MIN( vgaBufPtr->buf_width(),  VGA_LEGACY_WIDTH  );
	int h = MIN( vgaBufPtr->buf_height(), VGA_LEGACY_HEIGHT );

	if( p > w || vgaBufPtr->buf_width() > VGA_LEGACY_WIDTH )
	{
		char *basePtr = vgaBufPtr->buf_ptr();

		for( int y = h-1; y > 0; --y )         // row 0 is already in place
			memmove( basePtr + p*y, basePtr + VGA_LEGACY_WIDTH*y, w );
	}

	//--- clear whatever the 800x600 image does not reach ---//

	for( int y = 0; y < h; ++y )
	{
		if( vgaBufPtr->buf_width() > w )
			memset( vgaBufPtr->buf_ptr() + p*y + w, 0, vgaBufPtr->buf_width() - w );
	}

	for( int y = h; y < vgaBufPtr->buf_height(); ++y )
		memset( vgaBufPtr->buf_ptr() + p*y, 0, vgaBufPtr->buf_width() );
}
//---------- End of function spread_full_screen_image --------//


//-------- Start of function ImageRes::put_to_buf --------//
//
// Put the image to the specified Vga buffer. 
//
// <VgaBuf*> vgaBufPtr = the pointer to the Vga buffer
// <char*>	 imageName = name of the image
//
void ImageRes::put_to_buf(VgaBuf* vgaBufPtr, const char* imageName)
{
	set_user_buf( vgaBufPtr->buf_ptr(), vgaBufPtr->buf_size(), 4 );	// 4-by pass the width and height info of the source data, only read the bitmap into the buffer
	read(imageName);
	reset_user_buf();

	spread_full_screen_image( vgaBufPtr );
}
//---------- End of function ImageRes::put_to_buf --------//


//-------- Start of function ImageRes::put_to_buf --------//
//
// Put the image to the specified Vga buffer.
//
// <VgaBuf*> vgaBufPtr = the pointer to the Vga buffer
// <int>     bitmapId  = id. of the bitmap in the resource file.
//
void ImageRes::put_to_buf(VgaBuf* vgaBufPtr, int bitmapId)
{
	set_user_buf( vgaBufPtr->buf_ptr(), vgaBufPtr->buf_size(), 4 );	// 4-by pass the width and height info of the source data, only read the bitmap into the buffer
	get_data(bitmapId);
	reset_user_buf();

	spread_full_screen_image( vgaBufPtr );
}
//---------- End of function ImageRes::put_to_buf --------//

