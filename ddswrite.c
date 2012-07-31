/*
	DDS GIMP plugin

	Copyright (C) 2004 Shawn Kirst <skirst@gmail.com>,
   with parts (C) 2003 Arne Reuter <homepage@arnereuter.de> where specified.

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301 USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "ddsplugin.h"
#include "dds.h"
#include "dxt.h"
#include "mipmap.h"
#include "endian.h"
#include "imath.h"
#include "color.h"

static gint save_dialog(gint32 image_id, gint32 drawable);
static void save_dialog_response(GtkWidget *widget, gint response_id, gpointer data);
static int write_image(FILE *fp, gint32 image_id, gint32 drawable_id);

static int runme = 0;

enum
{
   COMBO_VALUE, COMBO_STRING, COMBO_SENSITIVE
};

static const char *cubemap_face_names[4][6] =
{
   {
      "positive x", "negative x",
      "positive y", "negative y",
      "positive z", "negative z"
   },
   {
      "pos x", "neg x",
      "pos y", "neg y",
      "pos z", "neg z",
   },
   {
      "+x", "-x",
      "+y", "-y",
      "+z", "-z"
   },
   {
      "right", "left",
      "top", "bottom",
      "back", "front"
   }
};

static gint cubemap_faces[6];
static gint is_cubemap = 0;
static gint is_volume = 0;
static gint is_mipmap_chain_valid = 0;

static GtkWidget *compress_opt;
static GtkWidget *format_opt;
static GtkWidget *color_type_opt;
static GtkWidget *dither_chk;
static GtkWidget *mipmap_filter_opt;
static GtkWidget *gamma_chk;
static GtkWidget *gamma_spin;

typedef struct string_value_s
{
   int value;
   char *string;
} string_value_t;

static string_value_t compression_strings[] =
{
   {DDS_COMPRESS_NONE,   "None"},
   {DDS_COMPRESS_BC1,    "BC1 / DXT1"},
   {DDS_COMPRESS_BC2,    "BC2 / DXT3"},
   {DDS_COMPRESS_BC3,    "BC3 / DXT5"},
   {DDS_COMPRESS_BC3N,   "BC3nm / DXT5nm"},
   {DDS_COMPRESS_BC4,    "BC4 / ATI1 (3Dc+)"},
   {DDS_COMPRESS_BC5,    "BC5 / ATI2 (3Dc)"},
   {DDS_COMPRESS_RXGB,   "RXGB (DXT5)"},
   {DDS_COMPRESS_AEXP,   "Alpha Exponent (DXT5)"},
   {DDS_COMPRESS_YCOCG,  "YCoCg (DXT5)"},
   {DDS_COMPRESS_YCOCGS, "YCoCg scaled (DXT5)"},
   {-1, 0}
};

static string_value_t format_strings[] =
{
   {DDS_FORMAT_DEFAULT, "Default"},
   {DDS_FORMAT_RGB8,    "RGB8"},
   {DDS_FORMAT_RGBA8,   "RGBA8"},
   {DDS_FORMAT_BGR8,    "BGR8"},
   {DDS_FORMAT_ABGR8,   "ABGR8"},
   {DDS_FORMAT_R5G6B5,  "R5G6B5"},
   {DDS_FORMAT_RGBA4,   "RGBA4"},
   {DDS_FORMAT_RGB5A1,  "RGB5A1"},
   {DDS_FORMAT_RGB10A2, "RGB10A2"},
   {DDS_FORMAT_R3G3B2,  "R3G3B2"},
   {DDS_FORMAT_A8,      "A8"},
   {DDS_FORMAT_L8,      "L8"},
   {DDS_FORMAT_L8A8,    "L8A8"},
   {DDS_FORMAT_AEXP,    "AExp"},
   {DDS_FORMAT_YCOCG,   "YCoCg"},
   {-1, 0}
};

static string_value_t mipmap_strings[] =
{
   {DDS_MIPMAP_NONE,     "No mipmaps"},
   {DDS_MIPMAP_GENERATE, "Generate mipmaps"},
   {DDS_MIPMAP_EXISTING, "Use existing mipmaps"},
   {-1, 0}
};

static string_value_t color_type_strings[] =
{
   {DDS_COLOR_DEFAULT,    "Default"},
   {DDS_COLOR_DISTANCE,   "Distance"},
   {DDS_COLOR_LUMINANCE,  "Luminance"},
   {DDS_COLOR_INSET_BBOX, "Inset bounding box"},
   {-1, 0}
};

static string_value_t mipmap_filter_strings[] =
{
   {DDS_MIPMAP_FILTER_DEFAULT,  "Default"},
   {DDS_MIPMAP_FILTER_NEAREST,  "Nearest"},
   {DDS_MIPMAP_FILTER_BOX,      "Box"},
   {DDS_MIPMAP_FILTER_BILINEAR, "Bilinear"},
   {DDS_MIPMAP_FILTER_BICUBIC,  "Bicubic"},
   {DDS_MIPMAP_FILTER_LANCZOS,  "Lanczos"},
   {-1, 0}
};

static string_value_t save_type_strings[] =
{
   {DDS_SAVE_SELECTED_LAYER, "Selected layer"},
   {DDS_SAVE_CUBEMAP,        "As cube map"},
   {DDS_SAVE_VOLUMEMAP,      "As volume map"},
   {-1, 0}
};

static struct
{
   int format;
   DXGI_FORMAT dxgi_format;
   int bpp;
   int alpha;
   unsigned int rmask;
   unsigned int gmask;
   unsigned int bmask;
   unsigned int amask;
} format_info[] =
{
   {DDS_FORMAT_RGB8,    DXGI_FORMAT_B8G8R8X8_UNORM,    3, 0, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000},
   {DDS_FORMAT_RGBA8,   DXGI_FORMAT_B8G8R8A8_UNORM,    4, 1, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000},
   {DDS_FORMAT_BGR8,    DXGI_FORMAT_UNKNOWN,           3, 0, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000},
   {DDS_FORMAT_ABGR8,   DXGI_FORMAT_R8G8B8A8_UNORM,    4, 1, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000},
   {DDS_FORMAT_R5G6B5,  DXGI_FORMAT_B5G6R5_UNORM,      2, 0, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000},
   {DDS_FORMAT_RGBA4,   DXGI_FORMAT_UNKNOWN,           2, 1, 0x00000f00, 0x000000f0, 0x0000000f, 0x0000f000},
   {DDS_FORMAT_RGB5A1,  DXGI_FORMAT_B5G5R5A1_UNORM,    2, 1, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000},
   {DDS_FORMAT_RGB10A2, DXGI_FORMAT_R10G10B10A2_UNORM, 4, 1, 0x000003ff, 0x000ffc00, 0x3ff00000, 0xc0000000},
   {DDS_FORMAT_R3G3B2,  DXGI_FORMAT_UNKNOWN,           1, 0, 0x000000e0, 0x0000001c, 0x00000003, 0x00000000},
   {DDS_FORMAT_A8,      DXGI_FORMAT_A8_UNORM,          1, 0, 0x00000000, 0x00000000, 0x00000000, 0x000000ff},
   {DDS_FORMAT_L8,      DXGI_FORMAT_R8_UNORM,          1, 0, 0x000000ff, 0x000000ff, 0x000000ff, 0x00000000},
   {DDS_FORMAT_L8A8,    DXGI_FORMAT_UNKNOWN,           2, 1, 0x000000ff, 0x000000ff, 0x000000ff, 0x0000ff00},
   {DDS_FORMAT_AEXP,    DXGI_FORMAT_B8G8R8A8_UNORM,    4, 1, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000},
   {DDS_FORMAT_YCOCG,   DXGI_FORMAT_B8G8R8A8_UNORM,    4, 1, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000}
};

static int check_cubemap(gint32 image_id)
{
   gint *layers, num_layers;
   int cubemap = 0, i, j, k, w, h;
   char *layer_name;
   GimpDrawable *drawable;
   GimpImageType type;

   layers = gimp_image_get_layers(image_id, &num_layers);

   if(num_layers == 6)
   {
      for(i = 0; i < 6; ++i)
         cubemap_faces[i] = -1;

      for(i = 0; i < 6; ++i)
      {
         layer_name = (char*)gimp_drawable_get_name(layers[i]);
         for(j = 0; j < 6; ++j)
         {
            for(k = 0; k < 4; ++k)
            {
               if(strstr(layer_name, cubemap_face_names[k][j]))
               {
                  if(cubemap_faces[j] == -1)
                  {
                     cubemap_faces[j] = layers[i];
                     break;
                  }
               }
            }
         }
      }

      cubemap = 1;

      /* check for 6 valid faces */
      for(i = 0; i < 6; ++i)
      {
         if(cubemap_faces[i] == -1)
         {
            cubemap = 0;
            break;
         }
      }

      /* make sure they are all the same size */
      if(cubemap)
      {
         drawable = gimp_drawable_get(cubemap_faces[0]);
         w = drawable->width;
         h = drawable->height;
         gimp_drawable_detach(drawable);
         for(i = 1; i < 6 && cubemap; ++i)
         {
            drawable = gimp_drawable_get(cubemap_faces[i]);
            if(drawable->width  != w ||
               drawable->height != h)
            {
               cubemap = 0;
            }
            gimp_drawable_detach(drawable);
         }
         /*
         if(cubemap == 0)
         {
            g_message("DDS: It appears that your image is a cube map,\n"
                      "but not all layers are the same size, thus a cube\n"
                      "map cannot be written.");
         }
         */
      }

      /* make sure they are all the same type */
      if(cubemap)
      {
         type = gimp_drawable_type(cubemap_faces[0]);
         for(i = 1; i < 6; ++i)
         {
            if(gimp_drawable_type(cubemap_faces[i]) != type)
            {
               cubemap = 0;
               break;
            }
         }

         /*
         if(cubemap == 0)
         {
            g_message("DDS: It appears that your image is a cube map,\n"
                      "but not all layers are the same type, thus a cube\n"
                      "map cannot be written (Perhaps some layers have\n"
                      "transparency and others do not?).");
         }
         */
      }
   }

   return(cubemap);
}

