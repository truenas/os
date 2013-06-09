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

#include "pcbsd_logo.c"

#include "vgaeffects.h"

#define NUMLINES 4
#define FRAMES   30
#define SCREEN_PTR 0xbe000
#define MEM_BASE     0xa000

/* This can be considered a constant during boot */
int ScreenWidth=80;
unsigned char vga_savedregisters[6];
unsigned char vga_savedpalette[16*4];

void vga_clrlin(int y,int nlines,unsigned char attr)
{
int i,j;

/* Provide appropriate safety */
if(nlines<=0) return;
if(y+nlines<0) return;
if(y<0) { nlines+=y; y=0; }
if(y>24) return;
if(y+nlines>24) nlines=25-y;

unsigned short *ptr=(unsigned short *)((SCREEN_PTR-MEM_BASE)+(ScreenWidth*2*y));

for(i=0;i<nlines;++i)
{
    for(j=0;j<ScreenWidth;++j)
    {
        *ptr++=(attr<<8)|0x20;
    }
}

}

void vga_print(int x,int y,unsigned int attr,char *string)
{
unsigned char *ap=(unsigned char *)(SCREEN_PTR-MEM_BASE),*ap2;

attr&=0xff;


ap+=(ScreenWidth*y+x)<<1;
ap2=(unsigned char *)string;
if(!attr) {
while(*ap2!=0) {
*ap=*ap2;
++ap;
++ap;
++ap2;
}
}
else {
while(*ap2!=0) {
*ap=*ap2;
++ap;
*ap=attr;
++ap;
++ap2;
}
}

}

void vga_sync()
{
    while((isa_inb(0x3da)&8));
    while(!(isa_inb(0x3da)&8));

}


void vga_initpalette()
{
    /* Set palette to standard colors */

     static char palette_values[16*3]= {
         0, 0, 0, /* black */
         0, 0, 42, /* blue */
         0, 42, 0, /* green */
         0, 42, 42, /* cyan */
         42, 0, 0,  /* red */
         42, 0, 42, /* magenta */
         42, 21, 0, /* brown */
         42, 42, 42, /* white */
         21, 21, 21, /* bright black */
         21, 21, 63, /* bright blue */
         21, 63, 21, /* bright green */
         21, 63, 63, /* bright cyan */
         63, 21, 21, /* bright red */
         63, 21, 63, /* bright magenta */
         63, 63, 21, /* yellow */
         63, 63, 63  /* bright white */
     };

    int f;
    /* Set all palette registers to first 15 vga colors */
    for(f=0;f<16;++f)
    {
    v86.ctl = 0;
    v86.eax = 0x1000;
    v86.ebx = f | (f<<8);
    v86.addr=0x10;
    v86int();
    }

    /* Set all palette colors to default */
    for(f=0;f<16;++f)
    {
    v86.ctl = 0;
    v86.eax = 0x1010;
    v86.ebx = f;
    v86.ecx = (palette_values[f*3+1]<<8)| palette_values[f*3+2];
    v86.edx = palette_values[f*3]<<8;
    v86.addr=0x10;
    v86int();
    }

}


void vga_init()
{
    vga_sync();
    isa_inb(0x3da);
    isa_outb(0x3c0,0x30);
    vga_savedregisters[5]=isa_inb(0x3c1);
    isa_inb(0x3da);

    /* Get Screen size from BIOS */
    ScreenWidth=80; //(int)peekb(0,0x44a);


    /* Save VGA registers to restore later */
    isa_outb(0x3d4,0x18);
    vga_savedregisters[0]=isa_inb(0x3d5);
    isa_outb(0x3d4,7);
    vga_savedregisters[1]=isa_inb(0x3d5);
    isa_outb(0x3d4,9);
    vga_savedregisters[2]=isa_inb(0x3d5);
    isa_outb(0x3d4,0xc);
    vga_savedregisters[3]=isa_inb(0x3d5);
    isa_outb(0x3d4,0xd);
    vga_savedregisters[4]=isa_inb(0x3d5);


    while((isa_inb(0x3da)&8));
    while(!(isa_inb(0x3da)&8));



    /* Turn off blink mode to use bright backgrounds */
    v86.ctl=0;
    v86.eax=0x1003;
    v86.ebx=0;
    v86.addr=0x10;
    v86int();


    /* Set Line Compare register to enable split screen operation */
    /* Start with line 0 so banner is out of screen */
    int scanline=0;
    isa_outb(0x3d4,0x18);
    isa_outb(0x3d5,scanline&0xff);
    isa_outb(0x3d4,7);
    isa_outb(0x3d5,(isa_inb(0x3d5)&0xef)|((scanline>>4)&0x10));
    isa_outb(0x3d4,9);
    isa_outb(0x3d5,(isa_inb(0x3d5)&0xbf)|((scanline>>3)&0x40));

    /* Change start address to use a higher page of memory for the upper portion */
    isa_outb(0x3d4,0xc);
    isa_outb(0x3d5,0x30);
    isa_outb(0x3d4,0xd);
    isa_outb(0x3d5,0);

    /* Set 8-bit wide characters */

    isa_outb(0x3c4,1);
    isa_outb(0x3c5,isa_inb(0x3c5)|1);

    vga_initpalette();

}


