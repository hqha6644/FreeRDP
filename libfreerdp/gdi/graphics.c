/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Graphical Objects
 *
 * Copyright 2011 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2016 Armin Novak <armin.novak@thincast.com>
 * Copyright 2016 Thincast Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <freerdp/config.h>

#include <winpr/crt.h>

#include <freerdp/log.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/dc.h>
#include <freerdp/gdi/shape.h>
#include <freerdp/gdi/region.h>
#include <freerdp/gdi/bitmap.h>

#include "clipping.h"
#include "drawing.h"
#include "brush.h"
#include "graphics.h"

#define TAG FREERDP_TAG("gdi")
/* Bitmap Class */

HGDI_BITMAP gdi_create_bitmap(rdpGdi* gdi, UINT32 nWidth, UINT32 nHeight, UINT32 SrcFormat,
                              BYTE* data)
{
	UINT32 nSrcStep = 0;
	UINT32 nDstStep = 0;
	BYTE* pSrcData = NULL;
	BYTE* pDstData = NULL;
	HGDI_BITMAP bitmap = NULL;

	if (!gdi)
		return NULL;

	nDstStep = nWidth * FreeRDPGetBytesPerPixel(gdi->dstFormat);
	pDstData = winpr_aligned_malloc(1ull * nHeight * nDstStep, 16);

	if (!pDstData)
		return NULL;

	pSrcData = data;
	nSrcStep = nWidth * FreeRDPGetBytesPerPixel(SrcFormat);

	if (!freerdp_image_copy_no_overlap(pDstData, gdi->dstFormat, nDstStep, 0, 0, nWidth, nHeight,
	                                   pSrcData, SrcFormat, nSrcStep, 0, 0, &gdi->palette,
	                                   FREERDP_FLIP_NONE))
	{
		winpr_aligned_free(pDstData);
		return NULL;
	}

	bitmap = gdi_CreateBitmap(nWidth, nHeight, gdi->dstFormat, pDstData);
	return bitmap;
}

static BOOL gdi_Bitmap_New(rdpContext* context, rdpBitmap* bitmap)
{
	gdiBitmap* gdi_bitmap = NULL;
	rdpGdi* gdi = context->gdi;
	gdi_bitmap = (gdiBitmap*)bitmap;
	gdi_bitmap->hdc = gdi_CreateCompatibleDC(gdi->hdc);

	if (!gdi_bitmap->hdc)
		return FALSE;

	if (!bitmap->data)
		gdi_bitmap->bitmap = gdi_CreateCompatibleBitmap(gdi->hdc, bitmap->width, bitmap->height);
	else
	{
		UINT32 format = bitmap->format;
		gdi_bitmap->bitmap =
		    gdi_create_bitmap(gdi, bitmap->width, bitmap->height, format, bitmap->data);
	}

	if (!gdi_bitmap->bitmap)
	{
		gdi_DeleteDC(gdi_bitmap->hdc);
		gdi_bitmap->hdc = NULL;
		return FALSE;
	}

	gdi_bitmap->hdc->format = gdi_bitmap->bitmap->format;
	gdi_SelectObject(gdi_bitmap->hdc, (HGDIOBJECT)gdi_bitmap->bitmap);
	gdi_bitmap->org_bitmap = NULL;
	return TRUE;
}

static void gdi_Bitmap_Free(WINPR_ATTR_UNUSED rdpContext* context, rdpBitmap* bitmap)
{
	gdiBitmap* gdi_bitmap = (gdiBitmap*)bitmap;

	if (gdi_bitmap)
	{
		if (gdi_bitmap->hdc)
			gdi_SelectObject(gdi_bitmap->hdc, (HGDIOBJECT)gdi_bitmap->org_bitmap);

		gdi_DeleteObject((HGDIOBJECT)gdi_bitmap->bitmap);
		gdi_DeleteDC(gdi_bitmap->hdc);
		winpr_aligned_free(bitmap->data);
	}

	free(bitmap);
}

