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

#include "../stdafx.h"
#include "../FamiTracker.h"
#include "../Settings.h"
#include "APU.h"
#include "OPLL.h"
#include "../RegisterState.h"		// // //

const float  COPLL::AMPLIFY = 4.6f;		// Mixing amplification, VRC7 patch 14 is 4, 88 times stronger than a 50 % square @ v = 15
const uint32_t COPLL::OPLL_CLOCK = CAPU::BASE_FREQ_VRC7;	// Clock frequency

COPLL::COPLL()
{
	m_pRegisterLogger->AddRegisterRange(0x00, 0x07);		// // //
	m_pRegisterLogger->AddRegisterRange(0x10, 0x18);
	m_pRegisterLogger->AddRegisterRange(0x20, 0x28);
	m_pRegisterLogger->AddRegisterRange(0x30, 0x38);
	Reset();
}

COPLL::~COPLL()
{
	if (m_pOPLLInt != NULL) {
		OPLL_delete(m_pOPLLInt);
		m_pOPLLInt = NULL;
	}

	SAFE_RELEASE_ARRAY(m_pBuffer);
}

void COPLL::Reset()
{
	m_iBufferPtr = 0;
	m_iTime = 0;
	m_BlipOPLL.clear();
	if (m_pOPLLInt != NULL) {
		// update patchset and OPLL type
		OPLL_setChipType(m_pOPLLInt, 0);

		if (m_UseExternalOPLLChip && m_PatchSet != NULL)
			OPLL_setPatch(m_pOPLLInt, m_PatchSet);
		else
			OPLL_resetPatch(m_pOPLLInt, m_PatchSelection);

		OPLL_reset(m_pOPLLInt);
	}
}

void COPLL::UpdateFilter(blip_eq_t eq)
{
	m_SynthOPLL.treble_eq(eq);
	m_BlipOPLL.set_sample_rate(eq.sample_rate);
	m_BlipOPLL.bass_freq(0);
}

void COPLL::SetClockRate(uint32_t Rate)
{
	m_BlipOPLL.clock_rate(Rate);
}

void COPLL::SetSampleSpeed(uint32_t SampleRate, double ClockRate, uint32_t FrameRate)
{
	if (m_pOPLLInt != NULL) {
		OPLL_delete(m_pOPLLInt);
	}

	m_pOPLLInt = OPLL_new(OPLL_CLOCK, SampleRate);

	OPLL_reset(m_pOPLLInt);

	m_iMaxSamples = (SampleRate / FrameRate) * 2;	// Allow some overflow

	SAFE_RELEASE_ARRAY(m_pBuffer);
	m_pBuffer = new int16_t[m_iMaxSamples];
	memset(m_pBuffer, 0, sizeof(int16_t) * m_iMaxSamples);
}

void COPLL::SetDirectVolume(double Volume)
{
	m_DirectVolume = Volume;
}

void COPLL::Write(uint16_t Address, uint8_t Value)
{
	switch (Address) {
		case 0x6000:
			m_iSoundReg = Value;
			break;
		case 0x6001:
			OPLL_writeReg(m_pOPLLInt, m_iSoundReg, Value);
			break;
	}
}

void COPLL::Log(uint16_t Address, uint8_t Value)		// // //
{
	switch (Address) {
	case 0x6000: m_pRegisterLogger->SetPort(Value); break;
	case 0x6001: m_pRegisterLogger->Write(Value); break;
	}
}

uint8_t COPLL::Read(uint16_t Address, bool &Mapped)
{
	return 0;
}

void COPLL::Process(uint32_t Time, Blip_Buffer& Output)
{
	// This cannot run in sync, fetch all samples at end of frame instead
	m_iTime += Time;
}

void COPLL::EndFrame(Blip_Buffer& Output, gsl::span<int16_t> TempBuffer)
{
	uint32_t WantSamples = Output.count_samples(m_iTime);

	static int32_t LastSample = 0;

	// Generate OPLL samples
	while (m_iBufferPtr < WantSamples) {
		int32_t RawSample = OPLL_calc(m_pOPLLInt);

		// emu2413's waveform output ranges from -4095...4095
		// fully rectified by abs(), so resulting waveform is around 0-4095
		for (int i = 0; i < 9; i++)
			m_ChannelLevels[i].update(static_cast<uint8_t>((255.0 * (OPLL_getchanvol(i) + 1.0)/4096.0)));

		// Apply direct volume, hacky workaround
		int32_t Sample = static_cast<int32_t>(double(RawSample) * m_DirectVolume);

		if (Sample > 32767)
			Sample = 32767;
		if (Sample < -32768)
			Sample = -32768;

		m_pBuffer[m_iBufferPtr++] = int16_t((Sample + LastSample) >> 1);
		LastSample = Sample;
	}

	Output.mix_samples((blip_amplitude_t*)m_pBuffer, WantSamples);

	m_iBufferPtr -= WantSamples;

	m_iTime = 0;
}

double COPLL::GetFreq(int Channel) const		// // //
{
	if (Channel < 0 || Channel >= 9) return 0.;
	int Lo = m_pRegisterLogger->GetRegister(Channel | 0x10)->GetValue();
	int Hi = m_pRegisterLogger->GetRegister(Channel | 0x20)->GetValue() & 0x0F;
	Lo |= (Hi << 8) & 0x100;
	Hi >>= 1;
	return 49716. * Lo / (1 << (19 - Hi));
}

int COPLL::GetChannelLevel(int Channel)
{
	ASSERT(0 <= Channel && Channel < 9);
	if (0 <= Channel && Channel < 9) {
		return m_ChannelLevels[Channel].getLevel();
	}
	return 0;
}

int COPLL::GetChannelLevelRange(int Channel) const
{
	return 127;
}

void COPLL::UpdateMixLevel(double v, bool UseSurveyMix)
{
	// The output of emu2413 is resampled. This means
	// that the emulator output suffers no multiplex hiss and
	// bit depth quantization.
	// TODO: replace emu2413 with Nuked-OPLL for better multiplexing accuracy?

	// hacky solution, since OPLL uses asynchronous direct buffer writes
	SetDirectVolume(UseSurveyMix ? v : (v * AMPLIFY));
	
	// emu2413's waveform output ranges from -4095...4095
	m_SynthOPLL.volume(v, UseSurveyMix ? 8191 : 10000);
}

void COPLL::UpdatePatchSet(int PatchSelection, bool UseExternalOPLLChip, uint8_t* PatchSet)
{
	m_PatchSelection = PatchSelection;
	m_UseExternalOPLLChip = UseExternalOPLLChip;
	m_PatchSet = PatchSet;
}