static int check_volume(gint32 image_id)
{
   gint *layers, num_layers;
   int volume = 0, i, w, h;
   GimpDrawable *drawable;
   GimpImageType type;

   layers = gimp_image_get_layers(image_id, &num_layers);

   if(num_layers > 1)
   {
      volume = 1;

      drawable = gimp_drawable_get(layers[0]);
      w = drawable->width;
      h = drawable->height;
      gimp_drawable_detach(drawable);
      for(i = 1; i < num_layers && volume; ++i)
      {
         drawable = gimp_drawable_get(layers[i]);
         if(drawable->width  != w ||
            drawable->height != h)
         {
            volume = 0;
         }
         gimp_drawable_detach(drawable);
      }

      /*
      if(!volume)
      {
         g_message("DDS: It appears your image may be a volume map,\n"
                   "but not all layers are the same size, thus a volume\n"
                   "map cannot be written.");
      }
      */

      if(volume)
      {
         type = gimp_drawable_type(layers[0]);
         for(i = 1; i < num_layers; ++i)
         {
            if(gimp_drawable_type(layers[i]) != type)
            {
               volume = 0;
               break;
            }
         }

         /*
         if(!volume)
         {
            g_message("DDS: It appears your image may be a volume map,\n"
                      "but not all layers are the same type, thus a volume\n"
                      "map cannot be written (Perhaps some layers have\n"
                      "transparency and others do not?).");
         }
         */
      }
   }

   return(volume);
}

static int check_mipmap_chain_consitency(gint32 image_id)
{
   gint *layers, num_layers;
   GimpDrawable *drawable = NULL;
   GimpImageType type = GIMP_RGB_IMAGE;
   int i, w, h, mipw, miph, mipmaps = 1;
   int max_w = 0, max_h = 0;

   layers = gimp_image_get_layers(image_id, &num_layers);

   if(num_layers == 1) return(1);

   /* find largest layer */
   for(i = 0; i < num_layers; ++i)
   {
      drawable = gimp_drawable_get(layers[i]);
      if((drawable->width * drawable->height) > (max_w * max_h))
      {
         type = gimp_drawable_type(layers[i]);
         max_w = drawable->width;
         max_h = drawable->height;
      }
      gimp_drawable_detach(drawable);
   }

   w = max_w;
   h = max_h;

   /* look for a complete mipmap chain */
   while(get_next_mipmap_dimensions(&mipw, &miph, w, h))
   {
      /* search layers for the next mipmap */
      for(i = 0; i < num_layers; ++i)
      {
         drawable = gimp_drawable_get(layers[i]);
         if((drawable->width  == mipw) &&
            (drawable->height == miph) &&
            (gimp_drawable_type(layers[i]) == type))
         {
            break;
         }
         else
         {
            gimp_drawable_detach(drawable);
         }
      }

      /* a layer meeting the needed mipmap dimensions was not found */
      if(i == num_layers)
         break;

      ++mipmaps;
      w = mipw;
      h = miph;

      gimp_drawable_detach(drawable);
   }

   return(mipmaps == num_layers);
}

static gint32 get_mipmap0_drawable_id(gint32 image_id)
{
   gint *layers, num_layers;
   GimpDrawable *drawable = NULL;
   int i, max_w = 0, max_h = 0;
   gint32 max_layer_id = -1;

   layers = gimp_image_get_layers(image_id, &num_layers);

   /* find largest layer */
   for(i = 0; i < num_layers; ++i)
   {
      drawable = gimp_drawable_get(layers[i]);
      if((drawable->width * drawable->height) > (max_w * max_h))
      {
         max_w = drawable->width;
         max_h = drawable->height;
         max_layer_id = drawable->drawable_id;
      }
      gimp_drawable_detach(drawable);
   }

   return(max_layer_id);
}

GimpPDBStatusType write_dds(gchar *filename, gint32 image_id, gint32 drawable_id)
{
   FILE *fp;
   gchar *tmp;
   int rc = 0;

   is_mipmap_chain_valid = check_mipmap_chain_consitency(image_id);

   is_cubemap = check_cubemap(image_id);
   is_volume = check_volume(image_id);

   /*
    a valid mipmap chain was detected, and user wants to save with
    existing mipmaps.  Override the drawable_id passed (selected layer)
    and find the drawable_id of the top level (largest) mipmap layer.
   */
   if(is_mipmap_chain_valid &&
      dds_write_vals.mipmaps == DDS_MIPMAP_EXISTING)
      drawable_id = get_mipmap0_drawable_id(image_id);

   if(interactive_dds)
   {
      if(!is_mipmap_chain_valid &&
         dds_write_vals.mipmaps == DDS_MIPMAP_EXISTING)
         dds_write_vals.mipmaps = DDS_MIPMAP_NONE;

      if(!save_dialog(image_id, drawable_id))
         return(GIMP_PDB_CANCEL);
   }
   else
   {
      if(dds_write_vals.savetype == DDS_SAVE_CUBEMAP && !is_cubemap)
      {
         g_message("DDS: Cannot save image as cube map");
         return(GIMP_PDB_EXECUTION_ERROR);
      }

      if(dds_write_vals.savetype == DDS_SAVE_VOLUMEMAP && !is_volume)
      {
         g_message("DDS: Cannot save image as volume map");
         return(GIMP_PDB_EXECUTION_ERROR);
      }

      if(dds_write_vals.savetype == DDS_SAVE_VOLUMEMAP &&
         dds_write_vals.compression != DDS_COMPRESS_NONE)
      {
         g_message("DDS: Cannot save volume map with compression");
         return(GIMP_PDB_EXECUTION_ERROR);
      }

      if(dds_write_vals.mipmaps == DDS_MIPMAP_EXISTING &&
         !is_mipmap_chain_valid)
      {
         g_message("DDS: Cannot save with existing mipmaps as the mipmap chain is incomplete");
         return(GIMP_PDB_EXECUTION_ERROR);
      }
   }

   fp = fopen(filename, "wb");
   if(fp == 0)
   {
      g_message("Error opening %s", filename);
      return(GIMP_PDB_EXECUTION_ERROR);
   }

   if(interactive_dds)
   {
      if(strrchr(filename, '/'))
         tmp = g_strdup_printf("Saving %s:", strrchr(filename, '/') + 1);
      else
         tmp = g_strdup_printf("Saving %s:", filename);
      gimp_progress_init(tmp);
      g_free(tmp);
   }

   rc = write_image(fp, image_id, drawable_id);

   fclose(fp);

   return(rc ? GIMP_PDB_SUCCESS : GIMP_PDB_EXECUTION_ERROR);
}

