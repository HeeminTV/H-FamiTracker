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

/*

 Hardware-based mixing levels are based on NSFPlay's default NSFe
 mixe chunk values: https://github.com/bbbradsmith/nsfplay/blob/master/xgm/player/nsf/nsfplay.cpp#L843

 Exported NSFe files use these constants with delta mix levels
 (CFamiTrackerDoc::m_iDeviceLevelOffset[]) for the "mixe" chunk
 https://www.nesdev.org/wiki/NSFe

 The information below is kept for archival purposes.

*/

/*

 This will mix and synthesize the APU audio using blargg's blip-buffer

 Mixing of internal audio relies on Blargg's findings

 Mixing of external channles are based on my own research:

 VRC6 (Madara):
	Pulse channels has the same amplitude as internal-
	pulse channels on equal volume levels.

 FDS:
	Square wave @ v = $1F: 2.4V
	  			  v = $0F: 1.25V
	(internal square wave: 1.0V)

 MMC5 (just breed):
	2A03 square @ v = $0F: 760mV (the cart attenuates internal channels a little)
	MMC5 square @ v = $0F: 900mV

 VRC7:
	2A03 Square  @ v = $0F: 300mV (the cart attenuates internal channels a lot)
	VRC7 Patch 5 @ v = $0F: 900mV
	Did some more tests and found patch 14 @ v=15 to be 13.77dB stronger than a 50% square @ v=15

 ---

 N163 & 5B are still unknown

*/

#include "../stdafx.h"
#include <memory>
#include <algorithm>
#include <cmath>
#include "Mixer.h"
#include "APU.h"
#include "2A03.h"
#include "FDS.h"
#include "N163.h"
#include "5E01.h" // Taken from E-FamiTracker by Euly
#include "7E02.h"
#include "OPLL.h"
#include "6581.h"

#include "utils/variadic_minmax.h"

//#define LINEAR_MIXING

static const float LEVEL_FALL_OFF_RATE = 0.6f;
static const int   LEVEL_FALL_OFF_DELAY = 3;

CMixer::CMixer(CAPU* Parent)
	: m_APU(Parent)
{
	memset(m_iChannels, 0, sizeof(int32_t) * CHANNELS);
	memset(m_fChannelLevels, 0, sizeof(float) * CHANNELS);
	memset(m_iChanLevelFallOff, 0, sizeof(uint32_t) * CHANNELS);

	m_fLevelAPU1 = 1.0f;
	m_fLevelAPU2 = 1.0f;
	m_fLevelVRC6 = 1.0f;
	m_fLevelVRC7 = 1.0f;
	m_fLevelMMC5 = 1.0f;
	m_fLevelFDS = 1.0f;
	m_fLevelN163 = 1.0f;
	m_fLevel5B = 1.0f;		// // // 050B
	m_fLevelAY8930 = 1.0f;
	m_fLevelAY = 1.0f;
	m_fLevelYM2149F = 1.0f;
	m_fLevel5E01_APU1 = 1.0f;
	m_fLevel5E01_APU2 = 1.0f;
	m_fLevel7E02_APU1 = 1.0f;
	m_fLevel7E02_APU2 = 1.0f;
	m_fLevelOPLL = 1.0f;
	m_fLevel6581 = 1.0f;

	m_iExternalChip = 0;
	m_iSampleRate = 0;

	m_iMeterDecayRate = DECAY_SLOW;		// // // 050B
}

CMixer::~CMixer()
{
}

void CMixer::ExternalSound(int Chip)
{
	m_iExternalChip = Chip;
}

