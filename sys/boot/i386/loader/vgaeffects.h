/*-
 * Copyright (c) 2013 Claudio D. Lapilli
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef VGA_EFFECTS_H

#define VGA_EFFECTS_H

#define peek(seg,off) (*(unsigned short *)( ((seg)<<4) + (off)))
#define peekb(seg,off) (*(unsigned char *)( ((seg)<<4) + (off)))
#define poke(seg,off,val) (*(unsigned short *)( ((seg)<<4) + (off)))=(unsigned short)(val)

/* These routines are provided by the loader */
extern int		isa_inb(int port);
extern void	isa_outb(int port, int value);

struct textmode_bitmap ;
struct bitmap_glyphs;

void vga_init();
void vga_showbanner();
void vga_print(int x,int y,unsigned int attr,char *string);
void vga_clrlin(int y,int nlines,unsigned char attr);
void vga_printlogo(int x,int y,struct textmode_bitmap *logo, struct bitmap_glyphs *glyphs);
void vga_unmapfonts();
void vga_mapfonts();
void vga_slidedown(int totallines);
void vga_slideupandexit();
void vga_setpalette();


#endif
