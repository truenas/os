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
 *
 */

/*******************************************************************
** vga_effects . c
** Additional FICL words designed for PCBSD's boot menu
** 
*******************************************************************/

#ifdef TESTMAIN
#include <stdlib.h>
#else
#include <stand.h>
#endif
#include "bootstrap.h"
#include <string.h>
#include <../../i386/btx/lib/btxv86.h>
#include <../../i386/loader/vgaeffects.h>

#include "ficl.h"

/*		PCBSD's loader interaction words and extras
 *
 * isaltpressed ( -- bool )
 * vgasetpalette ( colorindex red green blue -- )
 * vgaprint ( x y attr string -- )
 */

void
ficlIsAltpressed(FICL_VM *pVM)
{
v86.ctl = 0;
v86.eax = 0x0200; /* ah=2 */
v86.addr=0x16;
v86int();

stackPushINT(pVM->pStack, (v86.eax&8)? 1:0);

}


void
ficlvgaSetPalette(FICL_VM *pVM)
{
int idx,r,g,b;

b=stackPopINT(pVM->pStack);
g=stackPopINT(pVM->pStack);
r=stackPopINT(pVM->pStack);
idx=stackPopINT(pVM->pStack);

v86.ctl = 0;
v86.eax = 0x1010;
v86.ebx = idx;
v86.ecx = (g<<8)|b;
v86.edx = r<<8;
v86.addr=0x10;
v86int();

}


void
ficlvgaPrint(FICL_VM *pVM)
{
int x,y,attr;

int length = stackPopINT(pVM->pStack);
void *address = (void *)stackPopPtr(pVM->pStack);
attr=stackPopINT(pVM->pStack);
y=stackPopINT(pVM->pStack);
x=stackPopINT(pVM->pStack);

char *str = (char *)malloc(length + 1);
memcpy(str, address, length);
str[length] = 0;

vga_print(x,y,attr,str);
free(str);

}

void ficlvgaClrRect(FICL_VM *pVM)
{
int x,y,width,height,attr;

attr=stackPopINT(pVM->pStack);
height=stackPopINT(pVM->pStack);
width=stackPopINT(pVM->pStack);
y=stackPopINT(pVM->pStack);
x=stackPopINT(pVM->pStack);

if(width<=0) return;
if(height<=0) return;
int xlim=(x+width>80)? 80:x+width;
int ylim=(y+height>25)? 25:y+height;
if(x<0) x=0;
if(y<0) y=0;
if(xlim<=0) return;
if(ylim<=0) return;

unsigned char line[81];
int f;
for(f=0;f<xlim-x;++f) line[f]=32;
line[f]=0;

for(;y<ylim;++y)    vga_print(x,y,attr,line);

return;
}

void ficlvgaSlideDown(FICL_VM *pVM)
{
int nlines;

nlines=stackPopINT(pVM->pStack);

if(nlines<0) nlines=0;
if(nlines>25) nlines=25;

vga_slidedown(nlines);
return;
}

void ficlvgaPrintNum(FICL_VM *pVM)
{
    int x,y,attr;
    int num;

    num=stackPopINT(pVM->pStack);
    attr=stackPopINT(pVM->pStack);
    y=stackPopINT(pVM->pStack);
    x=stackPopINT(pVM->pStack);

char digits[10];
int digit=0;
digits[digit]=48;
while(num>=100) { digits[digit]++; num-=100; }
++digit;
digits[digit]=48;
while(num>=10) { digits[digit]++; num-=10; }
++digit;
digits[digit]=num+48;
++digit;
digits[digit]=0;

for(digit=0;digits[digit]=='0';++digit) digits[digit]=' ';

if(digits[digit]==0) { --digit; digits[digit]='0'; }
vga_print(x,y,attr,digits);
return;
}

void ficlvgaSlideUp(FICL_VM *pVM)
{
vga_slideupandexit();
return;
}

void ficlvgaSync(FICL_VM *pVM)
{
vga_sync();
return;
}

/**************************************************************************
                        f i c l C o m p i l e P l a t f o r m
** Build FreeBSD platform extensions into the system dictionary
**************************************************************************/
void vga_ficlAddWords(FICL_SYSTEM *pSys)
{
    FICL_DICT *dp = pSys->dp;
    assert (dp);

    dictAppendWord(dp, "isaltpressed",        ficlIsAltpressed,    FW_DEFAULT);
    dictAppendWord(dp, "vgasetpalette",        ficlvgaSetPalette,    FW_DEFAULT);
    dictAppendWord(dp, "vgaprint",        ficlvgaPrint,    FW_DEFAULT);
    dictAppendWord(dp, "vgaclrrect",        ficlvgaClrRect,    FW_DEFAULT);
    dictAppendWord(dp, "vgaslidedown",        ficlvgaSlideDown,    FW_DEFAULT);
    dictAppendWord(dp, "vgaprintnum",        ficlvgaPrintNum,    FW_DEFAULT);
    dictAppendWord(dp, "vgaslideup",        ficlvgaSlideUp,    FW_DEFAULT);
    dictAppendWord(dp, "vgasync",        ficlvgaSync,    FW_DEFAULT);

    return;
}