void CMixer::SetChipLevel(chip_level_t Chip, float Level)
{
	switch (Chip) {
		case CHIP_LEVEL_APU1:
			m_fLevelAPU1 = Level;
			break;
		case CHIP_LEVEL_APU2:
			m_fLevelAPU2 = Level;
			break;
		case CHIP_LEVEL_VRC6:
			m_fLevelVRC6 = Level;
			break;
		case CHIP_LEVEL_VRC7:
			m_fLevelVRC7 = Level;
			break;
		case CHIP_LEVEL_FDS:
			m_fLevelFDS = Level;
			break;
		case CHIP_LEVEL_MMC5:
			m_fLevelMMC5 = Level;
			break;
		case CHIP_LEVEL_N163:
			m_fLevelN163 = Level;
			break;
		case CHIP_LEVEL_5B:		// // // 050B
			m_fLevel5B = Level;
			break;
		case CHIP_LEVEL_AY8930:
			m_fLevelAY8930 = Level;
			break;
		case CHIP_LEVEL_AY:
			m_fLevelAY = Level;
			break;
		case CHIP_LEVEL_YM2149F:
			m_fLevelYM2149F = Level;
			break;
		case CHIP_LEVEL_5E01_APU1:
			m_fLevel5E01_APU1 = Level;
			break;
		case CHIP_LEVEL_5E01_APU2:
			m_fLevel5E01_APU2 = Level;
			break;
		case CHIP_LEVEL_7E02_APU1:
			m_fLevel7E02_APU1 = Level;
			break;
		case CHIP_LEVEL_7E02_APU2:
			m_fLevel7E02_APU2 = Level;
			break;
		case CHIP_LEVEL_OPLL:
			m_fLevelOPLL = Level;
			break;
		case CHIP_LEVEL_6581:
			m_fLevel6581 = Level;
			break;

		case CHIP_LEVEL_COUNT:
			break;
	}
}

float CMixer::GetAttenuation(bool UseSurveyMix) const
{
	float ATTENUATION_2A03 = 1.0f;

	if (!UseSurveyMix) {
		const float ATTENUATION_VRC6   = 0.80f;
		const float ATTENUATION_VRC7   = 0.64f;
		const float ATTENUATION_MMC5   = 0.83f;
		const float ATTENUATION_FDS    = 0.90f;
		const float ATTENUATION_N163   = 0.70f;		
		const float ATTENUATION_5B     = 0.50f;		// // // 050B
		const float ATTENUATION_AY8930 = 0.50f; // Taken from E-FamiTracker by Euly
		const float ATTENUATION_AY     = 0.50f;
		const float ATTENUATION_YM2149F= 0.50f;
		const float ATTENUATION_5E01   = 0.80f; // Taken from E-FamiTracker by Euly
		const float ATTENUATION_7E02   = 0.80f;
		const float ATTENUATION_OPLL   = 0.64f;
		const float ATTENUATION_6581   = 0.80f; // Taken from E-FamiTracker by Euly

		// Increase headroom if some expansion chips are enabled

		if (m_iExternalChip & SNDCHIP_VRC6)
			ATTENUATION_2A03 *= ATTENUATION_VRC6;
		if (m_iExternalChip & SNDCHIP_VRC7)
			ATTENUATION_2A03 *= ATTENUATION_VRC7;
		if (m_iExternalChip & SNDCHIP_FDS)
			ATTENUATION_2A03 *= ATTENUATION_FDS;
		if (m_iExternalChip & SNDCHIP_MMC5)
			ATTENUATION_2A03 *= ATTENUATION_MMC5;
		if (m_iExternalChip & SNDCHIP_N163)
			ATTENUATION_2A03 *= ATTENUATION_N163;
		if (m_iExternalChip & SNDCHIP_5B)		// // // 050B
			ATTENUATION_2A03 *= ATTENUATION_5B;
		if (m_iExternalChip & SNDCHIP_AY8930)
			ATTENUATION_2A03 *= ATTENUATION_AY8930;
		if (m_iExternalChip & SNDCHIP_AY)
			ATTENUATION_2A03 *= ATTENUATION_AY;
		if (m_iExternalChip & SNDCHIP_SSG)
			ATTENUATION_2A03 *= ATTENUATION_YM2149F;
		if (m_iExternalChip & SNDCHIP_5E01)		// Taken from E-FamiTracker by Euly
			ATTENUATION_2A03 *= ATTENUATION_5E01;
		if (m_iExternalChip & SNDCHIP_7E02)
			ATTENUATION_2A03 *= ATTENUATION_7E02;
		if (m_iExternalChip & SNDCHIP_OPLL)
			ATTENUATION_2A03 *= ATTENUATION_OPLL;
		if (m_iExternalChip & SNDCHIP_6581)		// Taken from E-FamiTracker by Euly
			ATTENUATION_2A03 *= ATTENUATION_6581;

	} else {
		// attenuation scaling is exponential based on total chips used
		uint8_t TotalChipsUsed = 1;
		if (m_iExternalChip & SNDCHIP_VRC6)   TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_VRC7)   TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_FDS)    TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_MMC5)   TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_N163)   TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_5B)     TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_AY8930) TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_AY)     TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_SSG)    TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_5E01)   TotalChipsUsed++; 
		if (m_iExternalChip & SNDCHIP_7E02)   TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_OPLL)   TotalChipsUsed++;
		if (m_iExternalChip & SNDCHIP_6581)   TotalChipsUsed++;
		ATTENUATION_2A03 *= static_cast<float>(1.0 / ((float)TotalChipsUsed / 4)); // now they are too quiet
	}

	return ATTENUATION_2A03;
}