void vga_showbanner()
{

/* Clear background */
vga_clrlin(0,1,0xf0);
vga_clrlin(1,NUMLINES-1,0xe0);
//vga_clrlin(NUMLINES,1,0x80);

vga_printlogo(50,0,&pcbsd_logo,&pcbsd_logo_font);


vga_slidedown(NUMLINES);


}

/* Slide the menu from the banner down */

void vga_slidedown(int totallines)
{
    int Const;
    int CharHeight=16;
    int startscan;
    int scanline;
    int framecnt;
    /* Slide the banner in */

    isa_outb(0x3d4,0x18);
    startscan=isa_inb(0x3d5);
    isa_outb(0x3d4,7);
    startscan|=((int)(isa_inb(0x3d5)&0x10))<<4;
    isa_outb(0x3d4,9);
    startscan|=((int)(isa_inb(0x3d5)&0x40))<<3;
    ++startscan;

    /* This constant is for accelerated motion */
    /* Use fixed point for accuracy */
    Const=((totallines*CharHeight-startscan)<<16)/(FRAMES*FRAMES);
    for(framecnt=0;framecnt<=FRAMES;framecnt++)
    {
    scanline=(((framecnt*framecnt)*Const)>>16)+startscan;
    /* Sync with vertical refresh */
    while((isa_inb(0x3da)&8));
    while(!(isa_inb(0x3da)&8));

    /* Set the split screen position */
    isa_outb(0x3d4,0x18);
    isa_outb(0x3d5,scanline&0xff);
    isa_outb(0x3d4,7);
    isa_outb(0x3d5,(isa_inb(0x3d5)&0xef)|((scanline>>4)&0x10));
    isa_outb(0x3d4,9);
    isa_outb(0x3d5,(isa_inb(0x3d5)&0xbf)|((scanline>>3)&0x40));
    }

    /* Now set the final position exactly in case there was rounding error */

    scanline=totallines*CharHeight-1;
    while((isa_inb(0x3da)&8));
    while(!(isa_inb(0x3da)&8));

    isa_outb(0x3d4,0x18);
    isa_outb(0x3d5,scanline&0xff);
    isa_outb(0x3d4,7);
    isa_outb(0x3d5,(isa_inb(0x3d5)&0xef)|((scanline>>4)&0x10));
    isa_outb(0x3d4,9);
    isa_outb(0x3d5,(isa_inb(0x3d5)&0xbf)|((scanline>>3)&0x40));


}

/* Slide the banner out and restore vga registers */

