
\ Copyright (c) 2003 Scott Long <scottl@freebsd.org>
\ Copyright (c) 2003 Aleksander Fafula <alex@fafula.com>
\ All rights reserved.
\
\ Redistribution and use in source and binary forms, with or without
\ modification, are permitted provided that the following conditions
\ are met:
\ 1. Redistributions of source code must retain the above copyright
\    notice, this list of conditions and the following disclaimer.
\ 2. Redistributions in binary form must reproduce the above copyright
\    notice, this list of conditions and the following disclaimer in the
\    documentation and/or other materials provided with the distribution.
\
\ THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
\ ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
\ IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
\ ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
\ FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
\ DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
\ OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
\ HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
\ LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
\ OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
\ SUCH DAMAGE.
\
\ $FreeBSD: src/sys/boot/forth/beastie.4th,v 1.10.2.2 2006/04/04 17:03:44 emax Exp $

marker task-beastie.4th

hide

variable menuidx
variable menuX
variable menuY
variable promptwidth

variable bootkey
variable bootacpikey
variable bootsafekey
variable bootverbosekey
variable rundisplaywiz
variable bootsinglekey
variable escapekey
variable rebootkey
variable bootsafe_set
variable xvesa
variable xvesa_set
variable xwiz_set
variable kernelkey
variable kerncurrent
variable kerncurrent_set
variable kernold
variable kernold_set
variable kerngeneric
variable kerngeneric_set

46 constant dot
0 constant menutextcolor
15 constant menubackcolor
7 constant menutitlecolor
3 constant menutitlebackcolor
1 constant menubasecolor
6 constant menutopdecorcolor
1 constant menubotdecorcolor

: print-base ( -- )

0 4 80 2
menubasecolor 16 *
menutextcolor +
vgaclrrect

0 6 10 19
\ Calculate color attribute for background
menubasecolor 16 *
menutextcolor +
\ Clear the area
vgaclrrect

70 6 1 19
\ Calculate color attribute for shade
0 16 *
menutextcolor +
\ Clear the area
vgaclrrect

71 6 9 19
\ Calculate color attribute for background
menubasecolor 16 *
menutextcolor +
\ Clear the area
vgaclrrect

10 6 60 3
menutitlebackcolor 16 *
menutextcolor +
\ Clear the area
vgaclrrect


10 9 60 16
menubackcolor 16 *
menutextcolor +
\ Clear the area
vgaclrrect

11 8 menutitlebackcolor 16 * menutopdecorcolor + s" __________________________________________________________" vgaprint
11 22 menubackcolor 16 * menubotdecorcolor + s" __________________________________________________________" vgaprint

;

: acpienabled? ( -- flag )
	s" acpi_load" getenv
	dup -1 = if
		drop false exit
	then
	s" YES" compare-insensitive 0<> if
		false exit
	then
	s" hint.acpi.0.disabled" getenv
	dup -1 <> if
		s" 0" compare 0<> if
			false exit
		then
	else
		drop
	then
	true
;

: printmenuitem ( -- n )
	menuidx @
	1+ dup
	menuidx !
	menuY @ + dup menuX @ swap 
	menubackcolor 16 * menutextcolor +
	menuidx @ vgaprintnum
	menuX @ 3 + swap
	menubackcolor 16 * menutextcolor +
	s" ." vgaprint
	menuidx @ 48 +
;

: options-menu ( -- )
;

: get-kernelname ( -- caddr u )
	kerncurrent_set @ 1 = if s" Current kernel (kernel)" exit then

	s" kernelold_show" getenv
	dup -1 <> if
		s" YES" compare-insensitive 0 = if
				kernold_set @ 1 = if s" Previous kernel (kernel.old)"
				exit
				then
		then
	else
		drop
	then
	s" kernelgeneric_show" getenv
	dup -1 <> if
		s" YES" compare-insensitive 0 = if
			kerngeneric_set @ 1 = if s" Generic PC-BSD (kernel.pcbsd)"
			exit
			then
		then
	else
		drop
	then
	s" Default (kernel)"
;

: cycle-kernels ( -- )
	kerncurrent_set @ 1 = if
		s" kernelold_show" getenv
		dup -1 <> if
			s" YES" compare-insensitive 0 = if
					1 kernold_set !
					0 kerncurrent_set !
					exit
			then
		else
			drop
		then
		s" kernelgeneric_show" getenv
		dup -1 <> if
			s" YES" compare-insensitive 0 = if
					1 kerngeneric_set !
					0 kerncurrent_set !
					exit
			then
		else
			drop
			exit
		then
	then

	kernold_set @ 1 = if
		s" kernelgeneric_show" getenv
		dup -1 <> if
			s" YES" compare-insensitive 0 = if
					1 kerngeneric_set !
					0 kernold_set !
					exit
			then
		else
			drop
			1 kerncurrent_set !
			0 kernold_set !
			exit
		then
		
		
	then

	kerngeneric_set @ 1 = if
			1 kerncurrent_set !
			0 kerngeneric_set !
			exit
	then


;

