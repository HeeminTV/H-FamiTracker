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

// YM2149F

#include "stdafx.h"
#include "FamiTrackerTypes.h"		// // //
#include "APU/Types.h"		// // //
#include "Sequence.h"		// // //
#include "Instrument.h"		// // //
#include "ChannelHandler.h"
#include "ChannelsYM2149F.h"
#include "APU/APU.h"
#include "InstHandler.h"		// // //
#include "SeqInstHandlerS5B.h"		// // //
#include <map>

// Static member variables, for the shared stuff in YM2149F
int			  CChannelHandlerYM2149F::s_iModes		= 0;
int			  CChannelHandlerYM2149F::s_iNoiseFreq	= 0;
int			  CChannelHandlerYM2149F::s_iNoisePrev	= -1;
int			  CChannelHandlerYM2149F::s_iDefaultNoise = 0;		// // //
unsigned char CChannelHandlerYM2149F::s_iEnvFreqHi	= 0;
unsigned char CChannelHandlerYM2149F::s_iEnvFreqLo	= 0;
bool		  CChannelHandlerYM2149F::s_bEnvTrigger	= false;		// // // 050B
int			  CChannelHandlerYM2149F::s_iEnvType	= 0;
int			  CChannelHandlerYM2149F::s_unused		= 0;		// // // 050B

// Class functions

void CChannelHandlerYM2149F::SetMode(int Chan, int Square, int Noise)
{
	Chan -= CHANID_YM2149F_CH1;

	switch (Chan) {
		case 0:
			s_iModes &= 0x36;
			break;
		case 1:
			s_iModes &= 0x2D;
			break;
		case 2:
			s_iModes &= 0x1B;
			break;
	}

	s_iModes |= (Noise << (3 + Chan)) | (Square << Chan);
}

void CChannelHandlerYM2149F::UpdateAutoEnvelope(int Period)		// // // 050B
{
	if (m_bEnvelopeEnabled && m_iAutoEnvelopeShift) {
		if (m_iAutoEnvelopeShift > 8) {
			Period >>= m_iAutoEnvelopeShift - 8 - 1;		// // // round off
			if (Period & 0x01) ++Period;
			Period >>= 1;
		}
		else if (m_iAutoEnvelopeShift < 8)
			Period <<= 8 - m_iAutoEnvelopeShift;
		s_iEnvFreqLo = Period & 0xFF;
		s_iEnvFreqHi = Period >> 8;
	}
}

void CChannelHandlerYM2149F::UpdateRegs()		// // //
{
	// Done only once
	if (s_iNoiseFreq != s_iNoisePrev)		// // //
		WriteReg(0x06, (s_iNoisePrev = s_iNoiseFreq) ^ 0x1F);
	WriteReg(0x07, s_iModes);
	WriteReg(0x0B, s_iEnvFreqLo);
	WriteReg(0x0C, s_iEnvFreqHi);
	if (s_bEnvTrigger)		// // // 050B
		WriteReg(0x0D, s_iEnvType);
	s_bEnvTrigger = false;
}

// Instance functions

CChannelHandlerYM2149F::CChannelHandlerYM2149F() : 
	CChannelHandler(0xFFF, 0x0F),
	m_bEnvelopeEnabled(false),		// // // 050B
	m_iAutoEnvelopeShift(0),		// // // 050B
	m_bUpdate(false)
{
	m_iDefaultDuty = YM2149F_MODE_SQUARE;		// // //
	s_iDefaultNoise = 0;		// // //
}


using EffParamT = unsigned char;
static const std::map<EffParamT, ym2149f_mode_t> VXX_TO_DUTY = {
	{1<<0, YM2149F_MODE_SQUARE},
	{1<<1, YM2149F_MODE_NOISE},
	{1<<2, YM2149F_MODE_ENVELOPE},
};

const char CChannelHandlerYM2149F::MAX_DUTY = 0x07;		// = 1|2|4

int CChannelHandlerYM2149F::getDutyMax() const {
	return MAX_DUTY;
}


bool CChannelHandlerYM2149F::HandleEffect(effect_t EffNum, EffParamT EffParam)
{
	switch (EffNum) {
	case EF_SUNSOFT_NOISE: // W
		s_iDefaultNoise = s_iNoiseFreq = EffParam & 0x1F;		// // // 050B
		break;
	case EF_SUNSOFT_ENV_HI: // I
		s_iEnvFreqHi = EffParam;
		break;
	case EF_SUNSOFT_ENV_LO: // J
		s_iEnvFreqLo = EffParam;
		break;
	case EF_SUNSOFT_ENV_TYPE: // H
		s_bEnvTrigger = true;		// // // 050B
		s_iEnvType = EffParam & 0x0F;
		m_bUpdate = true;
		m_bEnvelopeEnabled = EffParam != 0;
		m_iAutoEnvelopeShift = EffParam >> 4;
		break;
	case EF_DUTY_CYCLE: {
		/*
		Translate Vxx bitmask to `enum DutyType` bitmask, using VXX_TO_DUTY
		as a conversion table.

		CSeqInstHandlerYM2149F::ProcessSequence loads m_iDutyPeriod from the top
		3 bits of an instrument's duty sequence. (The bottom 5 go to m_iNoiseFreq.)
		This function moves Vxx to the top 3 bits of m_iDutyPeriod.
		*/

		unsigned char duty = 0;
		for (auto const&[VXX, DUTY] : VXX_TO_DUTY) {
			if (EffParam & VXX) {
				duty |= DUTY;
			}
		}

		m_iDefaultDuty = m_iDutyPeriod = duty;
		break;
	}
	default: return CChannelHandler::HandleEffect(EffNum, EffParam);
	}

	return true;
}