void vga_slideupandexit()
{
int scanline,framecnt;
int CharHeight;
int Const;

isa_outb(0x3d4,0x18);
scanline=isa_inb(0x3d5);
isa_outb(0x3d4,7);
scanline|=((int)(isa_inb(0x3d5)&0x10))<<4;
isa_outb(0x3d4,9);
scanline|=((int)(isa_inb(0x3d5)&0x40))<<3;
++scanline;

CharHeight=16;

Const=(scanline<<16)/(FRAMES*FRAMES);

for(framecnt=FRAMES;framecnt>=0;framecnt--)
{
scanline=((framecnt*framecnt)*Const)>>16;
/* Sync with vertical refresh */
while((isa_inb(0x3da)&8));
while(!(isa_inb(0x3da)&8));

/* Set the split screen position */
isa_outb(0x3d4,0x18);
isa_outb(0x3d5,scanline&0xff);
isa_outb(0x3d4,7);
isa_outb(0x3d5,(isa_inb(0x3d5)&0xef)|((scanline>>4)&0x10));
isa_outb(0x3d4,9);
isa_outb(0x3d5,(isa_inb(0x3d5)&0xbf)|((scanline>>3)&0x40));
}

/* Sync with vertical refresh */
while((isa_inb(0x3da)&8));
while(!(isa_inb(0x3da)&8));

/* Resotre all saved registers */
isa_outb(0x3c0,0x30);
isa_outb(0x3c0,vga_savedregisters[5]);
isa_inb(0x3da);

isa_outb(0x3d4,0xc);
isa_outb(0x3d5,vga_savedregisters[3]);
isa_outb(0x3d4,0xd);
isa_outb(0x3d5,vga_savedregisters[4]);
isa_outb(0x3d4,0x18);
isa_outb(0x3d5,vga_savedregisters[0]);
isa_outb(0x3d4,7);
isa_outb(0x3d5,vga_savedregisters[1]);
isa_outb(0x3d4,9);
isa_outb(0x3d5,vga_savedregisters[2]);

/* Restore palette colors */

vga_initpalette();

/* Sync with vertical refresh */
while((isa_inb(0x3da)&8));
while(!(isa_inb(0x3da)&8));

}




void vga_mapfonts()
{
    /* Make fonts appear at 0xa0000 */
    isa_outb(0x3c4,2);
    isa_outb(0x3c5,4);
    isa_outb(0x3c4,4);
    isa_outb(0x3c5,7);
    isa_outb(0x3ce,5);
    isa_outb(0x3cf,0);
    isa_outb(0x3ce,6);
    isa_outb(0x3cf,4);
    isa_outb(0x3ce,4);
    isa_outb(0x3cf,2);

}

void vga_unmapfonts()
{
    /* Back to normal text mode memory mapping */
    isa_outb(0x3c4,2);
    isa_outb(0x3c5,3);
    isa_outb(0x3c4,4);
    isa_outb(0x3c5,3);
    isa_outb(0x3ce,5);
    isa_outb(0x3cf,0x10);
    isa_outb(0x3ce,6);
    isa_outb(0x3cf,0xe);
    isa_outb(0x3ce,4);
    isa_outb(0x3cf,0);
}



void vga_setpalette()
{
    static char palette_values[3*8]=
    {
        0,0,0,
        12,20,32,
        32,32,36,
        22,35,45,
        32,5,5,
        55,5,8,
        60,62,62,
        62,63,63
    };
    int f;

    /* Set palette colors  0-8 = 9-15 because of faulty VMWARE VGA emulation*/
    for(f=0;f<8;++f)
    {
    v86.ctl = 0;
    v86.eax = 0x1010;
    v86.ebx = f+8;
    v86.ecx = (palette_values[f*3+1]<<8)| palette_values[f*3+2];
    v86.edx = palette_values[f*3]<<8;
    v86.addr=0x10;
    v86int();
    v86.ctl = 0;
    v86.eax = 0x1010;
    v86.ebx = f;
    v86.ecx = (palette_values[f*3+1]<<8)| palette_values[f*3+2];
    v86.edx = palette_values[f*3]<<8;
    v86.addr=0x10;
    v86int();
    }



}

void vga_printlogo(int x,int y,struct textmode_bitmap *logo, struct bitmap_glyphs *glyphs)
{
    /* Set the color palette */

    vga_setpalette();

    /* Draw the logo */

    vga_mapfonts();

    /* Enable second font */
    isa_outb(0x3c4,3);
    isa_outb(0x3c5,0x4);



    /* Store the font */
    memcpy((void *)(0xa4000-MEM_BASE+glyphs->start*32),glyphs->glyphs,32*glyphs->count);

    vga_unmapfonts();

    int yc;

    /* Draw the logo by displaying the characters */
    for(yc=0;yc<logo->height;++yc)
            memcpy((void *)(SCREEN_PTR-MEM_BASE+((y+yc)*ScreenWidth+x)*2),logo->charmap+yc*logo->width,logo->width*2);


}
