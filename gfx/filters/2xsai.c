/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

// Compile: gcc -o twoxsai.so -shared twoxsai.c -std=c99 -O3 -Wall -pedantic -fPIC

#include "softfilter.h"
#include <stdlib.h>

#ifdef RARCH_INTERNAL
#define softfilter_get_implementation twoxsai_get_implementation
#endif

#define TWOXSAI_SCALE 2

static unsigned twoxsai_generic_input_fmts(void)
{
   return SOFTFILTER_FMT_RGB565;
}

static unsigned twoxsai_generic_output_fmts(unsigned input_fmts)
{
   return input_fmts;
}

static unsigned twoxsai_generic_threads(void *data)
{
   struct filter_data *filt = (struct filter_data*)data;
   return filt->threads;
}

static void *twoxsai_generic_create(unsigned in_fmt, unsigned out_fmt,
      unsigned max_width, unsigned max_height,
      unsigned threads, softfilter_simd_mask_t simd)
{
   (void)simd;

   struct filter_data *filt = (struct filter_data*)calloc(1, sizeof(*filt));
   if (!filt)
      return NULL;
   filt->workers = (struct softfilter_thread_data*)calloc(threads, sizeof(struct softfilter_thread_data));
   filt->threads = threads;
   filt->in_fmt  = in_fmt;
   if (!filt->workers)
   {
      free(filt);
      return NULL;
   }
   return filt;
}

static void twoxsai_generic_output(void *data, unsigned *out_width, unsigned *out_height,
      unsigned width, unsigned height)
{
   *out_width = width * TWOXSAI_SCALE;
   *out_height = height * TWOXSAI_SCALE;
}

static void twoxsai_generic_destroy(void *data)
{
   struct filter_data *filt = (struct filter_data*)data;
   free(filt->workers);
   free(filt);
}

#define twoxsai_interpolate_rgb565(A, B) ((((A) & 0xF7DE) >> 1) + (((B) & 0xF7DE) >> 1) + ((A) & (B) & 0x0821));

#define twoxsai_interpolate2_rgb565(A, B, C, D) ((((A) & 0xE79C) >> 2) + (((B) & 0xE79C) >> 2) + (((C) & 0xE79C) >> 2) + (((D) & 0xE79C) >> 2)  + (((((A) & 0x1863) + ((B) & 0x1863) + ((C) & 0x1863) + ((D) & 0x1863)) >> 2) & 0x1863))

#define twoxsai_result1_rgb565(A, B, C, D) (((A) != (C) || (A) != (D)) - ((B) != (C) || (B) != (D)));

#define twoxsai_declare_variables(typename_t, in, nextline) \
         typename_t product, product1, product2; \
         typename_t colorI = *(in - nextline - 1); \
         typename_t colorE = *(in - nextline + 0); \
         typename_t colorF = *(in - nextline + 1); \
         typename_t colorJ = *(in - nextline + 2); \
         typename_t colorG = *(in - 1); \
         typename_t colorA = *(in + 0); \
         typename_t colorB = *(in + 1); \
         typename_t colorK = *(in + 2); \
         typename_t colorH = *(in + nextline - 1); \
         typename_t colorC = *(in + nextline + 0); \
         typename_t colorD = *(in + nextline + 1); \
         typename_t colorL = *(in + nextline + 2); \
         typename_t colorM = *(in + nextline + nextline - 1); \
         typename_t colorN = *(in + nextline + nextline + 0); \
         typename_t colorO = *(in + nextline + nextline + 1); \
         //typename_t colorP = *(in + nextline + nextline + 2);

