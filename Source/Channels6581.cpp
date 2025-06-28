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

// MOS Technology 6581

#include "stdafx.h"
#include "FamiTracker.h" // Heemin modification
#include "FamiTrackerTypes.h"		// // //
#include "APU/Types.h"		// // //
#include "Sequence.h"		// // //
#include "Instrument.h"		// // //
#include "ChannelHandler.h"
#include "Channels6581.h"
#include "APU/APU.h"
#include "InstHandler.h"		// // //
#include "SeqInstHandler.h"		// // //
#include "InstrumentSID.h"
#include "SeqInstHandlerSID.h"
#include <map>

// Static member variables, for the shared stuff in 6581
unsigned char			  CChannelHandler6581::s_iGlobalVolume = 15;
unsigned char			  CChannelHandler6581::s_iFilterResonance = 0;
unsigned int			  CChannelHandler6581::s_iFilterCutoff = 0;
unsigned char			  CChannelHandler6581::s_iFilterMode = 0;
unsigned char			  CChannelHandler6581::s_iFilterEnable = 0;
uint8_t					  CChannelHandler6581::s_iForceGate = 2;

// Class functions


//void CChannelHandler6581::UpdateRegs()		// // //
//{
	// Done only once
	//if (s_iNoiseFreq != s_iNoisePrev)		// // //
	//	WriteReg(0x06, (s_iNoisePrev = s_iNoiseFreq) ^ 0xFF);
	//WriteReg(0x07, s_iModes);
//}

// Instance functions

CChannelHandler6581::CChannelHandler6581() :
	CChannelHandler(0xFFFF, 0xF),
	m_bUpdate(false),
	m_iEnvAD(0),
	m_iEnvSR(0)
{
	m_iDefaultDuty = 4;		// // //
	SetLinearPitch(true);
}



const char CChannelHandler6581::MAX_DUTY = 0x0F;

int CChannelHandler6581::GetDutyMax() const {
	return MAX_DUTY;
}


bool CChannelHandler6581::HandleEffect(effect_t EffNum, unsigned char EffParam)
{
	switch (EffNum) {
	case EF_DUTY_CYCLE: {
		m_iDefaultDuty = m_iDutyPeriod = EffParam;
		break;
	}
	case EF_AY8930_PULSE_WIDTH: {
		m_iPulseWidth = EffParam << 4;
		break;
	}
	case EF_SID_FILTER_RESONANCE: {
		s_iFilterResonance = EffParam & 0xF;
		break;
	}
	case EF_SID_FILTER_CUTOFF_HI: {
		s_iFilterCutoff = (s_iFilterCutoff & 0xFF0) | ((EffParam & 0xF));
		break;
	}
	case EF_SID_FILTER_CUTOFF_LO: {
		s_iFilterCutoff = (s_iFilterCutoff & 0xF) | ((EffParam & 0xFF) << 4);
		break;
	}
	case EF_SID_FILTER_MODE: {
		if (EffParam == 0)
			s_iFilterEnable &= ~(1U << (m_iChannelID - CHANID_6581_CH1));
		else {
			s_iFilterEnable |= (1U << (m_iChannelID - CHANID_6581_CH1));
			s_iFilterMode = EffParam & 0xF;
		}
		break;
	}
	case EF_SID_ENVELOPE: {
		switch (EffParam >> 4) {
		case 0:
			m_iEnvAD = (m_iEnvAD & 0x0F) | ((EffParam & 0x0F) << 4);
		case 1:
			m_iEnvAD = (m_iEnvAD & 0xF0) | (EffParam & 0x0F);
		case 2:
			m_iEnvSR = (m_iEnvSR & 0x0F) | ((EffParam & 0x0F) << 4);
		case 3:
			m_iEnvSR = (m_iEnvSR & 0xF0) | (EffParam & 0x0F);
		}
		s_iFilterCutoff = (s_iFilterCutoff & 0xF) | ((EffParam & 0xFF) << 4);
		break;
	}
	case EF_SID_RING: {
		m_iTestBit = (EffParam & 0b1000) >> 3;
		m_iRingBit = (EffParam & 0b0100) >> 2;
		m_iSyncBit = (EffParam & 0b0010) >> 1;
		m_iGateBit = (EffParam & 0b0001) >> 0;
		break;
	}
	case EF_SID_GATE_MODE: {
		s_iForceGate = EffParam <= 2 ? EffParam : 2;
	}
	default: return CChannelHandler::HandleEffect(EffNum, EffParam);
	}

	return true;
}