static void swap_rb(unsigned char *pixels, unsigned int n, int bpp)
{
   unsigned int i;
   unsigned char t;

   for(i = 0; i < n; ++i)
   {
      t = pixels[bpp * i + 0];
      pixels[bpp * i + 0] = pixels[bpp * i + 2];
      pixels[bpp * i + 2] = t;
   }
}

static void alpha_exp(unsigned char *dst, int r, int g, int b, int a)
{
   float ar, ag, ab, aa;

   ar = (float)r / 255.0f;
   ag = (float)g / 255.0f;
   ab = (float)b / 255.0f;

   aa = MAX(ar, MAX(ag, ab));

   if(aa < 1e-04f)
   {
      dst[0] = b;
      dst[1] = g;
      dst[2] = r;
      dst[3] = 255;
      return;
   }

   ar /= aa;
   ag /= aa;
   ab /= aa;

   r = (int)floorf(255.0f * ar + 0.5f);
   g = (int)floorf(255.0f * ag + 0.5f);
   b = (int)floorf(255.0f * ab + 0.5f);
   a = (int)floorf(255.0f * aa + 0.5f);

   dst[0] = MAX(0, MIN(255, b));
   dst[1] = MAX(0, MIN(255, g));
   dst[2] = MAX(0, MIN(255, r));
   dst[3] = MAX(0, MIN(255, a));
}

static void convert_pixels(unsigned char *dst, unsigned char *src,
                           int format, int w, int h, int d, int bpp,
                           unsigned char *palette, int mipmaps)
{
   unsigned int i, num_pixels;
   unsigned char r, g, b, a;

   if(d > 0)
      num_pixels = get_volume_mipmapped_size(w, h, d, 1, 0, mipmaps, DDS_COMPRESS_NONE);
   else
      num_pixels = get_mipmapped_size(w, h, 1, 0, mipmaps, DDS_COMPRESS_NONE);

   for(i = 0; i < num_pixels; ++i)
   {
      if(bpp == 1)
      {
         if(palette)
         {
            r = palette[3 * src[i] + 0];
            g = palette[3 * src[i] + 1];
            b = palette[3 * src[i] + 2];
         }
         else
            r = g = b = src[i];

         if(format == DDS_FORMAT_A8)
            a = src[i];
         else
            a = 255;
      }
      else if(bpp == 2)
      {
         r = g = b = src[2 * i];
         a = src[2 * i + 1];
      }
      else if(bpp == 3)
      {
         b = src[3 * i + 0];
         g = src[3 * i + 1];
         r = src[3 * i + 2];
         a = 255;
      }
      else
      {
         b = src[4 * i + 0];
         g = src[4 * i + 1];
         r = src[4 * i + 2];
         a = src[4 * i + 3];
      }

      switch(format)
      {
         case DDS_FORMAT_RGB8:
            dst[3 * i + 0] = b;
            dst[3 * i + 1] = g;
            dst[3 * i + 2] = r;
            break;
         case DDS_FORMAT_RGBA8:
            dst[4 * i + 0] = b;
            dst[4 * i + 1] = g;
            dst[4 * i + 2] = r;
            dst[4 * i + 3] = a;
            break;
         case DDS_FORMAT_BGR8:
            dst[3 * i + 0] = r;
            dst[3 * i + 1] = g;
            dst[3 * i + 2] = b;
            break;
         case DDS_FORMAT_ABGR8:
            dst[4 * i + 0] = r;
            dst[4 * i + 1] = g;
            dst[4 * i + 2] = b;
            dst[4 * i + 3] = a;
            break;
         case DDS_FORMAT_R5G6B5:
            PUTL16(&dst[2 * i], pack_r5g6b5(r, g, b));
            break;
         case DDS_FORMAT_RGBA4:
            PUTL16(&dst[2 * i], pack_rgba4(r, g, b, a));
            break;
         case DDS_FORMAT_RGB5A1:
            PUTL16(&dst[2 * i], pack_rgb5a1(r, g, b, a));
            break;
         case DDS_FORMAT_RGB10A2:
            PUTL32(&dst[4 * i], pack_rgb10a2(r, g, b, a));
            break;
         case DDS_FORMAT_R3G3B2:
            dst[i] = pack_r3g3b2(r, g, b);
            break;
         case DDS_FORMAT_A8:
            dst[i] = a;
            break;
         case DDS_FORMAT_L8:
            dst[i] = rgb_to_luminance(r, g, b);
            break;
         case DDS_FORMAT_L8A8:
            dst[2 * i + 0] = rgb_to_luminance(r, g, b);
            dst[2 * i + 1] = a;
            break;
         case DDS_FORMAT_YCOCG:
            dst[4 * i] = a;
            RGB_to_YCoCg(&dst[4 * i], r, g, b);
            break;
         case DDS_FORMAT_AEXP:
            alpha_exp(&dst[4 * i], r, g, b, a);
            break;
         default:
            break;
      }
   }
}

static void get_mipmap_chain(unsigned char *dst, int w, int h, int bpp,
                             gint32 image_id)
{
   gint *layers, num_layers;
   GimpDrawable *drawable;
   GimpPixelRgn rgn;
   int i, offset, mipw, miph;

   layers = gimp_image_get_layers(image_id, &num_layers);

   offset = 0;

   while(get_next_mipmap_dimensions(&mipw, &miph, w, h))
   {
      drawable = NULL;

      /* search layers for the next mipmap */
      for(i = 0; i < num_layers; ++i)
      {
         drawable = gimp_drawable_get(layers[i]);
         if((drawable->width  == mipw) &&
            (drawable->height == miph))
            break;
      }

      if(i == num_layers) return;
      if(drawable == NULL) return;

      gimp_pixel_rgn_init(&rgn, drawable, 0, 0, mipw, miph, 0, 0);
      gimp_pixel_rgn_get_rect(&rgn, dst + offset, 0, 0, mipw, miph);

      /* we need BGRX or BGRA */
      if(bpp >= 3)
         swap_rb(dst + offset, mipw * miph, bpp);

      offset += (mipw * miph * bpp);
      w = mipw;
      h = miph;

      gimp_drawable_detach(drawable);
   }
}

