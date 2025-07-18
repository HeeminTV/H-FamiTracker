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

//#define LOGGING

#include "../Common.h"
// TODO switch to MixerCommon.h, with forward-declaration of CMixer, plus MixerConfig
#include "Mixer.h"

#include <vector>
#include <memory>
#include <optional>
// move to .cpp
#include <exception>
#include "VRC7.h"
#include "OPLL.h"

// External classes
class C2A03;		// // //
class CVRC6;
class CVRC7;
class CFDS;
class CMMC5;
class CN163;
class CS5B;
class CAY8930;	// Taken from E-FamiTracker by Euly
class CAY;
class CYM2149F;
class C5E01;	 // Taken from E-FamiTracker by Euly
class C7E02;
class COPLL;
class C6581;	// Taken from E-FamiTracker by Euly

class CSoundChip;		// // //
class CSoundChip2;
class CRegisterState;		// // //

#ifdef LOGGING
class CFile;
#endif

class CAPU {
public:
	CAPU(IAudioCallback *pCallback);		// // //
	~CAPU();

	/// Enforce a fixed address, so CMixer can hold a parent pointer to CAPU.
	BLIP_DISABLE_COPY_MOVE(CAPU)

	void	Reset();
	void	Process();
	void	AddCycles(int32_t Cycles);

	void	Write(uint16_t Address, uint8_t Value);		// // //
	uint8_t	Read(uint16_t Address);

	int32_t	GetVol(int Chan) const;
	uint8_t	GetReg(int Chip, int Reg) const;
	double	GetFreq(int Chip, int Chan) const;		// // //
	int	GetFDSModCounter() const;		// TODO: reading $4097 returns $00 for some reason, fix that and remove this hack instead
	CRegisterState *GetRegState(int Chip, int Reg) const;		// // //

	// 2A03
	uint8_t	GetSamplePos() const;
	uint8_t	GetDeltaCounter() const;
	bool	DPCMPlaying() const;
	void	WriteSample(const char *pBuf, int Size);		// // //
	void	ClearSample();		// // //

	// 5E01
	// Taken from E-FamiTracker by Euly
	uint8_t	Get5E01SamplePos() const;
	uint8_t	Get5E01DeltaCounter() const;
	bool	DPCM5E01Playing() const;
	void	Write5E01Sample(const char* pBuf, int Size);
	void	Clear5E01Sample();

	// 7E02
	uint8_t	Get7E02SamplePos() const;
	uint8_t	Get7E02DeltaCounter() const;
	bool	DPCM7E02Playing() const;
	void	Write7E02Sample(const char* pBuf, int Size);
	void	Clear7E02Sample();

	// Configuration methods:
	/// it's a config method which should be dependency-tracked by CAPUConfig,
	/// but it acts kinda like a constructor... so i'll let it slide. public it is.
	bool	SetupSound(int SampleRate, int NrChannels, int Speed);
	/// Mostly orthogonal.
	void	ChangeMachineRate(int Machine, int Rate);		// // //

	/// Called after SetupSound().
	uint32_t GetSoundBufferSamples() const {
		return m_iSoundBufferSamples;
	}

private:
	void	SetExternalSound(int Chip);
	// End configuration methods.

public:
	void	SetMeterDecayRate(int Type) const;		// // // 050B
	int		GetMeterDecayRate() const;		// // // 050B

#ifdef LOGGING
	void	Log();
#endif

public:
	static const int		OPLL_TONE_NUM = 10;
	static const uint8_t	LENGTH_TABLE[];
	static const uint32_t	BASE_FREQ_NTSC;
	static const uint32_t	BASE_FREQ_PAL;
	static const uint32_t	BASE_FREQ_VRC7;
	static const uint32_t	BASE_FREQ_ATARI;
	static const uint8_t	FRAME_RATE_NTSC;
	static const uint8_t	FRAME_RATE_PAL;
	static const uint16_t	NSF_RATE_NTSC;
	static const uint16_t	NSF_RATE_PAL;
	static const uint8_t	OPLL_DEFAULT_PATCHES[OPLL_TONE_NUM][19 * 8];
	static const std::string	OPLL_PATCHNAME_VRC7[19];
	static const std::string	OPLL_PATCHNAME_YM2413[19];
	static const std::string	OPLL_PATCHNAME_YMF281B[19];
	static const std::string	OPLL_PATCHNAME_HM[19];

private:
	static const int SEQUENCER_FREQUENCY;		// // //

	void StepSequence();		// // //
	void EndFrame();