static BOOL gdi_Bitmap_Paint(rdpContext* context, rdpBitmap* bitmap)
{
	gdiBitmap* gdi_bitmap = (gdiBitmap*)bitmap;
	UINT32 width = bitmap->right - bitmap->left + 1;
	UINT32 height = bitmap->bottom - bitmap->top + 1;
	return gdi_BitBlt(context->gdi->primary->hdc, WINPR_ASSERTING_INT_CAST(int, bitmap->left),
	                  WINPR_ASSERTING_INT_CAST(int, bitmap->top),
	                  WINPR_ASSERTING_INT_CAST(int, width), WINPR_ASSERTING_INT_CAST(int, height),
	                  gdi_bitmap->hdc, 0, 0, GDI_SRCCOPY, &context->gdi->palette);
}

static BOOL gdi_Bitmap_Decompress(rdpContext* context, rdpBitmap* bitmap, const BYTE* pSrcData,
                                  UINT32 DstWidth, UINT32 DstHeight, UINT32 bpp, UINT32 length,
                                  BOOL compressed, UINT32 codecId)
{
	UINT32 SrcSize = length;
	rdpGdi* gdi = context->gdi;
	UINT32 size = DstWidth * DstHeight;
	bitmap->compressed = FALSE;
	bitmap->format = gdi->dstFormat;

	if ((FreeRDPGetBytesPerPixel(bitmap->format) == 0) || (DstWidth == 0) || (DstHeight == 0) ||
	    (DstWidth > UINT32_MAX / DstHeight) ||
	    (size > (UINT32_MAX / FreeRDPGetBytesPerPixel(bitmap->format))))
	{
		WLog_ERR(TAG, "invalid input data");
		return FALSE;
	}

	size *= FreeRDPGetBytesPerPixel(bitmap->format);
	bitmap->length = size;
	bitmap->data = (BYTE*)winpr_aligned_malloc(bitmap->length, 16);

	if (!bitmap->data)
		return FALSE;

	if (compressed)
	{
		if ((codecId == RDP_CODEC_ID_REMOTEFX) || (codecId == RDP_CODEC_ID_IMAGE_REMOTEFX))
		{
			REGION16 invalidRegion = { 0 };
			region16_init(&invalidRegion);

			const BOOL rc =
			    rfx_process_message(context->codecs->rfx, pSrcData, SrcSize, bitmap->left,
			                        bitmap->top, bitmap->data, bitmap->format, gdi->stride,
			                        WINPR_ASSERTING_INT_CAST(UINT32, gdi->height), &invalidRegion);
			region16_uninit(&invalidRegion);

			if (!rc)
			{
				WLog_ERR(TAG, "rfx_process_message failed");
				return FALSE;
			}
		}
		else if (codecId == RDP_CODEC_ID_NSCODEC)
		{
			const int status = nsc_process_message(
			    context->codecs->nsc, 32, DstWidth, DstHeight, pSrcData, SrcSize, bitmap->data,
			    bitmap->format, 0, 0, 0, DstWidth, DstHeight, FREERDP_FLIP_VERTICAL);

			if (status < 1)
			{
				WLog_ERR(TAG, "nsc_process_message failed");
				return FALSE;
			}

			return freerdp_image_copy_no_overlap(bitmap->data, bitmap->format, 0, 0, 0, DstWidth,
			                                     DstHeight, pSrcData, PIXEL_FORMAT_XRGB32, 0, 0, 0,
			                                     &gdi->palette, FREERDP_FLIP_VERTICAL);
		}
		else if (bpp < 32)
		{
			if (!interleaved_decompress(context->codecs->interleaved, pSrcData, SrcSize, DstWidth,
			                            DstHeight, bpp, bitmap->data, bitmap->format, 0, 0, 0,
			                            DstWidth, DstHeight, &gdi->palette))
			{
				WLog_ERR(TAG, "interleaved_decompress failed");
				return FALSE;
			}
		}
		else
		{
			const BOOL fidelity =
			    freerdp_settings_get_bool(context->settings, FreeRDP_DrawAllowDynamicColorFidelity);
			freerdp_planar_switch_bgr(context->codecs->planar, fidelity);
			if (!planar_decompress(context->codecs->planar, pSrcData, SrcSize, DstWidth, DstHeight,
			                       bitmap->data, bitmap->format, 0, 0, 0, DstWidth, DstHeight,
			                       TRUE))
			{
				WLog_ERR(TAG, "planar_decompress failed");
				return FALSE;
			}
		}
	}
	else
	{
		const UINT32 SrcFormat = gdi_get_pixel_format(bpp);
		const size_t sbpp = FreeRDPGetBytesPerPixel(SrcFormat);
		const size_t dbpp = FreeRDPGetBytesPerPixel(bitmap->format);

		if ((sbpp == 0) || (dbpp == 0))
			return FALSE;
		else
		{
			const size_t dstSize = SrcSize * dbpp / sbpp;

			if (dstSize < bitmap->length)
			{
				WLog_ERR(TAG, "dstSize %" PRIuz " < bitmap->length %" PRIu32, dstSize,
				         bitmap->length);
				return FALSE;
			}
		}

		if (!freerdp_image_copy_no_overlap(bitmap->data, bitmap->format, 0, 0, 0, DstWidth,
		                                   DstHeight, pSrcData, SrcFormat, 0, 0, 0, &gdi->palette,
		                                   FREERDP_FLIP_VERTICAL))
		{
			WLog_ERR(TAG, "freerdp_image_copy failed");
			return FALSE;
		}
	}

	return TRUE;
}