static void write_layer(FILE *fp, gint32 image_id, gint32 drawable_id,
                        int w, int h, int bpp, int fmtbpp, int mipmaps)
{
   GimpDrawable *drawable;
   GimpPixelRgn rgn;
   GimpImageBaseType basetype;
   GimpImageType type;
   unsigned char *src, *dst, *fmtdst, *tmp;
   unsigned char *palette = NULL;
   int i, c, x, y, size, fmtsize, offset, colors;
   int compression = dds_write_vals.compression;

   basetype = gimp_image_base_type(image_id);
   type = gimp_drawable_type(drawable_id);

   drawable = gimp_drawable_get(drawable_id);
   src = g_malloc(w * h * bpp);
   gimp_pixel_rgn_init(&rgn, drawable, 0, 0, w, h, 0, 0);
   gimp_pixel_rgn_get_rect(&rgn, src, 0, 0, w, h);

   if(basetype == GIMP_INDEXED)
   {
      palette = gimp_image_get_colormap(image_id, &colors);

      if(type == GIMP_INDEXEDA_IMAGE)
      {
         tmp = g_malloc(w * h);
         for(i = 0; i < w * h; ++i)
            tmp[i] = src[2 * i];
         g_free(src);
         src = tmp;
         bpp = 1;
      }
   }

   /* we want and assume BGRA ordered pixels for bpp >= 3 from here and
      onwards */
   if(bpp >= 3)
      swap_rb(src, w * h, bpp);

   if(compression == DDS_COMPRESS_BC3N)
   {
      if(bpp != 4)
      {
         fmtsize = w * h * 4;
         fmtdst = g_malloc(fmtsize);
         convert_pixels(fmtdst, src, DDS_FORMAT_RGBA8, w, h, 0, bpp,
                        palette, 1);
         g_free(src);
         src = fmtdst;
         bpp = 4;
      }

      for(y = 0; y < drawable->height; ++y)
      {
         for(x = 0; x < drawable->width; ++x)
         {
            /* set alpha to red (x) */
            src[y * (drawable->width * 4) + (x * 4) + 3] =
               src[y * (drawable->width * 4) + (x * 4) + 2];
            /* set red to 1 */
            src[y * (drawable->width * 4) + (x * 4) + 2] = 255;
         }
      }
   }

   /* RXGB (Doom3) */
   if(compression == DDS_COMPRESS_RXGB)
   {
      if(bpp != 4)
      {
         fmtsize = w * h * 4;
         fmtdst = g_malloc(fmtsize);
         convert_pixels(fmtdst, src, DDS_FORMAT_RGBA8, w, h, 0, bpp,
                        palette, 1);
         g_free(src);
         src = fmtdst;
         bpp = 4;
      }

      for(y = 0; y < drawable->height; ++y)
      {
         for(x = 0; x < drawable->width; ++x)
         {
            /* swap red and alpha */
            c = src[y * (drawable->width * 4) + (x * 4) + 3];
            src[y * (drawable->width * 4) + (x * 4) + 3] =
               src[y * (drawable->width * 4) + (x * 4) + 2];
            src[y * (drawable->width * 4) + (x * 4) + 2] = c;
         }
      }
   }

   if(compression == DDS_COMPRESS_YCOCG ||
      compression == DDS_COMPRESS_YCOCGS) /* convert to YCoCG */
   {
      fmtsize = w * h * 4;
      fmtdst = g_malloc(fmtsize);
      convert_pixels(fmtdst, src, DDS_FORMAT_YCOCG, w, h, 0, bpp,
                     palette, 1);
      g_free(src);
      src = fmtdst;
      bpp = 4;
   }

   if(compression == DDS_COMPRESS_AEXP)
   {
      fmtsize = w * h * 4;
      fmtdst = g_malloc(fmtsize);
      convert_pixels(fmtdst, src, DDS_FORMAT_AEXP, w, h, 0, bpp,
                     palette, 1);
      g_free(src);
      src = fmtdst;
      bpp = 4;
   }

   if(compression == DDS_COMPRESS_NONE)
   {
      if(mipmaps > 1)
      {
         /* pre-convert indexed images to RGB for better quality mipmaps
            if a pixel format conversion is requested */
         if(dds_write_vals.format > DDS_FORMAT_DEFAULT && basetype == GIMP_INDEXED)
         {
            fmtsize = get_mipmapped_size(w, h, 3, 0, mipmaps, DDS_COMPRESS_NONE);
            fmtdst = g_malloc(fmtsize);
            convert_pixels(fmtdst, src, DDS_FORMAT_RGB8, w, h, 0, bpp,
                           palette, 1);
            g_free(src);
            src = fmtdst;
            bpp = 3;
            palette = NULL;
         }

         size = get_mipmapped_size(w, h, bpp, 0, mipmaps, DDS_COMPRESS_NONE);
         dst = g_malloc(size);
         if(dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE)
         {
            generate_mipmaps(dst, src, w, h, bpp, palette != NULL,
                             mipmaps,
                             dds_write_vals.mipmap_filter,
                             dds_write_vals.gamma_correct,
                             dds_write_vals.gamma);
         }
         else
         {
            memcpy(dst, src, w * h * bpp);
            get_mipmap_chain(dst + (w * h * bpp), w, h, bpp, image_id);
         }

         if(dds_write_vals.format > DDS_FORMAT_DEFAULT)
         {
            fmtsize = get_mipmapped_size(w, h, fmtbpp, 0, mipmaps,
                                         DDS_COMPRESS_NONE);
            fmtdst = g_malloc(fmtsize);

            convert_pixels(fmtdst, dst, dds_write_vals.format, w, h, 0, bpp,
                           palette, mipmaps);

            g_free(dst);
            dst = fmtdst;
            bpp = fmtbpp;
         }

         offset = 0;

         for(i = 0; i < mipmaps; ++i)
         {
            size = get_mipmapped_size(w, h, bpp, i, 1, DDS_COMPRESS_NONE);
            fwrite(dst + offset, 1, size, fp);
            offset += size;
         }

         g_free(dst);
      }
      else
      {
         if(dds_write_vals.format > DDS_FORMAT_DEFAULT)
         {
            fmtdst = g_malloc(h * w * fmtbpp);
            convert_pixels(fmtdst, src, dds_write_vals.format, w, h, 0, bpp,
                           palette, 1);
            g_free(src);
            src = fmtdst;
            bpp = fmtbpp;
         }

         fwrite(src, 1, h * w * bpp, fp);
      }
   }
   else
   {
      size = get_mipmapped_size(w, h, bpp, 0, mipmaps, compression);

      dst = g_malloc(size);

      if(basetype == GIMP_INDEXED)
      {
         fmtsize = get_mipmapped_size(w, h, 3, 0, mipmaps,
                                      DDS_COMPRESS_NONE);
         fmtdst = g_malloc(fmtsize);
         convert_pixels(fmtdst, src, DDS_FORMAT_RGB8, w, h, 0, bpp,
                        palette, mipmaps);
         g_free(src);
         src = fmtdst;
         bpp = 3;
      }

      if(mipmaps > 1)
      {
         fmtsize = get_mipmapped_size(w, h, bpp, 0, mipmaps,
                                      DDS_COMPRESS_NONE);
         fmtdst = g_malloc(fmtsize);
         if(dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE)
         {
            generate_mipmaps(fmtdst, src, w, h, bpp, 0, mipmaps,
                             dds_write_vals.mipmap_filter,
                             dds_write_vals.gamma_correct, dds_write_vals.gamma);
         }
         else
         {
            memcpy(fmtdst, src, w * h * bpp);
            get_mipmap_chain(fmtdst + (w * h * bpp), w, h, bpp, image_id);
         }

         g_free(src);
         src = fmtdst;
      }

      dxt_compress(dst, src, compression, w, h, bpp, mipmaps,
                   dds_write_vals.color_type, dds_write_vals.dither);

      fwrite(dst, 1, size, fp);

      g_free(dst);
   }

   g_free(src);

   gimp_drawable_detach(drawable);
}