	void LogWrite(uint16_t Address, uint8_t Value);

private:
	CMixer		*m_pMixer;
	IAudioCallback *m_pParent;

	// Expansion chips
	std::unique_ptr<C2A03> m_p2A03;
	std::unique_ptr<C5E01> m_p5E01; // Taken from E-FamiTracker by Euly
	std::unique_ptr<C7E02> m_p7E02;
	std::unique_ptr<C6581> m_p6581; // Taken from E-FamiTracker by Euly

	CVRC6		*m_pVRC6;
	CMMC5		*m_pMMC5;
	std::unique_ptr<CFDS> m_pFDS;
	std::unique_ptr<CN163> m_pN163;
	std::unique_ptr<CVRC7> m_pVRC7;
	std::unique_ptr<COPLL> m_pOPLL;
	CS5B		*m_pS5B;
	CAY8930		*m_pAY8930; // Taken from E-FamiTracker by Euly
	CAY			*m_pAY;
	CYM2149F	*m_pYM2149F;

	/// Bitfield of external sound chips enabled.
	/// Never read, except for code hidden behind #ifdef LOGGING.
	int			m_iExternalSoundChips;

	std::vector<CSoundChip*> m_SoundChips;
	std::vector<CSoundChip2*> m_SoundChips2;

	uint32_t	m_iSampleRate;						// // //
	uint32_t	m_iFrameCycleCount;
	uint32_t	m_iFrameClock;
	uint32_t	m_iCyclesToRun;						// Number of cycles to process

	uint32_t	m_iSoundBufferSamples;				// Size of buffer, in samples
	bool		m_bStereoEnabled;					// If stereo is enabled

	uint32_t	m_iSampleSizeShift;					// To convert samples to bytes
	uint32_t	m_iSoundBufferSize;					// Size of buffer, in samples
	uint32_t	m_iBufferPointer;					// Fill pos in buffer
	int16_t		*m_pSoundBuffer;					// Sound transfer buffer

	uint32_t	m_iFrameCycles;						// Cycles emulated from start of frame
	uint32_t	m_iSequencerClock;					// Clock for frame sequencer
	uint32_t	m_iSequencerNext;					// // // Next value for sequencer
	uint8_t		m_iSequencerCount;					// // // Step count for sequencer

	float		m_fLevelVRC7;
	float		m_fLevelOPLL;
	float		m_fLevel6581; // Taken from E-FamiTracker by Euly
	// // // 050B removed

#ifdef LOGGING
	CFile		  *m_pLog;
	int			  m_iFrame;
//	unsigned char m_iRegs[32];
#endif

	friend class CMixer;
	friend class CAPUConfig;
};

/// Used to reconfigure CAPU and ensure all modified properties are recomputed properly.
/// All modified properties are recomputed once, when CAPUConfig is destroyed.
/// TODO port to an apply() API?
class CAPUConfig {
public:
	CAPUConfig(CAPU * APU)
		: m_APU(APU)
		, m_Mixer(APU->m_pMixer)
		, m_UncaughtExceptions(std::uncaught_exceptions())
	{}

	// Mutator methods
	void SetExternalSound(int Chip) {
		m_ExternalSound = Chip;
	}

	void SetupEmulation(
		bool N163DisableMultiplexing ,
		int UseOPLLPatchSet,
		bool UseOPLLExt,
		std::vector<uint8_t> UseOPLLPatchBytes,
		std::vector<std::string> UseVRC7PatchNames
	);

	void SetupMixer(
		int LowCut,
		int HighCut,
		int HighDamp,
		int Volume,
		bool UseSurveyMix,
		int16_t FDSLowpass,
		int16_t N163Lowpass,
		std::vector<int16_t> DeviceMixOffsets
	);

	void SetChipLevel(chip_level_t Chip, float LeveldB, bool SurveyMix = false);

	/// Commit changes if no exception is active.
	///
	/// I prefer placing all mutations in a single flat method,
	/// where all dependencies are made explicit,
	/// over depending on the ordering of methods.
	~CAPUConfig() noexcept(false);

// fields
private:
	CAPU * m_APU;
	CMixer * m_Mixer;

	int m_UncaughtExceptions;

	// Mutations.
	std::optional<int> m_ExternalSound;
	std::optional<float> m_ChipLevels[CHIP_LEVEL_COUNT];		// Chip levels, in linear gain factor scale
	std::optional<MixerConfig> m_MixerConfig;
	std::optional<EmulatorConfig> m_EmulatorConfig;
};
