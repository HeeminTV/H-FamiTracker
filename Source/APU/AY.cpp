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

#include <algorithm>
#include "APU.h"
#include "AY.h"
#include "../RegisterState.h"

// // // 050B
// AY-3-8910 channel class

const int32_t EXP_VOLUME[32] = {
		1, 	1, 	2, 	2, 
		3, 	3, 	4, 	4, 
		6, 	6, 	8, 	8, 
		11, 11, 16, 16, 
		23, 23, 32, 32, 
		45, 45, 64, 64, 
		90, 90, 128,128, 
		181,181,255,255
};

// decreasing by i * 10 ^ (-3.0 / 20.0)


CAYChannel::CAYChannel(CMixer *pMixer, uint8_t ID) : CChannel(pMixer, SNDCHIP_AY, ID),
	m_iVolume(0),
	m_iPeriod(0),
	m_iPeriodClock(0),
	m_bSquareHigh(false),
	m_bSquareDisable(false),
	m_bNoiseDisable(false)
{
}

void CAYChannel::Process(uint32_t Time)
{
	m_iPeriodClock += Time;
	if (m_iPeriodClock >= m_iPeriod) {
		m_iPeriodClock = 0;
		m_bSquareHigh = !m_bSquareHigh;
	}
	m_iTime += Time;
}

void CAYChannel::Reset()
{
	m_iVolume = 0;
	m_iPeriod = 0;
	m_iPeriodClock = 0;
	m_bSquareHigh = false;
	m_bSquareDisable = true;
	m_bNoiseDisable = true;
}

uint32_t CAYChannel::GetTime()
{
	if (m_iPeriod < 2U || !m_iVolume)
		return 0xFFFFFU;
	return m_iPeriod - m_iPeriodClock;
}

void CAYChannel::Output(uint32_t Noise, uint32_t Envelope)
{
	int Level = ((m_iVolume & 0x20) ? Envelope : m_iVolume) & 0x1F;
	int32_t Output = EXP_VOLUME[Level];
	if (!m_bSquareDisable && !m_bSquareHigh && m_iPeriod >= 2U)
		Output = 0;
	if (!m_bNoiseDisable && !Noise)
		Output = 0;
	Mix(static_cast<int32_t>(Output) * -1);
}

double CAYChannel::GetFrequency() const		// // //
{
	if (m_bSquareDisable || !m_iPeriod)
		return 0.;
	return CAPU::BASE_FREQ_NTSC / 2. / m_iPeriod;
}



// Sunsoft 5B chip class

CAY::CAY(CMixer *pMixer) : CSoundChip(pMixer),
	m_cPort(0),
	m_iCounter(0)
{
	m_pRegisterLogger->AddRegisterRange(0x00, 0x0F);		// // //

	m_pChannel[0] = new CAYChannel(pMixer, CHANID_AY_CH1);
	m_pChannel[1] = new CAYChannel(pMixer, CHANID_AY_CH2);
	m_pChannel[2] = new CAYChannel(pMixer, CHANID_AY_CH3);
	Reset();
}

CAY::~CAY()
{
	for (auto x : m_pChannel)
		if (x)
			delete x;
}

void CAY::Reset()
{
	m_iNoiseState = 0xFFFF;
	m_iCounter = 0;
	m_iNoisePeriod = 0x1F << 5;
	m_iNoiseClock = 0;
	m_iEnvelopePeriod = 0;
	m_iEnvelopeClock = 0;
	m_iEnvelopeLevel = 0;
	m_iEnvelopeShape = 0;
	m_bEnvelopeHold = true;
	
	for (auto x : m_pChannel)
		x->Reset();
}

void CAY::Process(uint32_t Time)
{
	while (Time > 0U) {
		uint32_t TimeToRun = Time;
		if (m_iEnvelopeClock < m_iEnvelopePeriod)
			TimeToRun = std::min<uint32_t>(m_iEnvelopePeriod - m_iEnvelopeClock, TimeToRun);
		if (m_iNoiseClock < m_iNoisePeriod)
			TimeToRun = std::min<uint32_t>(m_iNoisePeriod - m_iNoiseClock, TimeToRun);
		for (const auto x : m_pChannel)
			TimeToRun = std::min<uint32_t>(x->GetTime(), TimeToRun);

		m_iCounter += TimeToRun;
		Time -= TimeToRun;

		RunEnvelope(TimeToRun);
		RunNoise(TimeToRun);
		for (auto x : m_pChannel)
			x->Process(TimeToRun);

		for (auto x : m_pChannel)
			x->Output(m_iNoiseState & 0x01, m_iEnvelopeLevel);
	}
}