void CMixer::RecomputeEmuMixState()
{
	auto LowCut = m_MixerConfig.LowCut;
	auto HighCut = m_MixerConfig.HighCut;
	auto HighDamp = m_MixerConfig.HighDamp;

	// Blip-buffer filtering
	BlipBuffer.bass_freq(LowCut);

	blip_eq_t eq(-HighDamp, HighCut, m_iSampleRate);

	// See https://docs.google.com/document/d/19vtipTYI-vqL3-BPrE9HPjHmPpkFuIZKvWfevP3Oo_A/edit#heading=h.h70ipevgjbn7
	// for an exploration of how I came to this design.
	for (auto* chip : m_APU->m_SoundChips2) {
		chip->UpdateFilter(eq);
	}

	SynthVRC6.treble_eq(eq);
	SynthMMC5.treble_eq(eq);
	SynthS5B.treble_eq(eq);
	SynthAY8930.treble_eq(eq);
	SynthAY.treble_eq(eq);
	SynthYM2149F.treble_eq(eq);

	// Volume levels
	auto &chip2A03 = *m_APU->m_p2A03;
	auto &chipVRC7 = *m_APU->m_pVRC7;
	auto &chipFDS = *m_APU->m_pFDS;
	auto &chipN163 = *m_APU->m_pN163;
	auto& chip5E01 = *m_APU->m_p5E01; // Taken from E-FamiTracker by Euly
	auto& chip7E02 = *m_APU->m_p7E02;
	auto& chipOPLL = *m_APU->m_pOPLL;
	auto& chip6581 = *m_APU->m_p6581; // Taken from E-FamiTracker by Euly

	bool UseSurveyMixing = m_MixerConfig.UseSurveyMix;

	float Volume = m_MixerConfig.OverallVol * GetAttenuation(UseSurveyMixing);
	
	// Update mixing

	// Maybe the range argument, as well as the constant factor in the volume,
	// should be supplied by the CSoundChip2 subclass rather than CMixer.
	chip2A03.UpdateMixingAPU1(Volume * m_fLevelAPU1, UseSurveyMixing);
	chip2A03.UpdateMixingAPU2(Volume * m_fLevelAPU2, UseSurveyMixing);
	chipFDS.UpdateMixLevel(Volume * m_fLevelFDS, UseSurveyMixing);
	chipN163.UpdateMixLevel(Volume * m_fLevelN163, UseSurveyMixing);
	chip5E01.UpdateMixing5E01_APU1(Volume * m_fLevel5E01_APU1, UseSurveyMixing);
	chip5E01.UpdateMixing5E01_APU2(Volume * m_fLevel5E01_APU2, UseSurveyMixing);
	chip7E02.UpdateMixing7E02_APU1(Volume * m_fLevel7E02_APU1, UseSurveyMixing); // Im in your walls
	chip7E02.UpdateMixing7E02_APU2(Volume * m_fLevel7E02_APU2, UseSurveyMixing);
	chip6581.UpdateMix(Volume * m_fLevel6581); // Taken from E-FamiTracker by Euly

	if (UseSurveyMixing) {
		chipVRC7.UpdateMixLevel(Volume * m_fLevelVRC7, UseSurveyMixing);
		SynthVRC6.volume(Volume * m_fLevelVRC6, 15 + 15 + 31);			// P1 + P2 + Saw, linear
		SynthMMC5.volume(Volume * m_fLevelMMC5, 15 + 15 + 255);		// P1 + P2 + DAC, linear
		SynthS5B.volume(Volume * m_fLevel5B, 255 + 255 + 255);			// 5B1 + 5B2 + 5B3, linear
		SynthAY8930.volume(Volume * m_fLevelAY8930, 255 + 255 + 255);	// AY1 + AY2 + AY3, linear
		SynthAY.volume(Volume * m_fLevelAY, 255 + 255 + 255);			// AY1 + AY2 + AY3, linear
		SynthYM2149F.volume(Volume * m_fLevelYM2149F, 255 + 255 + 255);// YM1 + YM2 + YM3, linear
		chipOPLL.UpdateMixLevel(Volume * m_fLevelOPLL, UseSurveyMixing);
	} else {
		// match legacy expansion audio mixing

		// VRC7 level does not decrease as you enable expansion chips
		chipVRC7.UpdateMixLevel(m_MixerConfig.OverallVol * m_fLevelVRC7);
		SynthVRC6.volume(Volume * 3.98333f * m_fLevelVRC6, 500);
		SynthMMC5.volume(Volume * 1.18421f * m_fLevelMMC5, 130);
		SynthS5B.volume(Volume * m_fLevel5B, 1200);  // Not checked
		SynthAY8930.volume(Volume * m_fLevel5B, 1200);
		SynthAY.volume(Volume * m_fLevelAY, 1200);
		SynthYM2149F.volume(Volume * m_fLevelYM2149F, 1200);
		chipOPLL.UpdateMixLevel(m_MixerConfig.OverallVol * m_fLevelOPLL);
	}

	// Update per-chip filtering and emulation

	chipN163.UpdateN163Filter(m_MixerConfig.N163Lowpass, m_EmulatorConfig.N163DisableMultiplexing);
	chipFDS.UpdateFDSFilter(m_MixerConfig.FDSLowpass);

	ASSERT(!m_EmulatorConfig.UseOPLLPatchBytes.empty());
	ASSERT(m_EmulatorConfig.UseOPLLPatchBytes.size() == 19 * 8);

	chipVRC7.UpdatePatchSet(
		m_EmulatorConfig.UseOPLLPatchSet,
		m_EmulatorConfig.UseOPLLExt,
		&m_EmulatorConfig.UseOPLLPatchBytes[0]
	);

	chipOPLL.UpdatePatchSet(
		7, // 2413
		false, // No
		NULL // idk
	);
}

