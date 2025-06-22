/*
** Dn-FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2020-2025 D.P.C.M.
** FamiTracker Copyright (C) 2005-2020 Jonathan Liss
** 0CC-FamiTracker Copyright (C) 2014-2018 HertzDevil
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program. If not, see https://www.gnu.org/licenses/.
*/

#pragma once

#include <cstdint>

const int SNDCHIP_NONE	= 0;			// 2A03 Only
const int SNDCHIP_VRC6	= 1;			// Konami VRCVI
const int SNDCHIP_VRC7	= 2;			// Konami VRCVII
const int SNDCHIP_FDS	= 4;			// Famicom Disk Sound
const int SNDCHIP_MMC5	= 8;			// Nintendo MMC5
const int SNDCHIP_N163	= 16;			// Namco N163
const int SNDCHIP_5B	= 32;			// Sunsoft 5B
const int SNDCHIP_5E01	= 512;			// Eulous 5E01
const int SNDCHIP_7E02	= 1024;			// Saeyahn 7E02
const int SNDCHIP_OPLL  = 128;			// Yamaha YM2413

enum chan_id_t {
	CHANID_2A03_SQUARE1,
	CHANID_2A03_SQUARE2,
	CHANID_2A03_TRIANGLE,
	CHANID_2A03_NOISE,
	CHANID_2A03_DPCM,

	CHANID_VRC6_PULSE1,
	CHANID_VRC6_PULSE2,
	CHANID_VRC6_SAWTOOTH,

	CHANID_MMC5_SQUARE1,
	CHANID_MMC5_SQUARE2,
	CHANID_MMC5_VOICE,

	CHANID_N163_CH1,		// // //
	CHANID_N163_CH2,
	CHANID_N163_CH3,
	CHANID_N163_CH4,
	CHANID_N163_CH5,
	CHANID_N163_CH6,
	CHANID_N163_CH7,
	CHANID_N163_CH8,

	CHANID_FDS,

	CHANID_VRC7_CH1,
	CHANID_VRC7_CH2,
	CHANID_VRC7_CH3,
	CHANID_VRC7_CH4,
	CHANID_VRC7_CH5,
	CHANID_VRC7_CH6,

	CHANID_5B_CH1,
	CHANID_5B_CH2,
	CHANID_5B_CH3,

	CHANID_5E01_SQUARE1,
	CHANID_5E01_SQUARE2,
	CHANID_5E01_WAVEFORM,
	CHANID_5E01_NOISE,
	CHANID_5E01_DPCM,

	CHANID_7E02_SQUARE1,
	CHANID_7E02_SQUARE2,
	CHANID_7E02_WAVEFORM,
	CHANID_7E02_NOISE,
	CHANID_7E02_DPCM,

	CHANID_OPLL_CH1,
	CHANID_OPLL_CH2,
	CHANID_OPLL_CH3,
	CHANID_OPLL_CH4,
	CHANID_OPLL_CH5,
	CHANID_OPLL_CH6,
	CHANID_OPLL_CH7,
	CHANID_OPLL_CH8,
	CHANID_OPLL_CH9,

	CHANNELS		/* Total number of channels */
};

enum apu_machine_t {
	MACHINE_NTSC, 
	MACHINE_PAL
};