static void twoxsai_generic_rgb565(unsigned width, unsigned height,
      int first, int last, uint16_t *src, 
      unsigned src_stride, uint16_t *dst, unsigned dst_stride)
{
   const unsigned nextline = (last) ? 0 : src_stride;

   for (; height; height--)
   {
      uint16_t *in  = (uint16_t*)src;
      uint16_t *out = (uint16_t*)dst;

      for (unsigned finish = width; finish; finish -= 1)
      {
         twoxsai_declare_variables(uint16_t, in, nextline);

         //---------------------------------------
         // Map of the pixels:           I|E F|J
         //                              G|A B|K
         //                              H|C D|L
         //                              M|N O|P

         if (colorA == colorD && colorB != colorC)
         {
            if ((colorA == colorE && colorB == colorL) || (colorA == colorC && colorA == colorF && colorB != colorE && colorB == colorJ))
               product = colorA;
            else
            {
               product = twoxsai_interpolate_rgb565(colorA, colorB);
            }

            if ((colorA == colorG && colorC == colorO) || (colorA == colorB && colorA == colorH && colorG != colorC && colorC == colorM))
               product1 = colorA;
            else
            {
               product1 = twoxsai_interpolate_rgb565(colorA, colorC);
            }

            product2 = colorA;
         } else if (colorB == colorC && colorA != colorD)
         {
            if ((colorB == colorF && colorA == colorH) || (colorB == colorE && colorB == colorD && colorA != colorF && colorA == colorI))
               product = colorB;
            else
            {
               product = twoxsai_interpolate_rgb565(colorA, colorB);
            }

            if ((colorC == colorH && colorA == colorF) || (colorC == colorG && colorC == colorD && colorA != colorH && colorA == colorI))
               product1 = colorC;
            else
            {
               product1 = twoxsai_interpolate_rgb565(colorA, colorC);
            }

            product2 = colorB;
         }
         else if (colorA == colorD && colorB == colorC)
         {
            if (colorA == colorB)
            {
               product  = colorA;
               product1 = colorA;
               product2 = colorA;
            }
            else
            {
               int r = 0;
               product1 = twoxsai_interpolate_rgb565(colorA, colorC);
               product  = twoxsai_interpolate_rgb565(colorA, colorB);

               r += twoxsai_result1_rgb565(colorA, colorB, colorG, colorE);
               r += twoxsai_result1_rgb565(colorB, colorA, colorK, colorF);
               r += twoxsai_result1_rgb565(colorB, colorA, colorH, colorN);
               r += twoxsai_result1_rgb565(colorA, colorB, colorL, colorO);

               if (r > 0)
                  product2 = colorA;
               else if (r < 0)
                  product2 = colorB;
               else
               {
                  product2 = twoxsai_interpolate2_rgb565(colorA, colorB, colorC, colorD);
               }
            }
         }
         else
         {
            product2 = twoxsai_interpolate2_rgb565(colorA, colorB, colorC, colorD);

            if (colorA == colorC && colorA == colorF && colorB != colorE && colorB == colorJ)
               product = colorA;
            else if (colorB == colorE && colorB == colorD && colorA != colorF && colorA == colorI)
               product = colorB;
            else
            {
               product = twoxsai_interpolate_rgb565(colorA, colorB);
            }

            if (colorA == colorB && colorA == colorH && colorG != colorC && colorC == colorM)
               product1 = colorA;
            else if (colorC == colorG && colorC == colorD && colorA != colorH && colorA == colorI)
               product1 = colorC;
            else
            {
               product1 = twoxsai_interpolate_rgb565(colorA, colorC);
            }
         }

         out[0] = colorA;
         out[1] = product;
         out[dst_stride] = product1;
         out[dst_stride + 1] = product2;

         ++in;
         out += 2;
      }

      src += src_stride;
      dst += 2 * dst_stride;
   }
}

static void twoxsai_work_cb_rgb565(void *data, void *thread_data)
{
   struct softfilter_thread_data *thr = (struct softfilter_thread_data*)thread_data;
   uint16_t *input = (uint16_t*)thr->in_data;
   uint16_t *output = (uint16_t*)thr->out_data;
   unsigned width = thr->width;
   unsigned height = thr->height;

   twoxsai_generic_rgb565(width, height,
         thr->first, thr->last, input, thr->in_pitch / SOFTFILTER_BPP_RGB565, output, thr->out_pitch / SOFTFILTER_BPP_RGB565);
}

static void twoxsai_generic_packets(void *data,
      struct softfilter_work_packet *packets,
      void *output, size_t output_stride,
      const void *input, unsigned width, unsigned height, size_t input_stride)
{
   struct filter_data *filt = (struct filter_data*)data;
   unsigned i;
   for (i = 0; i < filt->threads; i++)
   {
      struct softfilter_thread_data *thr = (struct softfilter_thread_data*)&filt->workers[i];

      unsigned y_start = (height * i) / filt->threads;
      unsigned y_end = (height * (i + 1)) / filt->threads;
      thr->out_data = (uint8_t*)output + y_start * TWOXSAI_SCALE * output_stride;
      thr->in_data = (const uint8_t*)input + y_start * input_stride;
      thr->out_pitch = output_stride;
      thr->in_pitch = input_stride;
      thr->width = width;
      thr->height = y_end - y_start;

      // Workers need to know if they can access pixels outside their given buffer.
      thr->first = y_start;
      thr->last = y_end == height;

      if (filt->in_fmt == SOFTFILTER_FMT_RGB565)
         packets[i].work = twoxsai_work_cb_rgb565;
      packets[i].thread_data = thr;
   }
}

static const struct softfilter_implementation twoxsai_generic = {
   twoxsai_generic_input_fmts,
   twoxsai_generic_output_fmts,

   twoxsai_generic_create,
   twoxsai_generic_destroy,

   twoxsai_generic_threads,
   twoxsai_generic_output,
   twoxsai_generic_packets,
   "2xSaI",
   SOFTFILTER_API_VERSION,
};

const struct softfilter_implementation *softfilter_get_implementation(softfilter_simd_mask_t simd)
{
   (void)simd;
   return &twoxsai_generic;
}

#ifdef RARCH_INTERNAL
#undef softfilter_get_implementation
#endif