static void write_volume_mipmaps(FILE *fp, gint32 image_id, gint32 *layers,
                                 int w, int h, int d, int bpp, int fmtbpp,
                                 int mipmaps)
{
   int i, size, offset, colors;
   unsigned char *src, *dst, *tmp, *fmtdst;
   unsigned char *palette = 0;
   GimpDrawable *drawable;
   GimpPixelRgn rgn;
   GimpImageBaseType type;

   type = gimp_image_base_type(image_id);

   if(dds_write_vals.compression != DDS_COMPRESS_NONE) return;

   src = g_malloc(w * h * bpp * d);

   if(gimp_image_base_type(image_id) == GIMP_INDEXED)
      palette = gimp_image_get_colormap(image_id, &colors);

   offset = 0;
   for(i = 0; i < d; ++i)
   {
      drawable = gimp_drawable_get(layers[i]);
      gimp_pixel_rgn_init(&rgn, drawable, 0, 0, w, h, 0, 0);
      gimp_pixel_rgn_get_rect(&rgn, src + offset, 0, 0, w, h);
      offset += (w * h * bpp);
      gimp_drawable_detach(drawable);
   }

   if(gimp_drawable_type(layers[0]) == GIMP_INDEXEDA_IMAGE)
   {
      tmp = g_malloc(w * h * d);
      for(i = 0; i < w * h * d; ++i)
         tmp[i] = src[2 * i];
      g_free(src);
      src = tmp;
      bpp = 1;
   }

   /* we want and assume BGRA ordered pixels for bpp >= 3 from here and
      onwards */
   if(bpp >= 3)
      swap_rb(src, w * h * d, bpp);

   /* pre-convert indexed images to RGB for better mipmaps if a
      pixel format conversion is requested */
   if(dds_write_vals.format > DDS_FORMAT_DEFAULT && type == GIMP_INDEXED)
   {
      size = get_volume_mipmapped_size(w, h, d, 3, 0, mipmaps,
                                       DDS_COMPRESS_NONE);
      dst = g_malloc(size);
      convert_pixels(dst, src, DDS_FORMAT_RGB8, w, h, d, bpp, palette, 1);
      g_free(src);
      src = dst;
      bpp = 3;
      palette = NULL;
   }

   size = get_volume_mipmapped_size(w, h, d, bpp, 0, mipmaps,
                                    dds_write_vals.compression);

   dst = g_malloc(size);

   offset = get_volume_mipmapped_size(w, h, d, bpp, 0, 1,
                                      dds_write_vals.compression);

   generate_volume_mipmaps(dst, src, w, h, d, bpp,
                           palette != NULL, mipmaps,
                           dds_write_vals.mipmap_filter,
                           dds_write_vals.gamma_correct,
                           dds_write_vals.gamma);

   if(dds_write_vals.format > DDS_FORMAT_DEFAULT)
   {
      size = get_volume_mipmapped_size(w, h, d, fmtbpp, 0, mipmaps,
                                       dds_write_vals.compression);
      offset = get_volume_mipmapped_size(w, h, d, fmtbpp, 0, 1,
                                         dds_write_vals.compression);
      fmtdst = g_malloc(size);

      convert_pixels(fmtdst, dst, dds_write_vals.format, w, h, d, bpp,
                     palette, mipmaps);
      g_free(dst);
      dst = fmtdst;
   }

   fwrite(dst + offset, 1, size, fp);

   g_free(src);
   g_free(dst);
}

