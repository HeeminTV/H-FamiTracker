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

#include "Types.h"
#include "../Common.h"
#include "../Blip_Buffer/blip_buffer.h"

#include <vector>		// !! !!
#include <string>		// !! !!

enum chip_level_t {
	CHIP_LEVEL_APU1,
	CHIP_LEVEL_APU2,
	CHIP_LEVEL_VRC6,
	CHIP_LEVEL_VRC7,
	CHIP_LEVEL_FDS,
	CHIP_LEVEL_MMC5,
	CHIP_LEVEL_N163,
	CHIP_LEVEL_5B,
	CHIP_LEVEL_AY8930,
	CHIP_LEVEL_AY,
	CHIP_LEVEL_YM2149F,
	CHIP_LEVEL_5E01_APU1,
	CHIP_LEVEL_5E01_APU2,
	CHIP_LEVEL_7E02_APU1,
	CHIP_LEVEL_7E02_APU2,
	CHIP_LEVEL_OPLL,
	CHIP_LEVEL_6581,
	CHIP_LEVEL_COUNT
};

class C2A03;
class CFDS;
class CAPU;
class C5E01; // Taken from E-FamiTracker by Euly
class C7E02;
class COPLL;
class C6581; // Taken from E-FamiTracker by Euly

struct MixerConfig {
	// Global lowpass
	int LowCut = 0;
	// Global highpass
	int HighCut = 0;
	// Global higpass damping
	int HighDamp = 0;
	// Global volume
	float OverallVol = 0;

	// https://forums.nesdev.org/viewtopic.php?t=17741
	// Use survey derived default mix levels. Overrides the chip levels.
	bool UseSurveyMix = false;

	// Device lowpassing, described in integer Hz.
	int16_t FDSLowpass = 2000;
	int16_t N163Lowpass = 12000;

	// Device mixing offsets, described in centibels. too late to change to millibels.
	// range is +- 12 db.
	std::vector<int16_t> DeviceMixOffsets = {
		0,		// 2A03_APU1
		0,		// 2A03_APU2
		0,		// VRC6
		0,		// VRC7
		0,		// 2C33
		0,		// MMC5
		0,		// N163
		0,		// 5B
		0,		// AY8930
		0,		// AY-3-8910
		0,		// YM2149F
		0,		// 5E01_APU1
		0,		// 5E01_APU2
		0,		// 7E02_APU1
		0,		// 7E02_APU2
		0,		// YM2413
		0,		// 6581
	};
};

struct EmulatorConfig {
	bool N163DisableMultiplexing = true;
	int UseOPLLPatchSet = 0;

	// Use external OPLL instead of VRC7
	bool UseOPLLExt = false;

	// User-defined hardware patch set for external OPLL
	std::vector<uint8_t> UseOPLLPatchBytes = {
		0, 0, 0, 0, 0, 0, 0, 0,		// patch 0 must always be 0
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0
	};

	// User-defined hardware patch names for external OPLL
	std::vector<std::string> UseVRC7PatchNames = {
		"(custom patch)",		// patch 0 must always be named "(custom instrument)"
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""
	};
};

class CMixer
{
public:
	CMixer(CAPU * Parent);
	~CMixer();

	void	ExternalSound(int Chip);

	void	AddValue(int ChanID, int Chip, int Value, int AbsValue, int FrameCycles);
	void	SetMixing(MixerConfig cfg) {
		m_MixerConfig = cfg;
	}
	void	SetEmulation(EmulatorConfig cfg) {
		m_EmulatorConfig = cfg;
	};
	void	RecomputeEmuMixState();		// must be called after SetMixing() and SetEmulation()

	bool	AllocateBuffer(unsigned int Size, uint32_t SampleRate, uint8_t NrChannels);
	Blip_Buffer& GetBuffer() {
		return BlipBuffer;
	}
	void	SetClockRate(uint32_t Rate);
	void	ClearBuffer();
	void FinishBuffer(int t);
	int		SamplesAvail() const;
	void	MixSamples(blip_amplitude_t *pBuffer, uint32_t Count);
	uint32_t	GetMixSampleCount(int t) const;