int CMixer::GetMeterDecayRate() const		// // // 050B
{
	return m_iMeterDecayRate;
}

void CMixer::SetMeterDecayRate(int Rate)		// // // 050B
{
	m_iMeterDecayRate = Rate;
}

bool CMixer::AllocateBuffer(unsigned int BufferLength, uint32_t SampleRate, uint8_t NrChannels)
{
	m_iSampleRate = SampleRate;
	BlipBuffer.set_sample_rate(SampleRate, (BufferLength * 1000 * 2) / SampleRate);

	// I don't know if BlipFDS is initialized or not.
	// So I copied the above call to CMixer::UpdateSettings().
	return true;
}

void CMixer::SetClockRate(uint32_t Rate)
{
	// Change the clockrate
	BlipBuffer.clock_rate(Rate);

	// Propagate the change to any sound chips with their own Blip_Buffer.
	// Note that m_APU->m_SoundChips2 may not have been initialized yet,
	// so CAPU::SetExternalSound() does the same thing.
	for (auto * chip : m_APU->m_SoundChips2) {
		chip->SetClockRate(Rate);
	}
}

void CMixer::ClearBuffer()
{
	BlipBuffer.clear();

	// What about CSoundChip2 which owns its own Blip_Synth?
	// I've decided that CMixer should not be responsible for clearing those Blip_Synth,
	// but rather CSoundChip2::Reset() should do so.
	//
	// This works because CMixer::ClearBuffer() is only called by CAPU::Reset(),
	// which also calls CSoundChip2::Reset() on each sound chip.

	#define X(SYNTH)  SYNTH.clear();
	FOREACH_SYNTH(X, );
	#undef X
}