static int write_image(FILE *fp, gint32 image_id, gint32 drawable_id)
{
   GimpDrawable *drawable;
   GimpImageType drawable_type;
   GimpImageBaseType basetype;
   GimpPixelRgn rgn;
   int i, w, h, bpp = 0, fmtbpp = 0, has_alpha = 0;
   int num_mipmaps;
   unsigned char hdr[DDS_HEADERSIZE];
   unsigned int flags = 0, pflags = 0, caps = 0, caps2 = 0, size = 0;
   unsigned int rmask = 0, gmask = 0, bmask = 0, amask = 0;
   unsigned int fourcc = 0;
   gint32 num_layers, *layers;
   guchar *cmap;
   gint colors;
   unsigned char zero[4] = {0, 0, 0, 0};

   layers = gimp_image_get_layers(image_id, &num_layers);

   drawable = gimp_drawable_get(drawable_id);

   w = drawable->width;
   h = drawable->height;

   basetype = gimp_image_base_type(image_id);
   drawable_type = gimp_drawable_type(drawable_id);
   gimp_pixel_rgn_init(&rgn, drawable, 0, 0, w, h, 0, 0);

   switch(drawable_type)
   {
      case GIMP_RGB_IMAGE:      bpp = 3; break;
      case GIMP_RGBA_IMAGE:     bpp = 4; break;
      case GIMP_GRAY_IMAGE:     bpp = 1; break;
      case GIMP_GRAYA_IMAGE:    bpp = 2; break;
      case GIMP_INDEXED_IMAGE:  bpp = 1; break;
      case GIMP_INDEXEDA_IMAGE: bpp = 2; break;
      default:
         break;
   }

   if(dds_write_vals.format > DDS_FORMAT_DEFAULT)
   {
      for(i = 0; ; ++i)
      {
         if(format_info[i].format == dds_write_vals.format)
         {
            fmtbpp = format_info[i].bpp;
            has_alpha = format_info[i].alpha;
            rmask = format_info[i].rmask;
            gmask = format_info[i].gmask;
            bmask = format_info[i].bmask;
            amask = format_info[i].amask;
            break;
         }
      }
   }
   else if(bpp == 1)
   {
      if(basetype == GIMP_INDEXED)
      {
         fmtbpp = 1;
         has_alpha = 0;
         rmask = bmask = gmask = amask = 0;
      }
      else
      {
         fmtbpp = 1;
         has_alpha = 0;
         rmask = 0x000000ff;
         gmask = bmask = amask = 0;
      }
   }
   else if(bpp == 2)
   {
      if(basetype == GIMP_INDEXED)
      {
         fmtbpp = 1;
         has_alpha = 0;
         rmask = gmask = bmask = amask = 0;
      }
      else
      {
         fmtbpp = 2;
         has_alpha = 1;
         rmask = 0x000000ff;
         gmask = 0x000000ff;
         bmask = 0x000000ff;
         amask = 0x0000ff00;
      }
   }
   else if(bpp == 3)
   {
      fmtbpp = 3;
      rmask = 0x00ff0000;
      gmask = 0x0000ff00;
      bmask = 0x000000ff;
      amask = 0x00000000;
   }
   else
   {
      fmtbpp = 4;
      has_alpha = 1;
      rmask = 0x00ff0000;
      gmask = 0x0000ff00;
      bmask = 0x000000ff;
      amask = 0xff000000;
   }

   memset(hdr, 0, DDS_HEADERSIZE);

   PUTL32(hdr,       FOURCC('D','D','S',' '));
   PUTL32(hdr + 4,   124);
   PUTL32(hdr + 12,  h);
   PUTL32(hdr + 16,  w);
   PUTL32(hdr + 76,  32);
   PUTL32(hdr + 88,  fmtbpp << 3);
   PUTL32(hdr + 92,  rmask);
   PUTL32(hdr + 96,  gmask);
   PUTL32(hdr + 100, bmask);
   PUTL32(hdr + 104, amask);

   /*
    put some information in the reserved area to identify the origin
    of the image
   */
   PUTL32(hdr + 32,  FOURCC('G','I','M','P'));
   PUTL32(hdr + 36,  FOURCC('-','D','D','S'));
   PUTL32(hdr + 40,  DDS_PLUGIN_VERSION);

   flags = DDSD_CAPS | DDSD_PIXELFORMAT | DDSD_WIDTH | DDSD_HEIGHT;

   caps = DDSCAPS_TEXTURE;
   if(dds_write_vals.mipmaps)
   {
      flags |= DDSD_MIPMAPCOUNT;
      caps |= (DDSCAPS_COMPLEX | DDSCAPS_MIPMAP);
      if(dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE)
         num_mipmaps = get_num_mipmaps(w, h);
      else
         num_mipmaps = num_layers;
   }
   else
      num_mipmaps = 1;

   if(dds_write_vals.savetype == DDS_SAVE_CUBEMAP && is_cubemap)
   {
      caps |= DDSCAPS_COMPLEX;
      caps2 |= (DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_ALL_FACES);
   }
   else if(dds_write_vals.savetype == DDS_SAVE_VOLUMEMAP && is_volume)
   {
      PUTL32(hdr + 24, num_layers);
      flags |= DDSD_DEPTH;
      caps |= DDSCAPS_COMPLEX;
      caps2 |= DDSCAPS2_VOLUME;
   }

   PUTL32(hdr + 28,  num_mipmaps);
   PUTL32(hdr + 108, caps);
   PUTL32(hdr + 112, caps2);

   if(dds_write_vals.compression == DDS_COMPRESS_NONE)
   {
      flags |= DDSD_PITCH;

      if(dds_write_vals.format > DDS_FORMAT_DEFAULT)
      {
         if(dds_write_vals.format == DDS_FORMAT_A8)
            pflags |= DDPF_ALPHA;
         else
         {
            if((fmtbpp == 1 || dds_write_vals.format == DDS_FORMAT_L8A8) &&
               (dds_write_vals.format != DDS_FORMAT_R3G3B2))
               pflags |= DDPF_LUMINANCE;
            else
               pflags |= DDPF_RGB;
         }
      }
      else
      {
         if(bpp == 1)
         {
            if(basetype == GIMP_INDEXED)
               pflags |= DDPF_PALETTEINDEXED8;
            else
               pflags |= DDPF_LUMINANCE;
         }
         else if(bpp == 2 && basetype == GIMP_INDEXED)
            pflags |= DDPF_PALETTEINDEXED8;
         else
            pflags |= DDPF_RGB;
      }

      if(has_alpha) pflags |= DDPF_ALPHAPIXELS;

      PUTL32(hdr + 8, flags);
      PUTL32(hdr + 20, w * fmtbpp);
      PUTL32(hdr + 80, pflags);

      /*
       write extra fourcc info - this is special to GIMP DDS. When the image
       is read by the plugin, we can detect the added information to decode
       the pixels
      */
      if(dds_write_vals.format == DDS_FORMAT_AEXP)
      {
         PUTL32(hdr + 44, FOURCC('A','E','X','P'));
      }
      else if(dds_write_vals.format == DDS_FORMAT_YCOCG)
      {
         PUTL32(hdr + 44, FOURCC('Y','C','G','1'));
      }
   }
   else
   {
      flags |= DDSD_LINEARSIZE;
      pflags = DDPF_FOURCC;

      switch(dds_write_vals.compression)
      {
         case DDS_COMPRESS_BC1:
            fourcc = FOURCC('D','X','T','1'); break;
         case DDS_COMPRESS_BC2:
            fourcc = FOURCC('D','X','T','3'); break;
         case DDS_COMPRESS_BC3:
         case DDS_COMPRESS_BC3N:
         case DDS_COMPRESS_YCOCG:
         case DDS_COMPRESS_YCOCGS:
         case DDS_COMPRESS_AEXP:
            fourcc = FOURCC('D','X','T','5'); break;
         case DDS_COMPRESS_RXGB:
            fourcc = FOURCC('R','X','G','B'); break;
         case DDS_COMPRESS_BC4:
            fourcc = FOURCC('A','T','I','1'); break;
         case DDS_COMPRESS_BC5:
            fourcc = FOURCC('A','T','I','2'); break;
      }

      if((dds_write_vals.compression == DDS_COMPRESS_BC3N) ||
         (dds_write_vals.compression == DDS_COMPRESS_RXGB))
         pflags |= DDPF_NORMAL;

      PUTL32(hdr + 8,  flags);
      PUTL32(hdr + 80, pflags);
      PUTL32(hdr + 84, fourcc);

      size = ((w + 3) >> 2) * ((h + 3) >> 2);
      if(dds_write_vals.compression == DDS_COMPRESS_BC1 ||
         dds_write_vals.compression == DDS_COMPRESS_BC4)
         size *= 8;
      else
         size *= 16;

      PUTL32(hdr + 20, size);

      /*
       write extra fourcc info - this is special to GIMP DDS. When the image
       is read by the plugin, we can detect the added information to decode
       the pixels
      */
      if(dds_write_vals.compression == DDS_COMPRESS_AEXP)
      {
         PUTL32(hdr + 44, FOURCC('A','E','X','P'));
      }
      else if(dds_write_vals.compression == DDS_COMPRESS_YCOCG)
      {
         PUTL32(hdr + 44, FOURCC('Y','C','G','1'));
      }
      else if(dds_write_vals.compression == DDS_COMPRESS_YCOCGS)
      {
         PUTL32(hdr + 44, FOURCC('Y','C','G','2'));
      }
   }

   fwrite(hdr, DDS_HEADERSIZE, 1, fp);

   if(basetype == GIMP_INDEXED && dds_write_vals.format == DDS_FORMAT_DEFAULT &&
      dds_write_vals.compression == DDS_COMPRESS_NONE)
   {
      cmap = gimp_image_get_colormap(image_id, &colors);
      for(i = 0; i < colors; ++i)
      {
         fwrite(&cmap[3 * i], 1, 3, fp);
         if(i == dds_write_vals.transindex)
            fputc(0, fp);
         else
            fputc(255, fp);
      }
      for(; i < 256; ++i)
         fwrite(zero, 1, 4, fp);
   }

   if(dds_write_vals.savetype == DDS_SAVE_CUBEMAP)
   {
      for(i = 0; i < 6; ++i)
      {
         write_layer(fp, image_id, cubemap_faces[i], w, h, bpp, fmtbpp,
                     num_mipmaps);
         if(interactive_dds)
            gimp_progress_update((float)(i + 1) / 6.0);
      }
   }
   else if(dds_write_vals.savetype == DDS_SAVE_VOLUMEMAP)
   {
      for(i = 0; i < num_layers; ++i)
      {
         write_layer(fp, image_id, layers[i], w, h, bpp, fmtbpp, 1);
         if(interactive_dds)
            gimp_progress_update((float)i / (float)num_layers);
      }

      if(num_mipmaps > 1)
         write_volume_mipmaps(fp, image_id, layers, w, h, num_layers,
                              bpp, fmtbpp, num_mipmaps);
   }
   else
   {
      write_layer(fp, image_id, drawable_id, w, h, bpp, fmtbpp,
                  num_mipmaps);
   }

   if(interactive_dds)
      gimp_progress_update(1.0);

   gimp_drawable_detach(drawable);

   return(1);
}

static GtkWidget *string_value_combo_new(string_value_t *strings,
                                         int active_value)
{
   GtkWidget *opt;
   GtkCellRenderer *renderer;
   GtkListStore *store;
   GtkTreeIter iter;
   int i, active = 0;

   store = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_BOOLEAN);
   for(i = 0; strings[i].string; ++i)
   {
      if(strings[i].value == active_value) active = i;
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter,
                         0, strings[i].value,
                         1, strings[i].string,
                         2, 1,
                         -1);
   }

   renderer = gtk_cell_renderer_text_new();

   opt = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(opt), renderer, 1);
   gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(opt), renderer,
                                  "text", COMBO_STRING,
                                  "sensitive", COMBO_SENSITIVE,
                                  NULL);

   gtk_combo_box_set_active(GTK_COMBO_BOX(opt), active);

   g_object_unref(store);

   return(opt);
}

static void string_value_combo_selected(GtkWidget *widget, gpointer data)
{
   int value;
   GtkTreeIter iter;
   GtkTreeModel *model;

   model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
   gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &iter);
   gtk_tree_model_get(model, &iter, COMBO_VALUE, &value, -1);

   *((int *)data) = value;
}

static void string_value_combo_set_item_sensitive(GtkWidget *widget,
                                                  int value, int sensitive)
{
   GtkTreeIter iter;
   GtkTreeModel *model;
   int val;

   model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
   gtk_tree_model_get_iter_first(model, &iter);
   do
   {
      gtk_tree_model_get(model, &iter, COMBO_VALUE, &val, -1);
      if(val == value)
      {
         gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                            COMBO_SENSITIVE, sensitive, -1);
         break;
      }
   } while(gtk_tree_model_iter_next(model, &iter));
}

static void string_value_combo_set_active(GtkWidget *widget,
                                          int value)
{
   GtkTreeIter iter;
   GtkTreeModel *model;
   int val;

   model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
   gtk_tree_model_get_iter_first(model, &iter);
   do
   {
      gtk_tree_model_get(model, &iter, COMBO_VALUE, &val, -1);
      if(val == value)
      {
         gtk_combo_box_set_active_iter(GTK_COMBO_BOX(widget), &iter);
         break;
      }
   } while(gtk_tree_model_iter_next(model, &iter));
}