void CAY::EndFrame()
{
	for (auto x : m_pChannel)
		x->EndFrame();
	m_iCounter = 0;
}

void CAY::Write(uint16_t Address, uint8_t Value)
{
	switch (Address) {
	case 0xC002:
		m_cPort = Value & 0x0F;
		break;
	case 0xE002:
		WriteReg(m_cPort, Value);
		break;
	}
}

uint8_t CAY::Read(uint16_t Address, bool &Mapped)
{
	Mapped = false;
	return 0U;
}

double CAY::GetFreq(int Channel) const		// // //
{
	switch (Channel) {
	case 0: case 1: case 2:
		return m_pChannel[Channel]->GetFrequency();
	case 3:
		if (!m_iEnvelopePeriod)
			return 0.;
		if (!(m_iEnvelopeShape & 0x08) || (m_iEnvelopeShape & 0x01))
			return 0.;
		return CAPU::BASE_FREQ_NTSC / ((m_iEnvelopeShape & 0x02) ? 64. : 32.) / m_iEnvelopePeriod;
	//case 4: TODO noise refresh rate
	}
	return 0.;
}

void CAY::WriteReg(uint8_t Port, uint8_t Value)
{
	switch (Port) {
	case 0x00: case 0x02: case 0x04:
	{
		auto pChan = m_pChannel[Port >> 1];
		pChan->m_iPeriod = (pChan->m_iPeriod & 0xF000) | (Value << 4);
	}
		break;
	case 0x01: case 0x03: case 0x05:
	{
		auto pChan = m_pChannel[Port >> 1];
		pChan->m_iPeriod = (pChan->m_iPeriod & 0x0FF0) | ((Value & 0x0F) << 12);
	}
		break;
	case 0x06:
		m_iNoisePeriod = Value ? ((Value & 0x1F) << 5) : 0x10;
		break;
	case 0x07:
		for (int i = 0; i < 3; ++i) {
			auto pChan = m_pChannel[i];
			pChan->m_bSquareDisable = (Value & (1 << i)) != 0;
			pChan->m_bNoiseDisable = (Value & (1 << (i + 3))) != 0;
		}
		break;
	case 0x08: case 0x09: case 0x0A:
		m_pChannel[Port - 0x08]->m_iVolume = Value * 2;
		break;
	case 0x0B:
		m_iEnvelopePeriod = (m_iEnvelopePeriod & 0xFF000) | (Value << 4);
		break;
	case 0x0C:
		m_iEnvelopePeriod = (m_iEnvelopePeriod & 0x00FF0) | (Value << 12);
		break;
	case 0x0D:
		m_iEnvelopeClock = 0;
		m_iEnvelopeShape = Value;
		m_bEnvelopeHold = false;
		m_iEnvelopeLevel = (Value & 0x04) ? 0 : 0x1F;
		break;
	}
}

void CAY::Log(uint16_t Address, uint8_t Value)		// // //
{
	switch (Address) {
	case 0xC002: m_pRegisterLogger->SetPort(Value); break;
	case 0xE002: m_pRegisterLogger->Write(Value); break;
	}
}

void CAY::RunEnvelope(uint32_t Time)
{
	m_iEnvelopeClock += Time;
	if (m_iEnvelopeClock >= m_iEnvelopePeriod && m_iEnvelopePeriod) {
		m_iEnvelopeClock = 0;
		if (!m_bEnvelopeHold) {
			m_iEnvelopeLevel += (m_iEnvelopeShape & 0x04) ? 1 : -1;
			m_iEnvelopeLevel &= 0x3F;
		}
		if (m_iEnvelopeLevel & 0x20) {
			if (m_iEnvelopeShape & 0x08) {
				if ((m_iEnvelopeShape & 0x03) == 0x01 || (m_iEnvelopeShape & 0x03) == 0x02)
					m_iEnvelopeShape ^= 0x04;
				if (m_iEnvelopeShape & 0x01)
					m_bEnvelopeHold = true;
				m_iEnvelopeLevel = (m_iEnvelopeShape & 0x04) ? 0 : 0x1F;
			}
			else {
				m_bEnvelopeHold = true;
				m_iEnvelopeLevel = 0;
			}
		}
	}
}

void CAY::RunNoise(uint32_t Time)
{
	m_iNoiseClock += Time;
	if (m_iNoiseClock >= m_iNoisePeriod) {
		m_iNoiseClock = 0;
		if (m_iNoiseState & 0x01)
			m_iNoiseState ^= 0x24000;
		m_iNoiseState >>= 1;
	}
}
