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

#include "SoundChip.h"
#include "Channel.h"

// // // 050B

class CAYChannel : public CChannel
{
public:
	friend class CAY;

	CAYChannel(CMixer *pMixer, uint8_t ID);
	
	void Process(uint32_t Time);
	void Reset();

	uint32_t GetTime();
	void Output(uint32_t Noise, uint32_t Envelope);

	double GetFrequency() const override;

private:
	uint8_t m_iVolume;
	uint32_t m_iPeriod;
	uint32_t m_iPeriodClock;

	bool m_bSquareHigh;
	bool m_bSquareDisable;
	bool m_bNoiseDisable;
};

class CAY : public CSoundChip
{
public:
	CAY(CMixer *pMixer);
	virtual ~CAY();
	
	void	Reset();
	void	Process(uint32_t Time);
	void	EndFrame();

	void	Write(uint16_t Address, uint8_t Value);
	uint8_t	Read(uint16_t Address, bool &Mapped);
	void	Log(uint16_t Address, uint8_t Value);		// // //

	double	GetFreq(int Channel) const override;		// // //

private:
	void	WriteReg(uint8_t Port, uint8_t Value);
	void	RunEnvelope(uint32_t Time);
	void	RunNoise(uint32_t Time);

private:
	CAYChannel *m_pChannel[3];

	uint8_t m_cPort;

	int m_iCounter;

	uint32_t m_iNoisePeriod;
	uint32_t m_iNoiseClock;
	uint32_t m_iNoiseState;

	uint32_t m_iEnvelopePeriod;
	uint32_t m_iEnvelopeClock;
	char m_iEnvelopeLevel;
	char m_iEnvelopeShape;
	bool m_bEnvelopeHold;
};