static void save_dialog_response(GtkWidget *widget, gint response_id,
                                 gpointer data)
{
   switch(response_id)
   {
      case GTK_RESPONSE_OK:
         runme = 1;
      default:
         gtk_widget_destroy(widget);
         break;
   }
}

static void compression_selected(GtkWidget *widget, gpointer data)
{
   GtkTreeIter iter;
   GtkTreeModel *model;
   model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
   gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &iter);
   gtk_tree_model_get(model, &iter, COMBO_VALUE,
                      &dds_write_vals.compression, -1);

   gtk_widget_set_sensitive(format_opt, dds_write_vals.compression == DDS_COMPRESS_NONE);
   gtk_widget_set_sensitive(color_type_opt,
                            (dds_write_vals.compression != DDS_COMPRESS_NONE) &&
                            (dds_write_vals.compression != DDS_COMPRESS_BC4) &&
                            (dds_write_vals.compression != DDS_COMPRESS_BC5) &&
                            (dds_write_vals.compression != DDS_COMPRESS_YCOCGS));
   gtk_widget_set_sensitive(dither_chk,
                            (dds_write_vals.compression != DDS_COMPRESS_NONE) &&
                            (dds_write_vals.compression != DDS_COMPRESS_BC4) &&
                            (dds_write_vals.compression != DDS_COMPRESS_BC5) &&
                            (dds_write_vals.compression != DDS_COMPRESS_YCOCGS));
}

static void savetype_selected(GtkWidget *widget, gpointer data)
{
   dds_write_vals.savetype = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

   switch(dds_write_vals.savetype)
   {
      case DDS_SAVE_SELECTED_LAYER:
      case DDS_SAVE_CUBEMAP:
         gtk_widget_set_sensitive(compress_opt, 1);
         string_value_combo_set_item_sensitive(mipmap_filter_opt,
                                               DDS_MIPMAP_FILTER_LANCZOS,
                                               1);
         break;
      case DDS_SAVE_VOLUMEMAP:
         dds_write_vals.compression = DDS_COMPRESS_NONE;
         gtk_combo_box_set_active(GTK_COMBO_BOX(compress_opt),
                                  DDS_COMPRESS_NONE);
         gtk_widget_set_sensitive(compress_opt, 0);
         if(dds_write_vals.mipmap_filter == DDS_MIPMAP_FILTER_LANCZOS)
            dds_write_vals.mipmap_filter = DDS_MIPMAP_FILTER_DEFAULT;
         string_value_combo_set_active(mipmap_filter_opt,
                                       dds_write_vals.mipmap_filter);
         string_value_combo_set_item_sensitive(mipmap_filter_opt,
                                               DDS_MIPMAP_FILTER_LANCZOS,
                                               0);
         break;
   }
}

static void mipmaps_selected(GtkWidget *widget, gpointer data)
{
   GtkTreeIter iter;
   GtkTreeModel *model;
   model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
   gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &iter);
   gtk_tree_model_get(model, &iter, COMBO_VALUE,
                      &dds_write_vals.mipmaps, -1);

   gtk_widget_set_sensitive(mipmap_filter_opt, dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE);
   gtk_widget_set_sensitive(gamma_chk, dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE);
   gtk_widget_set_sensitive(gamma_spin, (dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE) && dds_write_vals.gamma_correct);
}

static void toggle_clicked(GtkWidget *widget, gpointer data)
{
   int *flag = (int *)data;
   (*flag) = !(*flag);
}

static void transindex_clicked(GtkWidget *widget, gpointer data)
{
   GtkWidget *spin = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "spin"));

   if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
   {
      dds_write_vals.transindex = 0;
      gtk_widget_set_sensitive(spin, 1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 0);
   }
   else
   {
      gtk_widget_set_sensitive(spin, 0);
      dds_write_vals.transindex = -1;
   }
}