int CMixer::SamplesAvail() const
{
	return (int)BlipBuffer.samples_avail();
}

static int get_channel_level(CSoundChip2& chip, int channel) {
	int max = chip.GetChannelLevelRange(channel);
	int level = chip.GetChannelLevel(channel);

	// Clip out-of-bounds levels to the maximum allowed on the meter.
	level = min(level, max);

	int out = level * 16 / (max + 1);
	ASSERT(0 <= out && out <= 15);

	// Ensure that the division process never clips small levels to 0.
	if (level > 0 && out <= 0) {
		out = 1;
	}
	return out;
}

void CMixer::FinishBuffer(int t)
{
	BlipBuffer.end_frame(t);

	for (int i = 0; i < CHANNELS; ++i) {
		// TODO: this is more complicated than 0.5.0 beta's implementation
		if (m_iChanLevelFallOff[i] > 0) {
			if (m_iMeterDecayRate == DECAY_FAST)		// // // 050B
				m_iChanLevelFallOff[i] = 0;
			else
				--m_iChanLevelFallOff[i];
		}
		else if (m_fChannelLevels[i] > 0) {
			if (m_iMeterDecayRate == DECAY_FAST)		// // // 050B
				m_fChannelLevels[i] = 0;
			else {
				m_fChannelLevels[i] -= LEVEL_FALL_OFF_RATE;
				if (m_fChannelLevels[i] < 0)
					m_fChannelLevels[i] = 0;
			}
		}
	}

	auto& chip2A03 = *m_APU->m_p2A03;
	for (int i = 0; i < 5; i++)
		StoreChannelLevel(CHANID_2A03_SQUARE1 + i, get_channel_level(chip2A03, i));

	auto& chipFDS = *m_APU->m_pFDS;
	StoreChannelLevel(CHANID_FDS, get_channel_level(chipFDS, 0));

	auto& chipVRC7 = *m_APU->m_pVRC7;
	for (int i = 0; i < 6; ++i)
		StoreChannelLevel(CHANID_VRC7_CH1 + i, get_channel_level(chipVRC7, i));

	auto& chipN163 = *m_APU->m_pN163;
	for (int i = 0; i < 8; i++)
		StoreChannelLevel(CHANID_N163_CH1 + i, get_channel_level(chipN163, i));

	auto& chip5E01 = *m_APU->m_p5E01; // Taken from E-FamiTracker by Euly
	for (int i = 0; i < 5; i++)
		StoreChannelLevel(CHANID_5E01_SQUARE1 + i, get_channel_level(chip5E01, i));

	auto& chip7E02 = *m_APU->m_p7E02;
	for (int i = 0; i < 5; i++)
		StoreChannelLevel(CHANID_7E02_SQUARE1 + i, get_channel_level(chip7E02, i));

	auto& chipOPLL = *m_APU->m_pOPLL;
	for (int i = 0; i < 9; ++i)
		StoreChannelLevel(CHANID_OPLL_CH1 + i, get_channel_level(chipOPLL, i));

	auto& chip6581 = *m_APU->m_p6581; // Taken from E-FamiTracker by Euly
	for (int i = 0; i < 3; i++)
		StoreChannelLevel(CHANID_6581_CH1 + i, get_channel_level(chip6581, i));

}

//
// Mixing
//

void CMixer::MixVRC6(int Value, int Time)
{
	SynthVRC6.offset(Time, Value, &BlipBuffer);
}