void CChannelHandler6581::HandleNote(int Note, int Octave)		// // //
{
	CChannelHandler::HandleNote(Note, Octave);

	/*
	Vxx is handled above: CChannelHandler6581::HandleEffect, case EF_DUTY_CYCLE
	m_iDefaultDuty is Vxx.
	m_iDutyPeriod is Vxx plus instrument bit-flags. But it's not fully
		initialized yet (instruments are handled after notes) which is bad.
	https://docs.google.com/document/d/e/2PACX-1vQ8osh6mm4c4Ay_gVMIJCH8eRB5gBE180Xyeda1T5U6owG7BbKM-yNKVB8azg27HUD9QZ9Vf88crplE/pub
  */
}


void CChannelHandler6581::HandleNoteData(stChanNote* pNoteData, int EffColumns)
{
	CChannelHandler::HandleNoteData(pNoteData, EffColumns);
	if (pNoteData->Note != 0 && pNoteData->Note != RELEASE && pNoteData->Note != HALT)
		m_iGateCounter = 1;//((pNoteData->Instrument != HOLD_INSTRUMENT && pNoteData->Instrument != RELEASE_INSTRUMENT && pNoteData->Instrument != CUT_INSTRUMENT) || m_iGateBit == 0) ? 1 : 3;

	//if (pNoteData->Vol < MAX_VOLUME) {
	//	s_iGlobalVolume = pNoteData->Vol & 15;
	//}
}


void CChannelHandler6581::HandleEmptyNote()
{

}

void CChannelHandler6581::HandleCut()
{
	CutNote();
	m_iDutyPeriod = 4;
	m_iNote = 0;
	m_iGateBit = 0;
	m_iEnvAD = 0;
	m_iEnvSR = 0;
}


void CChannelHandler6581::HandleRelease()
{
	if (!m_bRelease) {
		m_iGateBit = 0;
		ReleaseNote();		// // //
	}
}

bool CChannelHandler6581::CreateInstHandler(inst_type_t Type)
{
	switch (Type) {
	case INST_2A03: case INST_VRC6: case INST_N163: case INST_S5B: case INST_FDS: case INST_SID:
		switch (m_iInstTypeCurrent) {
		case INST_2A03: case INST_VRC6: case INST_N163: case INST_S5B: case INST_FDS: case INST_SID: break;
		default:
			m_pInstHandler.reset(new CSeqInstHandlerSID(this, 0x0F, 0x01));
			return true;
		}
	}
	return false;
}

void CChannelHandler6581::WriteReg(int Reg, int Value)
{
	WriteRegister(0xD400 + Reg, Value);
}

void CChannelHandler6581::ResetChannel()
{
	CChannelHandler::ResetChannel();

	m_iDefaultDuty = m_iDutyPeriod = 4;
	m_iPulseWidth = 0x800;
	m_iGateBit = 0;
	m_iTestBit = 0;
	m_iGateCounter = 0;
	s_iGlobalVolume = 15;
	s_iFilterResonance = 0;
	s_iFilterCutoff = 0;
	s_iFilterMode = 0;
	s_iFilterEnable = 0;
	s_iForceGate = 2;
	m_iEnvAD = 0;
	m_iEnvSR = 0;
	m_iCurVol = m_iVolume;
	SetLinearPitch(true);
}

int CChannelHandler6581::CalculateVolume() const		// // //
{
	return LimitVolume((m_iVolume >> VOL_COLUMN_SHIFT) - GetTremolo() + m_iInstVolume - 15);
}

