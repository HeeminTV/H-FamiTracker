/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
**
** 0CC-FamiTracker is (C) 2014-2015 HertzDevil
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

// Microchip AY8930

#include "stdafx.h"
#include "FamiTrackerTypes.h"		// // //
#include "APU/Types.h"		// // //
#include "Sequence.h"		// // //
#include "Instrument.h"		// // //
#include "ChannelHandler.h"
#include "ChannelsAY8930.h"
#include "APU/APU.h"
#include "InstHandler.h"		// // //
#include "SeqInstHandlerS5B.h"		// // //
#include <map>

// Static member variables, for the shared stuff in AY8930
int			  CChannelHandlerAY8930::s_iModes		= 0;
int			  CChannelHandlerAY8930::s_iNoiseFreq	= 0;
int			  CChannelHandlerAY8930::s_iNoisePrev	= -1;
int			  CChannelHandlerAY8930::s_iDefaultNoise = 0;		// // //
int			  CChannelHandlerAY8930::s_iNoiseANDMask = 0x00;		// // //
int			  CChannelHandlerAY8930::s_iNoiseORMask = 0xFF;		// // //
int			  CChannelHandlerAY8930::s_unused		= 0;		// // // 050B

// Class functions

void CChannelHandlerAY8930::SetMode(int Chan, int Square, int Noise)
{
	Chan -= CHANID_AY8930_CH1;

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

void CChannelHandlerAY8930::UpdateAutoEnvelope(int Period)		// // // 050B
{
	if (m_bEnvelopeEnabled && m_iAutoEnvelopeShift) {
		if (m_iAutoEnvelopeShift > 8) {
			Period >>= m_iAutoEnvelopeShift - 8 - 1;		// // // round off
			if (Period & 0x01) ++Period;
			Period >>= 1;
		}
		else if (m_iAutoEnvelopeShift < 8)
			Period <<= 8 - m_iAutoEnvelopeShift;
		m_iEnvFreqLo = Period & 0xFF;
		m_iEnvFreqHi = Period >> 8;
	}
}

void CChannelHandlerAY8930::UpdateRegs()		// // //
{
	// Done only once
	if (s_iNoiseFreq != s_iNoisePrev)		// // //
		WriteReg(0x06, (s_iNoisePrev = s_iNoiseFreq) ^ 0xFF);
	WriteReg(0x07, s_iModes);
	WriteReg(0x19, s_iNoiseANDMask);
	WriteReg(0x1A, s_iNoiseORMask);
}

// Instance functions

CChannelHandlerAY8930::CChannelHandlerAY8930() : 
	CChannelHandler(0xFFFF, 0x1F),
	m_bEnvelopeEnabled(false),		// // // 050B
	m_iAutoEnvelopeShift(0),		// // // 050B
	m_iEnvFreqHi(0),		// // // 050B
  m_iEnvFreqLo(0),		// // // 050B
  m_bEnvTrigger(false),		// // // 050B
  m_iEnvType(0),		// // // 050B
	m_bUpdate(false)
{
	m_iDefaultDuty = AY8930_MODE_SQUARE;		// // //
	s_iDefaultNoise = 0;		// // //
}


using EffParamT = unsigned char;
static const std::map<EffParamT, ay8930_mode_t> VXX_TO_DUTY = {
	{1<<0, AY8930_MODE_SQUARE},
	{1<<1, AY8930_MODE_NOISE},
	{1<<2, AY8930_MODE_ENVELOPE},
};

const char CChannelHandlerAY8930::MAX_DUTY = 0x07;		// = 1|2|4

int CChannelHandlerAY8930::getDutyMax() const {
	return MAX_DUTY;
}


bool CChannelHandlerAY8930::HandleEffect(effect_t EffNum, EffParamT EffParam)
{
	switch (EffNum) {
	case EF_SUNSOFT_NOISE: // W
		s_iDefaultNoise = s_iNoiseFreq = EffParam & 0xFF;		// // // 050B
		break;
	case EF_SUNSOFT_ENV_HI: // I
		m_iEnvFreqHi = EffParam;
		break;
	case EF_SUNSOFT_ENV_LO: // J
		m_iEnvFreqLo = EffParam;
		break;
	case EF_SUNSOFT_ENV_TYPE: // H
		m_bEnvTrigger = true;		// // // 050B
		m_iEnvType = EffParam & 0x0F;
		m_bUpdate = true;
		m_bEnvelopeEnabled = EffParam != 0;
		m_iAutoEnvelopeShift = EffParam >> 4;
		break;
	case EF_AY8930_PULSE_WIDTH: // X
		m_iPulseWidth = EffParam & 0x0F;
		break;
	case EF_AY8930_AND_MASK: // Y
		s_iNoiseANDMask = EffParam;
		break;
	case EF_AY8930_OR_MASK: // Z
		s_iNoiseORMask = EffParam;
		break;
	case EF_AY8930_VOL:
		m_iExVolume = EffParam & 1;
		break;
	case EF_DUTY_CYCLE: {
		/*
		Translate Vxx bitmask to `enum DutyType` bitmask, using VXX_TO_DUTY
		as a conversion table.

		CSeqInstHandlerS5B::ProcessSequence loads m_iDutyPeriod from the top
		3 bits of an instrument's duty sequence. (The bottom 5 go to m_iNoiseFreq.)
		This function moves Vxx to the top 3 bits of m_iDutyPeriod.
		*/

		unsigned char duty = 0;
		for (auto const&[VXX, DUTY] : VXX_TO_DUTY) {
			if (EffParam & VXX) {
				duty |= DUTY;
			}
		}

		m_iDefaultDuty = m_iDutyPeriod = (duty);// | (EffParam & 0x0F);
		break;
	}
	default: return CChannelHandler::HandleEffect(EffNum, EffParam);
	}

	return true;
}

void CChannelHandlerAY8930::HandleNote(int Note, int Octave)		// // //
{
	CChannelHandler::HandleNote(Note, Octave);

	/*
	Vxx is handled above: CChannelHandlerAY8930::HandleEffect, case EF_DUTY_CYCLE
	m_iDefaultDuty is Vxx.
	m_iDutyPeriod is Vxx plus instrument bit-flags. But it's not fully
		initialized yet (instruments are handled after notes) which is bad.
	https://docs.google.com/document/d/e/2PACX-1vQ8osh6mm4c4Ay_gVMIJCH8eRB5gBE180Xyeda1T5U6owG7BbKM-yNKVB8azg27HUD9QZ9Vf88crplE/pub
	*/

	if (this->m_iDefaultDuty & AY8930_MODE_NOISE) {
		s_iNoiseFreq = s_iDefaultNoise;
	}
}

void CChannelHandlerAY8930::HandleEmptyNote()
{
}

void CChannelHandlerAY8930::HandleCut()
{
	CutNote();
	m_iDutyPeriod = AY8930_MODE_SQUARE;
	m_iNote = 0;
}

void CChannelHandlerAY8930::HandleRelease()
{
	if (!m_bRelease)
		ReleaseNote();		// // //
}

bool CChannelHandlerAY8930::CreateInstHandler(inst_type_t Type)
{
	switch (Type) {
	case INST_2A03: case INST_VRC6: case INST_N163: case INST_S5B: case INST_FDS:
		switch (m_iInstTypeCurrent) {
		case INST_2A03: case INST_VRC6: case INST_N163: case INST_S5B: case INST_FDS: break;
		default:
			m_pInstHandler.reset(new CSeqInstHandlerS5B(this, 0x1F, 0x1F, Type == INST_S5B ? 0x40 : 0x08));
			return true;
		}
	}
	return false;
}

void CChannelHandlerAY8930::WriteReg(int Reg, int Value)
{
	WriteRegister(0xC001, Reg);
	WriteRegister(0xE001, Value);
}

void CChannelHandlerAY8930::ResetChannel()
{
	CChannelHandler::ResetChannel();

	m_iDefaultDuty = m_iDutyPeriod = AY8930_MODE_SQUARE;
	s_iDefaultNoise = s_iNoiseFreq = 0;		// // //
	s_iNoiseORMask = 0x00;		// // //
	s_iNoiseANDMask = 0x0F;   // // //
	s_iNoisePrev = -1;		// // //
	m_bEnvelopeEnabled = false;
	m_iAutoEnvelopeShift = 0;
	m_iEnvFreqHi = 0;
	m_iEnvFreqLo = 0;
	m_iEnvType = 0;
	m_iPulseWidth = 0;
	m_iExVolume = 0;
	s_unused = 0;		// // // 050B
	m_bEnvTrigger = false;
}

int CChannelHandlerAY8930::CalculateVolume() const		// // //
{
	return LimitVolume((((m_iVolume >> VOL_COLUMN_SHIFT) - GetTremolo())*2 + m_iInstVolume - 31) | m_iExVolume);
}

int CChannelHandlerAY8930::ConvertDuty(int Duty)		// // //
{
	switch (m_iInstTypeCurrent) {
	case INST_2A03: case INST_VRC6: case INST_N163:
		return AY8930_MODE_SQUARE;
	default:
		return Duty;
	}
}

void CChannelHandlerAY8930::ClearRegisters()
{
	WriteReg(8 + m_iChannelID - CHANID_AY8930_CH1, 0);		// Clear volume
	WriteReg(0x16 + m_iChannelID - CHANID_AY8930_CH1, 0); // Clear duty
}

CString CChannelHandlerAY8930::GetCustomEffectString() const		// // //
{
	CString str = _T("");

	if (m_iEnvFreqLo)
		str.AppendFormat(_T(" H%02X"), m_iEnvFreqLo);
	if (m_iEnvFreqHi)
		str.AppendFormat(_T(" I%02X"), m_iEnvFreqHi);
	if (m_iEnvType)
		str.AppendFormat(_T(" J%02X"), m_iEnvType);
	if (s_iDefaultNoise)
		str.AppendFormat(_T(" W%02X"), s_iDefaultNoise);
	if (m_iPulseWidth)
		str.AppendFormat(_T(" X%02X"), m_iPulseWidth);
	if (s_iNoiseANDMask)
		str.AppendFormat(_T(" Y%02X"), s_iNoiseANDMask);
	if (s_iNoiseORMask)
		str.AppendFormat(_T(" Z%02X"), s_iNoiseORMask);
	if (m_iExVolume)
		str.AppendFormat(_T(" S%02X"), m_iExVolume);

	return str;
}

void CChannelHandlerAY8930::RefreshChannel()
{
	int Period = CalculatePeriod();
	unsigned char LoPeriod = Period & 0xFF;
	unsigned char HiPeriod = Period >> 8;
	int Volume = CalculateVolume();
	//unsigned char DutyCycle = m_iDutyPeriod & 0x0F;

	unsigned char Noise = (m_bGate && (m_iDutyPeriod & AY8930_MODE_NOISE)) ? 0 : 1;
	unsigned char Square = (m_bGate && (m_iDutyPeriod & AY8930_MODE_SQUARE)) ? 0 : 1;
	unsigned char Envelope = (m_bGate && (m_iDutyPeriod & AY8930_MODE_ENVELOPE)) ? 0x20 : 0; // m_bEnvelopeEnabled ? 0x10 : 0;

	UpdateAutoEnvelope(Period);		// // // 050B
	SetMode(m_iChannelID, Square, Noise);
	
	WriteReg((m_iChannelID - CHANID_AY8930_CH1) * 2    , LoPeriod);
	WriteReg((m_iChannelID - CHANID_AY8930_CH1) * 2 + 1, HiPeriod);
	WriteReg((m_iChannelID - CHANID_AY8930_CH1) + 8    , Volume | Envelope);
	WriteReg((m_iChannelID - CHANID_AY8930_CH1) + 0x16 , m_iPulseWidth);

	unsigned int FreqAddr[3] = {0x0B, 0x10, 0x12};
	unsigned int ModeAddr[3] = {0x0D, 0x14, 0x15};
	if (Envelope && (m_bTrigger || m_bUpdate))		// // // 050B
		m_bEnvTrigger = true;
	m_bUpdate = false;
	WriteReg(FreqAddr[m_iChannelID - CHANID_AY8930_CH1], m_iEnvFreqLo);
	WriteReg(FreqAddr[m_iChannelID - CHANID_AY8930_CH1] + 1, m_iEnvFreqHi);
	if (m_bEnvTrigger)		// // // 050B
		WriteReg(ModeAddr[m_iChannelID - CHANID_AY8930_CH1], m_iEnvType);
	m_bEnvTrigger = false;
	

	if (m_iChannelID == CHANID_AY8930_CH3)
		UpdateRegs();
}

void CChannelHandlerAY8930::SetNoiseFreq(int Pitch)		// // //
{
	s_iNoiseFreq =  0xFF - (0x1F - (Pitch & 0x1F));
}


void CChannelHandlerAY8930::SetExtra(int Value)		// // //
{

	switch (m_iInstTypeCurrent) {
	case INST_2A03:
		m_iPulseWidth = DUTY_AY8930_FROM_2A03[Value & 0x03];
		break;
	case INST_VRC6:
		m_iPulseWidth = DUTY_AY8930_FROM_VRC6[Value & 0x07];
		break;
	}
}