static void transindex_changed(GtkWidget *widget, gpointer data)
{
   dds_write_vals.transindex = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

static void adv_opt_expanded(GtkWidget *widget, gpointer data)
{
   dds_write_vals.show_adv_opt = !gtk_expander_get_expanded(GTK_EXPANDER(widget));
}

static void gamma_correct_clicked(GtkWidget *widget, gpointer data)
{
   dds_write_vals.gamma_correct = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
   gtk_widget_set_sensitive(gamma_spin, dds_write_vals.gamma_correct);
}

static void gamma_changed(GtkWidget *widget, gpointer data)
{
   dds_write_vals.gamma = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
}

static gint save_dialog(gint32 image_id, gint32 drawable_id)
{
   GtkWidget *dlg;
   GtkWidget *vbox, *hbox;
   GtkWidget *table;
   GtkWidget *label;
   GtkWidget *opt;
   GtkWidget *check;
   GtkWidget *spin;
   GtkWidget *expander;
   GimpImageBaseType basetype;

   if(is_cubemap)
      dds_write_vals.savetype = DDS_SAVE_CUBEMAP;
   else if(is_volume)
      dds_write_vals.savetype = DDS_SAVE_VOLUMEMAP;
   else
      dds_write_vals.savetype = DDS_SAVE_SELECTED_LAYER;

   basetype = gimp_image_base_type(image_id);

   dlg = gimp_dialog_new("Save as DDS", "dds", NULL, GTK_WIN_POS_MOUSE,
                         gimp_standard_help_func, SAVE_PROC,
                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                         NULL);

   gtk_signal_connect(GTK_OBJECT(dlg), "response",
                      GTK_SIGNAL_FUNC(save_dialog_response),
                      0);
   gtk_signal_connect(GTK_OBJECT(dlg), "destroy",
                      GTK_SIGNAL_FUNC(gtk_main_quit),
                      0);

   gtk_window_set_resizable(GTK_WINDOW(dlg), 0);

   vbox = gtk_vbox_new(0, 8);
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), vbox, 1, 1, 0);
   gtk_widget_show(vbox);

   table = gtk_table_new(4, 2, 0);
   gtk_widget_show(table);
   gtk_box_pack_start(GTK_BOX(vbox), table, 1, 1, 0);
   gtk_table_set_row_spacings(GTK_TABLE(table), 8);
   gtk_table_set_col_spacings(GTK_TABLE(table), 8);

   label = gtk_label_new("Compression:");
   gtk_widget_show(label);
   gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

   opt = string_value_combo_new(compression_strings,
                                dds_write_vals.compression);
   gtk_widget_show(opt);
   gtk_table_attach(GTK_TABLE(table), opt, 1, 2, 0, 1,
                    (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions)(GTK_EXPAND), 0, 0);

   gtk_signal_connect(GTK_OBJECT(opt), "changed",
                      GTK_SIGNAL_FUNC(compression_selected), 0);

   compress_opt = opt;

   label = gtk_label_new("Format:");
   gtk_widget_show(label);
   gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

   opt = string_value_combo_new(format_strings, dds_write_vals.format);
   gtk_widget_show(opt);
   gtk_table_attach(GTK_TABLE(table), opt, 1, 2, 1, 2,
                    (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions)(GTK_EXPAND), 0, 0);

   gtk_signal_connect(GTK_OBJECT(opt), "changed",
                      GTK_SIGNAL_FUNC(string_value_combo_selected),
                      &dds_write_vals.format);

   gtk_widget_set_sensitive(opt, dds_write_vals.compression == DDS_COMPRESS_NONE);

   format_opt = opt;

   label = gtk_label_new("Save:");
   gtk_widget_show(label);
   gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

   opt = string_value_combo_new(save_type_strings, dds_write_vals.savetype);
   gtk_widget_show(opt);
   gtk_table_attach(GTK_TABLE(table), opt, 1, 2, 2, 3,
                    (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions)(GTK_EXPAND), 0, 0);

   gtk_signal_connect(GTK_OBJECT(opt), "changed",
                      GTK_SIGNAL_FUNC(savetype_selected), 0);

   string_value_combo_set_item_sensitive(opt, DDS_SAVE_CUBEMAP, is_cubemap);
   string_value_combo_set_item_sensitive(opt, DDS_SAVE_VOLUMEMAP, is_volume);

   gtk_widget_set_sensitive(opt, is_cubemap || is_volume);

   label = gtk_label_new("Mipmaps:");
   gtk_widget_show(label);
   gtk_table_attach(GTK_TABLE(table), label, 0, 1, 3, 4,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

   opt = string_value_combo_new(mipmap_strings,
                                dds_write_vals.mipmaps);
   gtk_widget_show(opt);
   gtk_table_attach(GTK_TABLE(table), opt, 1, 2, 3, 4,
                    (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions)(GTK_EXPAND), 0, 0);

   gtk_signal_connect(GTK_OBJECT(opt), "changed",
                      GTK_SIGNAL_FUNC(mipmaps_selected), 0);

   string_value_combo_set_item_sensitive(opt, DDS_MIPMAP_EXISTING,
                                         !(is_volume || is_cubemap) &&
                                         is_mipmap_chain_valid);


   hbox = gtk_hbox_new(0, 8);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, 1, 1, 0);
   gtk_widget_show(hbox);

   check = gtk_check_button_new_with_label("Transparent index:");
   gtk_box_pack_start(GTK_BOX(hbox), check, 0, 0, 0);
   gtk_signal_connect(GTK_OBJECT(check), "clicked",
                      GTK_SIGNAL_FUNC(transindex_clicked), 0);
   gtk_widget_show(check);

   spin = gtk_spin_button_new(GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 255, 1, 1, 0)), 1, 0);
   gtk_box_pack_start(GTK_BOX(hbox), spin, 1, 1, 0);
   gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin),
                                     GTK_UPDATE_IF_VALID);
   gtk_signal_connect(GTK_OBJECT(spin), "value_changed",
                      GTK_SIGNAL_FUNC(transindex_changed), 0);
   gtk_widget_show(spin);

   g_object_set_data(G_OBJECT(check), "spin", spin);

   if(basetype != GIMP_INDEXED)
   {
      gtk_widget_set_sensitive(check, 0);
      gtk_widget_set_sensitive(spin, 0);
   }
   else if(dds_write_vals.transindex < 0)
   {
      gtk_widget_set_sensitive(spin, 0);
   }
   else if(dds_write_vals.transindex >= 0)
   {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), 1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), dds_write_vals.transindex);
   }

   if(is_volume && dds_write_vals.savetype == DDS_SAVE_VOLUMEMAP)
   {
      dds_write_vals.compression = DDS_COMPRESS_NONE;
      string_value_combo_set_active(compress_opt, DDS_COMPRESS_NONE);
      gtk_widget_set_sensitive(compress_opt, 0);
   }

   expander = gtk_expander_new("<b>Advanced Options</b>");
   gtk_expander_set_use_markup(GTK_EXPANDER(expander), 1);
   gtk_expander_set_expanded(GTK_EXPANDER(expander), dds_write_vals.show_adv_opt);
   gtk_expander_set_spacing(GTK_EXPANDER(expander), 8);
   gtk_signal_connect(GTK_OBJECT(expander), "activate",
                      GTK_SIGNAL_FUNC(adv_opt_expanded), 0);
   gtk_box_pack_start(GTK_BOX(vbox), expander, 1, 1, 0);
   gtk_widget_show(expander);

   table = gtk_table_new(5, 2, 0);
   gtk_table_set_row_spacings(GTK_TABLE(table), 8);
   gtk_table_set_col_spacings(GTK_TABLE(table), 8);
   gtk_container_add(GTK_CONTAINER(expander), table);
   gtk_widget_show(table);

   label = gtk_label_new("Color selection:");
   gtk_widget_show(label);
   gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

   opt = string_value_combo_new(color_type_strings, dds_write_vals.color_type);
   gtk_widget_show(opt);
   gtk_table_attach(GTK_TABLE(table), opt, 1, 2, 0, 1,
                    (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions)(GTK_EXPAND), 0, 0);

   gtk_signal_connect(GTK_OBJECT(opt), "changed",
                      GTK_SIGNAL_FUNC(string_value_combo_selected),
                      &dds_write_vals.color_type);

   color_type_opt = opt;

   check = gtk_check_button_new_with_label("Use dithering");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), dds_write_vals.dither);
   gtk_table_attach(GTK_TABLE(table), check, 0, 2, 1, 2,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_signal_connect(GTK_OBJECT(check), "clicked",
                      GTK_SIGNAL_FUNC(toggle_clicked), &dds_write_vals.dither);
   gtk_widget_show(check);

   dither_chk = check;

   label = gtk_label_new("Mipmap filter:");
   gtk_widget_show(label);
   gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

   opt = string_value_combo_new(mipmap_filter_strings,
                                dds_write_vals.mipmap_filter);
   gtk_widget_show(opt);
   gtk_table_attach(GTK_TABLE(table), opt, 1, 2, 2, 3,
                    (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions)(GTK_EXPAND), 0, 0);

   if(is_volume && dds_write_vals.savetype == DDS_SAVE_VOLUMEMAP)
      string_value_combo_set_item_sensitive(opt, DDS_MIPMAP_FILTER_LANCZOS, 0);

   gtk_signal_connect(GTK_OBJECT(opt), "changed",
                      GTK_SIGNAL_FUNC(string_value_combo_selected),
                      &dds_write_vals.mipmap_filter);

   mipmap_filter_opt = opt;

   check = gtk_check_button_new_with_label("Gamma-correct mipmaps");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), dds_write_vals.gamma_correct && dds_write_vals.mipmaps);
   gtk_table_attach(GTK_TABLE(table), check, 0, 2, 3, 4,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_signal_connect(GTK_OBJECT(check), "clicked",
                      GTK_SIGNAL_FUNC(gamma_correct_clicked), NULL);
   gtk_widget_show(check);

   gamma_chk = check;

   label = gtk_label_new("Gamma:");
   gtk_widget_show(label);
   gtk_table_attach(GTK_TABLE(table), label, 0, 1, 4, 5,
                    (GtkAttachOptions)(GTK_FILL),
                    (GtkAttachOptions)(0), 0, 0);
   gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

   spin = gtk_spin_button_new(GTK_ADJUSTMENT(gtk_adjustment_new(dds_write_vals.gamma, 1e-05, 100, 0.1, 0.5, 0)), 1, 1);
   gtk_table_attach(GTK_TABLE(table), spin, 1, 2, 4, 5,
                    (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions)(GTK_EXPAND), 0, 0);
   gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_IF_VALID);
   gtk_signal_connect(GTK_OBJECT(spin), "value_changed",
                      GTK_SIGNAL_FUNC(gamma_changed), 0);
   gtk_widget_show(spin);

   gamma_spin = spin;

   gtk_widget_set_sensitive(color_type_opt,
                            (dds_write_vals.compression != DDS_COMPRESS_NONE) &&
                            (dds_write_vals.compression != DDS_COMPRESS_BC4) &&
                            (dds_write_vals.compression != DDS_COMPRESS_BC5) &&
                            (dds_write_vals.compression != DDS_COMPRESS_YCOCGS));
   gtk_widget_set_sensitive(dither_chk,
                            (dds_write_vals.compression != DDS_COMPRESS_NONE) &&
                            (dds_write_vals.compression != DDS_COMPRESS_BC4) &&
                            (dds_write_vals.compression != DDS_COMPRESS_BC5) &&
                            (dds_write_vals.compression != DDS_COMPRESS_YCOCGS));
   gtk_widget_set_sensitive(mipmap_filter_opt, dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE);
   gtk_widget_set_sensitive(gamma_chk, dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE);
   gtk_widget_set_sensitive(gamma_spin, (dds_write_vals.mipmaps == DDS_MIPMAP_GENERATE) && dds_write_vals.gamma_correct);

   gtk_widget_show(dlg);

   runme = 0;

   gtk_main();

   return(runme);
}
