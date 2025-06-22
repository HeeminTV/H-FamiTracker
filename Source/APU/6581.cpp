/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
**
** 0CC-FamiTracker is (C) 2014-2016 HertzDevil
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/

#include "../stdafx.h"
#include "../Common.h"
#include <algorithm>  // std::min
#include "APU.h"
#include "Mixer.h"
#include "6581.h"
#include "../RegisterState.h"		// // //
#include "utils/variadic_minmax.h"
#include "residfp/SID.h"		// // !!

// // // 6581 sound chip class

C6581::C6581() {
	m_pRegisterLogger->AddRegisterRange(0xD400, 0xD41C);		// // //

	// Reset() is called by CAPU::SetExternalSound(), but let's call it ourself.
	Reset();
}

// TODO set range of Blip_Synth. Copy whatever CMixer does.

void C6581::Reset()
{
	m_Sid.reset();
	Synth6581.clear();

	m_Sid.setSamplingParameters(CAPU::BASE_FREQ_ATARI, SamplingMethod::DECIMATE, 44100, 44100);
	m_Sid.setFilter6581Curve(0.875);
	m_Sid.setChipModel(MOS8580);
	m_Sid.enableFilter(true);

}

void C6581::UpdateFilter(blip_eq_t eq)
{
	Synth6581.treble_eq(eq);
}

void C6581::Process(uint32_t Time, Blip_Buffer& Output)
{
	uint32_t now = 0;

	auto get_output = [this](uint32_t dclocks, uint32_t now, Blip_Buffer& blip_buf) {
		short buf = {};
		m_Sid.clock(dclocks, &buf);


		m_Sid.externalFilter->clock((unsigned short)(m_iInput));

		Synth6581.update(m_iTime + now, (int)(m_Sid.output() * 0.25), &blip_buf);
		// channel levels
		m_ChannelLevels[0].update((uint8_t)((m_Sid.voice[0]->output(m_Sid.voice[2]->wave()) + 2048 * 255) / 8192));
		m_ChannelLevels[1].update((uint8_t)((m_Sid.voice[1]->output(m_Sid.voice[0]->wave()) + 2048 * 255) / 8192));
		m_ChannelLevels[2].update((uint8_t)((m_Sid.voice[2]->output(m_Sid.voice[1]->wave()) + 2048 * 255) / 8192));
	};

	while (now < Time) {
		// Due to how nsfplay is implemented, when ClocksUntilLevelChange() is used,
		// the result of `Tick(clocks); Render()` should be sent to Blip_Synth
		// at the instant in time *before* Tick() is called.
		// See https://docs.google.com/document/d/1BnXwR3Avol7S5YNa3d4duGdbI6GNMwuYWLHuYiMZh5Y/edit#heading=h.lnh9d8j1x3uc
		auto dclocks = 6;//Time - now;
		get_output(dclocks, now, Output);
		now += dclocks;
	}

	m_iTime += Time;
}

void C6581::EndFrame(Blip_Buffer&, gsl::span<int16_t>)
{
	m_iTime = 0;
}

void C6581::Write(uint16_t Address, uint8_t Value)
{
	if (Address >= 0xD400 && Address <= 0xD41C)
		m_Sid.write(Address - 0xD400, Value);
}

uint8_t C6581::Read(uint16_t Address, bool& Mapped)
{
	//if (Address >= 0xD400 && Address <= 0xD41C)
		//return m_Sid.read().sid_register[Address-0xD400];
	return 0;
}

double C6581::GetFreq(int Channel) const		// // !!
{

	switch (Channel) {
	case 0: case 1: case 2:
		return 0;//Read(0xD400 + Channel * 7, true) * 0.0596;
	}
	return 0.0;
}

int C6581::GetChannelLevel(int Channel)
{
	if (0 <= Channel && Channel < 3) {
		return m_ChannelLevels[Channel].getLevel();
	}
	return 0;
}

int C6581::GetChannelLevelRange(int Channel) const
{
	switch (Channel) {
	case 0: case 1: case 2:
		return 127;

	default:
		// unknown channel, return 1 to avoid division by 0
		return 1;
	}
}


void C6581::UpdateMix(double v) {
	Synth6581.volume(v, 10000);
}

void C6581::SetInput(int x) {
	m_iInput = x;
}