	void	AddSample(int ChanID, int Value);
	int		ReadBuffer(void *Buffer);

	int32_t	GetChanOutput(uint8_t Chan) const;
	void	SetChipLevel(chip_level_t Chip, float Level);
	uint32_t	ResampleDuration(uint32_t Time) const;

	int		GetMeterDecayRate() const;		// // // 050B
	void	SetMeterDecayRate(int Rate);		// // // 050B

private:
	void MixVRC6(int Value, int Time);
	void MixMMC5(int Value, int Time);
	void MixS5B(int Value, int Time);
	void MixAY8930(int Value, int Time);
	void MixAY(int Value, int Time);
	void MixYM2149F(int Value, int Time);

	void StoreChannelLevel(int Channel, int Value);
	void ClearChannelLevels();

	float GetAttenuation(bool UseSurveyMix) const;

private:
	// Pointer to parent/owning CAPU object.
	CAPU * m_APU;

	// Blip buffer synths

	// Should never be null during playback. CAPU creates all expansion chips,
	// even if chips are not active in current module.
	Blip_Synth<blip_good_quality> SynthVRC6;
	Blip_Synth<blip_good_quality> SynthMMC5;
	Blip_Synth<blip_good_quality> SynthS5B;		// // // 050B
	Blip_Synth<blip_good_quality> SynthAY8930;
	Blip_Synth<blip_good_quality> SynthAY;
	Blip_Synth<blip_good_quality> SynthYM2149F;

	/// Only used by CMixer::ClearBuffer(), which clears the global Blip_Buffer
	/// and all Blip_Synth owned by CMixer.
	///
	/// What about CSoundChip2 which owns its own Blip_Synth?
	/// I've decided that CMixer should not be responsible for clearing those Blip_Synth,
	/// but rather CSoundChip2::Reset() should do so.
	///
	/// This works because CMixer::ClearBuffer() is only called by CAPU::Reset(),
	/// which also calls CSoundChip2::Reset() on each sound chip.
	#define FOREACH_SYNTH(X, SEP) \
		X(SynthVRC6)   SEP \
		X(SynthMMC5)   SEP \
		X(SynthS5B)    SEP \
		X(SynthAY8930) SEP \
		X(SynthAY)     SEP \
		X(SynthYM2149F)

	// Blip buffer object
	Blip_Buffer	BlipBuffer;

	int32_t		m_iChannels[CHANNELS];
	int			m_iExternalChip;
	uint32_t	m_iSampleRate;

	// channel levels for volume meter
	float		m_fChannelLevels[CHANNELS];
	// volume meter falloff rate
	uint32_t	m_iChanLevelFallOff[CHANNELS];

	int			m_iMeterDecayRate;		// // // 050B
	MixerConfig m_MixerConfig;
	EmulatorConfig m_EmulatorConfig;

	uint8_t m_VRC7PatchSelection;
	uint8_t m_VRC7PatchSet[19 * 9];
	bool m_VRC7PatchUserDefined;

	// device level gain multipliers, in linear scale
	// default level (0dB) is at 1.0
	float		m_fLevelAPU1;
	float		m_fLevelAPU2;
	float		m_fLevelVRC6;
	float		m_fLevelVRC7;
	float		m_fLevelMMC5;
	float		m_fLevelFDS;
	float		m_fLevelN163;
	float		m_fLevel5B;		// // // 050B
	float		m_fLevelAY8930;
	float		m_fLevelAY;
	float		m_fLevelYM2149F;
	float		m_fLevel5E01_APU1;
	float		m_fLevel5E01_APU2;
	float		m_fLevel7E02_APU1;
	float		m_fLevel7E02_APU2;
	float		m_fLevelOPLL;
	float		m_fLevel6581; // Taken from E-FamiTracker by Euly

	friend class CAPU;
};