static BOOL gdi_Bitmap_SetSurface(rdpContext* context, rdpBitmap* bitmap, BOOL primary)
{
	rdpGdi* gdi = NULL;

	if (!context)
		return FALSE;

	gdi = context->gdi;

	if (!gdi)
		return FALSE;

	if (primary)
		gdi->drawing = gdi->primary;
	else
		gdi->drawing = (gdiBitmap*)bitmap;

	return TRUE;
}

/* Glyph Class */
static BOOL gdi_Glyph_New(rdpContext* context, rdpGlyph* glyph)
{
	BYTE* data = NULL;
	gdiGlyph* gdi_glyph = NULL;

	if (!context || !glyph)
		return FALSE;

	gdi_glyph = (gdiGlyph*)glyph;
	gdi_glyph->hdc = gdi_GetDC();

	if (!gdi_glyph->hdc)
		return FALSE;

	gdi_glyph->hdc->format = PIXEL_FORMAT_MONO;
	data = freerdp_glyph_convert(glyph->cx, glyph->cy, glyph->aj);

	if (!data)
	{
		gdi_DeleteDC(gdi_glyph->hdc);
		return FALSE;
	}

	gdi_glyph->bitmap = gdi_CreateBitmap(glyph->cx, glyph->cy, PIXEL_FORMAT_MONO, data);

	if (!gdi_glyph->bitmap)
	{
		gdi_DeleteDC(gdi_glyph->hdc);
		winpr_aligned_free(data);
		return FALSE;
	}

	gdi_SelectObject(gdi_glyph->hdc, (HGDIOBJECT)gdi_glyph->bitmap);
	gdi_glyph->org_bitmap = NULL;
	return TRUE;
}

static void gdi_Glyph_Free(WINPR_ATTR_UNUSED rdpContext* context, rdpGlyph* glyph)
{
	gdiGlyph* gdi_glyph = NULL;
	gdi_glyph = (gdiGlyph*)glyph;

	if (gdi_glyph)
	{
		gdi_SelectObject(gdi_glyph->hdc, (HGDIOBJECT)gdi_glyph->org_bitmap);
		gdi_DeleteObject((HGDIOBJECT)gdi_glyph->bitmap);
		gdi_DeleteDC(gdi_glyph->hdc);
		free(glyph->aj);
		free(glyph);
	}
}

static BOOL gdi_Glyph_Draw(rdpContext* context, const rdpGlyph* glyph, INT32 x, INT32 y, INT32 w,
                           INT32 h, INT32 sx, INT32 sy, BOOL fOpRedundant)
{
	const gdiGlyph* gdi_glyph = NULL;
	rdpGdi* gdi = NULL;
	HGDI_BRUSH brush = NULL;
	BOOL rc = FALSE;

	if (!context || !glyph)
		return FALSE;

	gdi = context->gdi;
	gdi_glyph = (const gdiGlyph*)glyph;

	if (!fOpRedundant)
	{
		GDI_RECT rect = { 0 };

		if (x > 0)
			rect.left = x;

		if (y > 0)
			rect.top = y;

		if (x + w > 0)
			rect.right = x + w - 1;

		if (y + h > 0)
			rect.bottom = y + h - 1;

		if ((rect.left < rect.right) && (rect.top < rect.bottom))
		{
			brush = gdi_CreateSolidBrush(gdi->drawing->hdc->bkColor);

			if (!brush)
				return FALSE;

			gdi_FillRect(gdi->drawing->hdc, &rect, brush);
			gdi_DeleteObject((HGDIOBJECT)brush);
		}
	}

	brush = gdi_CreateSolidBrush(gdi->drawing->hdc->textColor);

	if (!brush)
		return FALSE;

	gdi_SelectObject(gdi->drawing->hdc, (HGDIOBJECT)brush);
	rc = gdi_BitBlt(gdi->drawing->hdc, x, y, w, h, gdi_glyph->hdc, sx, sy, GDI_GLYPH_ORDER,
	                &context->gdi->palette);
	gdi_DeleteObject((HGDIOBJECT)brush);
	return rc;
}