int CChannelHandler6581::ConvertDuty(int Duty)		// // //
{
	return Duty;
}

void CChannelHandler6581::ClearRegisters()
{
	//	WriteReg(0x18, 15);
}

CString CChannelHandler6581::GetSlideEffectString() const		// // //
{
	CString str = _T("");
	switch (m_iEffect) {
	case EF_ARPEGGIO:
		if (m_iEffectParam) str.AppendFormat(_T(" %c%02X"), EFF_CHAR[m_iEffect], m_iEffectParam); break;
	case EF_PORTA_UP:
		if (m_iPortaSpeed) str.AppendFormat(_T(" %c%02X"), EFF_CHAR[EF_PORTA_DOWN], m_iPortaSpeed);  break;
	case EF_PORTA_DOWN:
		if (m_iPortaSpeed) str.AppendFormat(_T(" %c%02X"), EFF_CHAR[EF_PORTA_UP], m_iPortaSpeed); break;
	case EF_PORTAMENTO:
		if (m_iPortaSpeed) str.AppendFormat(_T(" %c%02X"), EFF_CHAR[m_iEffect], m_iPortaSpeed); break;
	}

	return str;
}

CString CChannelHandler6581::GetCustomEffectString() const		// // //
{
	CString str = _T("");

	return str;
}

void CChannelHandler6581::RefreshChannel()
{

	// Channel address offset
	unsigned int Offset = 7 * (m_iChannelID - CHANID_6581_CH1);

	// Calculate values
	int Period = CalculatePeriod();
	unsigned char LoFreq = (Period & 0xFF);
	unsigned char HiFreq = (Period >> 8);

	unsigned char LoPW = (m_iPulseWidth & 0xFF);
	unsigned char HiPW = (m_iPulseWidth >> 8);

	WriteReg(0x05 + Offset, m_iEnvAD);
	WriteReg(0x06 + Offset, m_iEnvSR);

	//if (m_iCurVol != m_iInstVolume && m_iInstVolume < MAX_VOLUME) {
	//	m_iCurVol = m_iInstVolume;
	//	s_iGlobalVolume = m_iInstVolume;
	//}

	if (m_iGateCounter > 0) {
		if (m_iGateCounter < 2) {
			m_iGateCounter++;
			m_iGateBit = 0;
			WriteReg(0x05 + Offset, 0x00);
			WriteReg(0x06 + Offset, 0x00);
		}
		else {
			m_iGateBit = 1;
			m_iGateCounter = 0;
		}
	}


	unsigned char Waveform = (m_iDutyPeriod & 15) << 4;
	Waveform |= 
		s_iForceGate == 0x02 ? m_iGateBit : s_iForceGate
	| (m_iSyncBit << 1) | (m_iRingBit << 2) | (m_iTestBit << 3);

	WriteReg(0x00 + Offset, LoFreq);
	WriteReg(0x01 + Offset, HiFreq);
	WriteReg(0x02 + Offset, LoPW);
	WriteReg(0x03 + Offset, HiPW);
	WriteReg(0x04 + Offset, Waveform);
	WriteReg(0x15, (s_iFilterCutoff & 0xF));
	WriteReg(0x16, (s_iFilterCutoff & 0xFF0) >> 4);
	WriteReg(0x17, (s_iFilterResonance << 4) | s_iFilterEnable);
	WriteReg(0x18, (s_iFilterMode << 4) | s_iGlobalVolume);

}

void CChannelHandler6581::SetADSR(unsigned char EnvAD, unsigned char EnvSR)
{
	m_iEnvAD = EnvAD;
	m_iEnvSR = EnvSR;
}

void CChannelHandler6581::SetPulseWidth(unsigned int PulseWidth)
{
	m_iPulseWidth = PulseWidth;
}

void CChannelHandler6581::SetFilterCutoff(unsigned int FilterCutoff)
{
	s_iFilterCutoff = FilterCutoff;
}

unsigned int CChannelHandler6581::GetPulseWidth() const
{
	return m_iPulseWidth;
}