void CMixer::MixMMC5(int Value, int Time)
{
	SynthMMC5.offset(Time, Value, &BlipBuffer);
}

void CMixer::MixS5B(int Value, int Time)
{
	SynthS5B.offset(Time, Value, &BlipBuffer);
}

void CMixer::MixAY8930(int Value, int Time)
{
	SynthAY8930.offset(Time, Value, &BlipBuffer);
}

void CMixer::MixAY(int Value, int Time)
{
	SynthAY.offset(Time, Value, &BlipBuffer);
}

void CMixer::MixYM2149F(int Value, int Time)
{
	SynthYM2149F.offset(Time, Value, &BlipBuffer);
}

void CMixer::AddValue(int ChanID, int Chip, int Value, int AbsValue, int FrameCycles)
{
	// Add sound to mixer
	//

	int Delta = Value - m_iChannels[ChanID];
	StoreChannelLevel(ChanID, AbsValue);
	m_iChannels[ChanID] = Value;

	// Unless otherwise notes, Value is already a delta.
	switch (Chip) {
		case SNDCHIP_NONE:
			// 2A03 nonlinear mixing bypasses CMixer now, and talks directly to BlipBuffer
			// (obtained by CMixer::GetBuffer()).
			break;
		case SNDCHIP_MMC5:
			// Value == AbsValue.
			MixMMC5(Delta, FrameCycles);
			break;
		case SNDCHIP_VRC6:
			MixVRC6(Value, FrameCycles);
			break;
		case SNDCHIP_5B:		// // // 050B
			MixS5B(Value, FrameCycles);
			break;
		case SNDCHIP_AY8930:
			MixAY8930(Value, FrameCycles);
			break;
		case SNDCHIP_AY:
			MixAY(Value, FrameCycles);
			break;
		case SNDCHIP_SSG:
			MixYM2149F(Value, FrameCycles);
			break;
	}
}

int CMixer::ReadBuffer(void *Buffer)
{
	return BlipBuffer.read_samples((blip_amplitude_t*)Buffer, BlipBuffer.samples_avail());
}

int32_t CMixer::GetChanOutput(uint8_t Chan) const
{
	return (int32_t)m_fChannelLevels[Chan];
}

void CMixer::StoreChannelLevel(int Channel, int Value)
{
	int AbsVol = abs(Value);

	// Adjust channel levels for some channels
	if (Channel == CHANID_VRC6_SAWTOOTH)
		AbsVol = (AbsVol * 3) / 4;

	if (Channel == CHANID_MMC5_VOICE) // Taken from E-FamiTracker by Euly
		AbsVol = (AbsVol * 2) / 36;


	if (Channel >= CHANID_5B_CH1 && Channel <= CHANID_5B_CH3) {
		AbsVol = (int)(logf((float)AbsVol) * 2.8f);
	}

	if (Channel >= CHANID_AY8930_CH1 && Channel <= CHANID_AY8930_CH3) {
		AbsVol = (int)(logf((float)AbsVol) * 2.8f);
	}

	if (Channel >= CHANID_AY_CH1 && Channel <= CHANID_AY_CH3) {
		AbsVol = (int)(logf((float)AbsVol) * 2.8f);
	}

	if (Channel >= CHANID_YM2149F_CH1 && Channel <= CHANID_YM2149F_CH3) {
		AbsVol = (int)(logf((float)AbsVol) * 2.8f);
	}

	if (float(AbsVol) >= m_fChannelLevels[Channel]) {
		m_fChannelLevels[Channel] = float(AbsVol);
		m_iChanLevelFallOff[Channel] = LEVEL_FALL_OFF_DELAY;
	}
}

void CMixer::ClearChannelLevels()
{
	memset(m_fChannelLevels, 0, sizeof(float) * CHANNELS);
	memset(m_iChanLevelFallOff, 0, sizeof(uint32_t) * CHANNELS);
}

uint32_t CMixer::ResampleDuration(uint32_t Time) const
{
	return (uint32_t)BlipBuffer.resampled_duration((blip_nsamp_t)Time);
}