void CChannelHandlerYM2149F::HandleNote(int Note, int Octave)		// // //
{
	CChannelHandler::HandleNote(Note, Octave);

	/*
	Vxx is handled above: CChannelHandlerYM2149F::HandleEffect, case EF_DUTY_CYCLE
	m_iDefaultDuty is Vxx.
	m_iDutyPeriod is Vxx plus instrument bit-flags. But it's not fully
		initialized yet (instruments are handled after notes) which is bad.
	https://docs.google.com/document/d/e/2PACX-1vQ8osh6mm4c4Ay_gVMIJCH8eRB5gBE180Xyeda1T5U6owG7BbKM-yNKVB8azg27HUD9QZ9Vf88crplE/pub
	*/

	if (this->m_iDefaultDuty & YM2149F_MODE_NOISE) {
		s_iNoiseFreq = s_iDefaultNoise;
	}
}

void CChannelHandlerYM2149F::HandleEmptyNote()
{
}

void CChannelHandlerYM2149F::HandleCut()
{
	CutNote();
	m_iDutyPeriod = YM2149F_MODE_SQUARE;
	m_iNote = 0;
}

void CChannelHandlerYM2149F::HandleRelease()
{
	if (!m_bRelease)
		ReleaseNote();		// // //
}

bool CChannelHandlerYM2149F::CreateInstHandler(inst_type_t Type)
{
	switch (Type) {
	case INST_2A03: case INST_VRC6: case INST_N163: case INST_S5B: case INST_FDS:
		switch (m_iInstTypeCurrent) {
		case INST_2A03: case INST_VRC6: case INST_N163: case INST_S5B: case INST_FDS: break;
		default:
			m_pInstHandler.reset(new CSeqInstHandlerS5B(this, 0x0F, Type == INST_S5B ? 0x40 : 0));
			return true;
		}
	}
	return false;
}

void CChannelHandlerYM2149F::WriteReg(int Reg, int Value)
{
	WriteRegister(0xC003, Reg);
	WriteRegister(0xE003, Value);
}

void CChannelHandlerYM2149F::ResetChannel()
{
	CChannelHandler::ResetChannel();

	m_iDefaultDuty = m_iDutyPeriod = YM2149F_MODE_SQUARE;
	s_iDefaultNoise = s_iNoiseFreq = 0;		// // //
	s_iNoisePrev = -1;		// // //
	m_bEnvelopeEnabled = false;
	m_iAutoEnvelopeShift = 0;
	s_iEnvFreqHi = 0;
	s_iEnvFreqLo = 0;
	s_iEnvType = 0;
	s_unused = 0;		// // // 050B
	s_bEnvTrigger = false;
}

int CChannelHandlerYM2149F::CalculateVolume() const		// // //
{
	return LimitVolume((m_iVolume >> VOL_COLUMN_SHIFT) + m_iInstVolume - 15 - GetTremolo());
}

int CChannelHandlerYM2149F::ConvertDuty(int Duty)		// // //
{
	switch (m_iInstTypeCurrent) {
	case INST_2A03: case INST_VRC6: case INST_N163:
		return YM2149F_MODE_SQUARE;
	default:
		return Duty;
	}
}

void CChannelHandlerYM2149F::ClearRegisters()
{
	WriteReg(8 + m_iChannelID - CHANID_YM2149F_CH1, 0);		// Clear volume
}

CString CChannelHandlerYM2149F::GetCustomEffectString() const		// // //
{
	CString str = _T("");

	if (s_iEnvFreqLo)
		str.AppendFormat(_T(" H%02X"), s_iEnvFreqLo);
	if (s_iEnvFreqHi)
		str.AppendFormat(_T(" I%02X"), s_iEnvFreqHi);
	if (s_iEnvType)
		str.AppendFormat(_T(" J%02X"), s_iEnvType);
	if (s_iDefaultNoise)
		str.AppendFormat(_T(" W%02X"), s_iDefaultNoise);

	return str;
}

void CChannelHandlerYM2149F::RefreshChannel()
{
	int Period = CalculatePeriod();
	unsigned char LoPeriod = Period & 0xFF;
	unsigned char HiPeriod = Period >> 8;
	int Volume = CalculateVolume();

	unsigned char Noise = (m_bGate && (m_iDutyPeriod & YM2149F_MODE_NOISE)) ? 0 : 1;
	unsigned char Square = (m_bGate && (m_iDutyPeriod & YM2149F_MODE_SQUARE)) ? 0 : 1;
	unsigned char Envelope = (m_bGate && (m_iDutyPeriod & YM2149F_MODE_ENVELOPE)) ? 0x10 : 0; // m_bEnvelopeEnabled ? 0x10 : 0;

	UpdateAutoEnvelope(Period);		// // // 050B
	SetMode(m_iChannelID, Square, Noise);
	
	WriteReg((m_iChannelID - CHANID_YM2149F_CH1) * 2    , LoPeriod);
	WriteReg((m_iChannelID - CHANID_YM2149F_CH1) * 2 + 1, HiPeriod);
	WriteReg((m_iChannelID - CHANID_YM2149F_CH1) + 8    , Volume | Envelope);

	if (Envelope && (m_bTrigger || m_bUpdate))		// // // 050B
		s_bEnvTrigger = true;
	m_bUpdate = false;

	if (m_iChannelID == CHANID_YM2149F_CH3)
		UpdateRegs();
}

void CChannelHandlerYM2149F::SetNoiseFreq(int Pitch)		// // //
{
	s_iNoiseFreq = Pitch;
}