static BOOL gdi_Glyph_BeginDraw(rdpContext* context, INT32 x, INT32 y, INT32 width, INT32 height,
                                UINT32 bgcolor, UINT32 fgcolor, BOOL fOpRedundant)
{
	rdpGdi* gdi = NULL;

	if (!context || !context->gdi)
		return FALSE;

	gdi = context->gdi;

	if (!gdi->drawing || !gdi->drawing->hdc)
		return FALSE;

	if (!fOpRedundant)
	{
		if (!gdi_decode_color(gdi, bgcolor, &bgcolor, NULL))
			return FALSE;

		if (!gdi_decode_color(gdi, fgcolor, &fgcolor, NULL))
			return FALSE;

		gdi_SetClipRgn(gdi->drawing->hdc, x, y, width, height);
		gdi_SetTextColor(gdi->drawing->hdc, bgcolor);
		gdi_SetBkColor(gdi->drawing->hdc, fgcolor);

		if (1)
		{
			GDI_RECT rect = { 0 };
			HGDI_BRUSH brush = gdi_CreateSolidBrush(fgcolor);

			if (!brush)
				return FALSE;

			if (x > 0)
				rect.left = x;

			if (y > 0)
				rect.top = y;

			rect.right = x + width - 1;
			rect.bottom = y + height - 1;

			if ((x + width > rect.left) && (y + height > rect.top))
				gdi_FillRect(gdi->drawing->hdc, &rect, brush);

			gdi_DeleteObject((HGDIOBJECT)brush);
		}

		return gdi_SetNullClipRgn(gdi->drawing->hdc);
	}

	return TRUE;
}

static BOOL gdi_Glyph_EndDraw(rdpContext* context, WINPR_ATTR_UNUSED INT32 x,
                              WINPR_ATTR_UNUSED INT32 y, WINPR_ATTR_UNUSED INT32 width,
                              WINPR_ATTR_UNUSED INT32 height, WINPR_ATTR_UNUSED UINT32 bgcolor,
                              WINPR_ATTR_UNUSED UINT32 fgcolor)
{
	rdpGdi* gdi = NULL;

	if (!context || !context->gdi)
		return FALSE;

	gdi = context->gdi;

	if (!gdi->drawing || !gdi->drawing->hdc)
		return FALSE;

	gdi_SetNullClipRgn(gdi->drawing->hdc);
	return TRUE;
}

/* Graphics Module */
BOOL gdi_register_graphics(rdpGraphics* graphics)
{
	rdpBitmap bitmap = { 0 };
	rdpGlyph glyph = { 0 };
	bitmap.size = sizeof(gdiBitmap);
	bitmap.New = gdi_Bitmap_New;
	bitmap.Free = gdi_Bitmap_Free;
	bitmap.Paint = gdi_Bitmap_Paint;
	bitmap.Decompress = gdi_Bitmap_Decompress;
	bitmap.SetSurface = gdi_Bitmap_SetSurface;
	graphics_register_bitmap(graphics, &bitmap);
	glyph.size = sizeof(gdiGlyph);
	glyph.New = gdi_Glyph_New;
	glyph.Free = gdi_Glyph_Free;
	glyph.Draw = gdi_Glyph_Draw;
	glyph.BeginDraw = gdi_Glyph_BeginDraw;
	glyph.EndDraw = gdi_Glyph_EndDraw;
	graphics_register_glyph(graphics, &glyph);
	return TRUE;
}