: beastie-menu ( -- )
	vgasync
	-1 menuidx !
	10 menuY !
	14 menuX !
	print-base
	12 7 menutitlebackcolor  16 * menutitlecolor  + s" Welcome to PC-BSD!" vgaprint
	printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Boot PC-BSD [default]" vgaprint bootkey !
	printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Boot PC-BSD in single user mode" vgaprint bootsinglekey !
	printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Boot PC-BSD with verbose logging" vgaprint bootverbosekey !
	s" arch-i386" environment? if
		drop
		printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Toggle ACPI " vgaprint bootacpikey !
		acpienabled? if
			menuX @ 40 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  (Enabled)" vgaprint
		else
			menuX @ 40 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s" (Disabled)" vgaprint
		then
	else
		-2 bootacpikey !
	then
	printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Toggle Safe Mode " vgaprint bootsafekey !
	bootsafe_set @ 1 = if
		menuX @ 40 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  (Enabled)" vgaprint
	else
		menuX @ 40 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s" (Disabled)" vgaprint
	then
	printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Toggle X in VESA mode " vgaprint xvesa !
	xvesa_set @ 1 = if
		menuX @ 40 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  (Enabled)" vgaprint
	else
		menuX @ 40 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s" (Disabled)" vgaprint
	then
	printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Run the Display Wizard " vgaprint rundisplaywiz !
	xwiz_set @ 1 = if
		menuX @ 40 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  (Enabled)" vgaprint
	else
		menuX @ 40 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s" (Disabled)" vgaprint
	then
	printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Escape to loader prompt" vgaprint escapekey !
	s" kernelmenu_show" getenv
	dup -1 <> if
		s" YES" compare-insensitive 0 = if
			printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Selected kernel: " vgaprint kernelkey !
				menuX @ 22 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + get-kernelname vgaprint
		then
	else
		drop
	then
	printmenuitem menuX @ 4 + menuY @ menuidx @ + menubackcolor  16 * menutextcolor  + s"  Reboot" vgaprint rebootkey !
	menuX @ 21 menubackcolor  16 * menutextcolor  +
	s" Select option, [Enter] to boot" vgaprint
;

: tkeynotimer
			key
;

set-current

: beastie-start

 	isaltpressed 0= if
		0 vgaslideup boot
	else
		s" NO" s" autoboot_delay" setenv
		25 vgaslidedown
	endif



	s" beastie_disable" getenv
	dup -1 <> if
		s" YES" compare-insensitive 0= if
			exit
		then
	else
		drop
	then

	s" NO" s" xvesa" setenv
	0 bootsafe_set !
	0 xvesa_set !
	0 xwiz_set !
	1 kerncurrent_set !
	0 kernold_set !
	0 kerngeneric_set !
		
	begin
			beastie-menu

			tkeynotimer

			dup 13 = if 
				drop
				kerncurrent_set @ 1 = if
					0 1 unload drop
					s" kernel" s" kernel" setenv
				then
				kerngeneric_set @  1 = if
					0 1 unload drop
					s" kernel.pcbsd" s" kernel" setenv
				then
				kernold_set @ 1 = if
					0 1 unload drop
					s" kernel.old" s" kernel" setenv
				then
				0 vgaslideup boot
			then
			dup bootkey @ = if drop 
				kerncurrent_set @ 1 = if
					0 1 unload drop
					s" kernel" s" kernel" setenv
				then
				kerngeneric_set @  1 = if
					0 1 unload drop
					s" kernel.pcbsd" s" kernel" setenv
				then
				kernold_set @ 1 = if
					0 1 unload drop
					s" kernel.old" s" kernel" setenv
				then
				0 vgaslideup boot
			then
			dup bootacpikey @ = if
				acpienabled? if
					s" acpi_load" unsetenv
					s" 1" s" hint.acpi.0.disabled" setenv
					s" 1" s" loader.acpi_disabled_by_user" setenv
				else
					s" YES" s" acpi_load" setenv
					s" 0" s" hint.acpi.0.disabled" setenv
					s" loader.acpi_disabled_by_user" unsetenv

				then
			then
			dup bootsafekey @ = if
				bootsafe_set @ 1 = if
					s" 0" s" hw.ata.ata_dma" setenv
					s" 0" s" hw.ata.atapi_dma" setenv
					s" 0" s" hw.ata.wc" setenv
					s" 0" s" hw.eisa_slots" setenv
					s" 1" s" hint.kbdmux.0.disabled" setenv
					0 bootsafe_set !
				else
					s" 1" s" hw.ata.ata_dma" setenv
					s" 1" s" hw.ata.atapi_dma" setenv
					s" 1" s" hw.ata.wc" setenv
					s" 1" s" hw.eisa_slots" setenv
					s" 0" s" hint.kbdmux.0.disabled" setenv
					1 bootsafe_set !
				then
			then
			dup bootverbosekey @ = if
				s" YES" s" boot_verbose" setenv
				drop 0 vgaslideup boot
			then
			dup xvesa @ = if
				xvesa_set @ 1 = if
					s" NO" s" xvesa" setenv
					0 xvesa_set !
				else
					s" YES" s" xvesa" setenv
					1 xvesa_set !
				then
			then
			dup rundisplaywiz @ = if
				xwiz_set @ 1 = if
					s" NO" s" runwiz" setenv
					0 xwiz_set !
				else
					s" YES" s" runwiz" setenv
					1 xwiz_set !
				then
			then
			dup bootsinglekey @ = if
				s" YES" s" boot_single" setenv
				drop 0 vgaslideup boot
			then
			dup kernelkey @ = if
				cycle-kernels
			then
			dup escapekey @ = if
				drop
				s" NO" s" autoboot_delay" setenv
				clear
				vgaslideup
				exit
			then
			rebootkey @ = if drop 0 vgaslideup reboot then
	again
;

previous
