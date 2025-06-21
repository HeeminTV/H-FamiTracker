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

// This file handles playing of OPLL channels

#include "stdafx.h"
#include "FamiTrackerTypes.h"		// // //
#include "APU/Types.h"		// // //
#include "Instrument.h"		// // //
#include "ChannelHandler.h"
#include "ChannelsOPLL.h"
#include "InstHandler.h"		// // //
#include "InstHandlerOPLL.h"		// // //

int g_iPercMode;	// Taken from 0CC-LLTracker
int g_iPercModePrev;
int g_iPercVolumeBD;
int g_iPercVolumeSDHH;
int g_iPercVolumeTOMCY;

#define OPL_NOTE_ON 0x10
#define OPL_SUSTAIN_ON 0x20

const int OPLL_PITCH_RESOLUTION = 2;		// // // extra bits for internal pitch

// True if custom instrument registers needs to be updated, shared among all channels
bool CChannelHandlerOPLL::m_bRegsDirty = false;
// Each bit represents that the custom patch register on that index has been updated
char CChannelHandlerOPLL::m_cPatchFlag = 0;		// // // 050B
// Custom instrument patch
unsigned char CChannelHandlerOPLL::m_iPatchRegs[8];		// // // 050B

CChannelHandlerOPLL::CChannelHandlerOPLL() : 
	FrequencyChannelHandler((1 << (OPLL_PITCH_RESOLUTION + 9)) - 1, 15),		// // //
	m_iCommand(OPLL_CMD_NONE),
	m_iTriggeredNote(0)
{
	m_iVolume = VOL_COLUMN_MAX;

	g_iPercMode = 0; // Taken from 0CC-LLTracker
	g_iPercModePrev = 0;
	g_iPercVolumeBD = 15;
	g_iPercVolumeSDHH = 15;
	g_iPercVolumeTOMCY = 15;
}

void CChannelHandlerOPLL::SetChannelID(int ID)
{
	CChannelHandler::SetChannelID(ID);
	m_iChannel = ID - CHANID_OPLL_CH1;
}

void CChannelHandlerOPLL::SetPatch(unsigned char Patch)		// // //
{
	m_iDutyPeriod = Patch;
}

void CChannelHandlerOPLL::SetCustomReg(size_t Index, unsigned char Val)		// // //
{
	ASSERT(Index < sizeof(m_iPatchRegs));
	if (!(m_cPatchFlag & (1 << Index)))		// // // 050B
		m_iPatchRegs[Index] = Val;
}

void CChannelHandlerOPLL::HandleNoteData(stChanNote *pNoteData, int EffColumns)
{
	FrequencyChannelHandler::HandleNoteData(pNoteData, EffColumns);		// // //

	if (m_iCommand == OPLL_CMD_NOTE_TRIGGER && pNoteData->Instrument == HOLD_INSTRUMENT)		// // // 050B
		m_iCommand = OPLL_CMD_NOTE_ON;
}

bool CChannelHandlerOPLL::HandleEffect(effect_t EffNum, unsigned char EffParam)
{
	switch (EffNum) {
	case EF_DUTY_CYCLE:
		m_iPatch = EffParam;		// // // 050B
		break;
	case EF_VRC7_PORT:		// // // 050B
		m_iCustomPort = EffParam & 0x07;
		break;
	case EF_VRC7_WRITE:		// // // 050B
		m_iPatchRegs[m_iCustomPort] = EffParam;
		m_cPatchFlag |= 1 << m_iCustomPort;
		m_bRegsDirty = true;
		break;
	case EF_DAC:	// Taken from 0CC-LLTracker
		/*switch (EffParam & 0xf0)
		{
		case 0x00://turn percussion mode on/off
			switch (EffParam & 0x0f)
			{
			case 0x00://off
				g_iPercMode &= ~0x20;
				break;
			case 0x01://on
				g_iPercMode |= 0x20;
				break;
			}
			break;
		}
		what the heck????
		*/
		if (EffParam == 0x00) {
			g_iPercMode &= ~0x20;
		}
		else if (EffParam == 0x01) {
			g_iPercMode |= 0x20;
		}
		break;
	default: return FrequencyChannelHandler::HandleEffect(EffNum, EffParam);
	}

	return true;
}

void CChannelHandlerOPLL::HandleEmptyNote()
{
}

void CChannelHandlerOPLL::HandleCut()
{
	RegisterKeyState(-1);
	m_bGate = false;
//	m_iPeriod = 0;
//	m_iPortaTo = 0;
	m_iCommand = OPLL_CMD_NOTE_HALT;
//	m_iOldOctave = -1;		// // //
}

void CChannelHandlerOPLL::UpdateNoteRelease()		// // //
{
	// Note release (Lxx)
	if (m_iNoteRelease > 0) {
		m_iNoteRelease--;
		if (m_iNoteRelease == 0) {
			HandleRelease();
		}
	}
}

void CChannelHandlerOPLL::HandleRelease()
{
	if (!m_bRelease) {
		m_iCommand = OPLL_CMD_NOTE_RELEASE;
		RegisterKeyState(-1);
	}
}

void CChannelHandlerOPLL::HandleNote(int Note, int Octave)
{
	CChannelHandler::HandleNote(Note, Octave);		// // //

	m_bHold	= true;

/*
	if ((m_iEffect != EF_PORTAMENTO || m_iPortaSpeed == 0) ||
		m_iCommand == OPLL_CMD_NOTE_HALT || m_iCommand == OPLL_CMD_NOTE_RELEASE)		// // // 050B
		m_iCommand = OPLL_CMD_NOTE_TRIGGER;
*/
	if (m_iPortaSpeed > 0 && m_iEffect == EF_PORTAMENTO &&
		m_iCommand != OPLL_CMD_NOTE_HALT && m_iCommand != OPLL_CMD_NOTE_RELEASE)		// // // 050B
		CorrectOctave();
	else
		m_iCommand = OPLL_CMD_NOTE_TRIGGER;
}

int CChannelHandlerOPLL::RunNote(int Octave, int Note)		// // //
{
	// Run the note and handle portamento
	int NewNote = MIDI_NOTE(Octave, Note);

	int NesFreq = TriggerNote(NewNote);

	if (m_iPortaSpeed > 0 && m_iEffect == EF_PORTAMENTO && m_bGate) {		// // //
		if (m_iPeriod == 0) {
			m_iPeriod = NesFreq;
			m_iOldOctave = m_iOctave = Octave;
		}
		m_iPortaTo = NesFreq;
		
	}
	else {
		m_iPeriod = NesFreq;
		m_iPortaTo = 0;
		m_iOldOctave = m_iOctave = Octave;
	}

	m_bGate = true;

	CorrectOctave();		// // //

	return NewNote;
}

bool CChannelHandlerOPLL::CreateInstHandler(inst_type_t Type)
{
	switch (Type) {
	case INST_VRC7:
		if (m_iInstTypeCurrent != INST_VRC7)
			m_pInstHandler.reset(new CInstHandlerOPLL(this, 0x0F));
		return true;
	}
	return false;
}

void CChannelHandlerOPLL::SetupSlide()		// // //
{
	CChannelHandler::SetupSlide();		// // //
	
	CorrectOctave();
}

void CChannelHandlerOPLL::CorrectOctave()		// // //
{
	// Set current frequency to the one with highest octave
	if (m_bLinearPitch)
		return;

	if (m_iOldOctave == -1) {
		m_iOldOctave = m_iOctave;
		return;
	}

	int Offset = m_iOctave - m_iOldOctave;
	if (Offset > 0) {
		m_iPeriod >>= Offset;
		m_iOldOctave = m_iOctave;
	}
	else if (Offset < 0) {
		// Do nothing
		m_iPortaTo >>= -Offset;
		m_iOctave = m_iOldOctave;
	}
}

int CChannelHandlerOPLL::TriggerNote(int Note)
{
	m_iTriggeredNote = Note;
	RegisterKeyState(Note);
	if (m_iCommand != OPLL_CMD_NOTE_TRIGGER && m_iCommand != OPLL_CMD_NOTE_HALT)
		m_iCommand = OPLL_CMD_NOTE_ON;
	m_iOctave = Note / NOTE_RANGE;

	if (g_iPercMode & 0x20 && m_iChannelID >= CHANID_OPLL_CH7 && m_iChannelID <= CHANID_OPLL_CH9) { // Taken from 0CC-LLTracker
		switch (Note % 12)	//drum mapping similar to the MIDI drum layout
		{
		case 0:	//BD
		case 1:
			g_iPercMode |= 0x10;
			g_iPercVolumeBD = (15 - CalculateVolume());
			break;
		case 2: //SD
		case 3:
		case 4:
			g_iPercMode |= 0x08;
			g_iPercVolumeSDHH = (g_iPercVolumeSDHH & 0xf0) | (15 - CalculateVolume());
			break;
		case 5: //TOM
		case 7:
		case 9:
		case 11:
			g_iPercMode |= 0x04;
			g_iPercVolumeTOMCY = (g_iPercVolumeTOMCY & 0x0f) | ((15 - CalculateVolume()) << 4);
			break;
		case 10: //CY
			g_iPercMode |= 0x02;
			g_iPercVolumeTOMCY = (g_iPercVolumeTOMCY & 0xf0) | (15 - CalculateVolume());
			break;
		case 6: //HH
		case 8:
			g_iPercMode |= 0x01;
			g_iPercVolumeSDHH = (g_iPercVolumeSDHH & 0x0f) | ((15 - CalculateVolume()) << 4);
			break;
		}
	}

	return m_bLinearPitch ? (Note << LINEAR_PITCH_AMOUNT) : GetFnum(Note);		// // //
}

unsigned int CChannelHandlerOPLL::GetFnum(int Note) const
{
	return m_pNoteLookupTable[Note % NOTE_RANGE] << OPLL_PITCH_RESOLUTION;		// // //
}

int CChannelHandlerOPLL::CalculateVolume() const
{
	int Volume = (m_iVolume >> VOL_COLUMN_SHIFT) - GetTremolo();
	if (Volume > 15)
		Volume = 15;
	if (Volume < 0)
		Volume = 0;
	return Volume;		// // //
}

int CChannelHandlerOPLL::CalculatePeriod(bool MultiplyByHarmonic) const
{
	int Detune = GetVibrato() - GetFinePitch() - GetPitch();
	int Period = LimitPeriod(GetPeriod() + (Detune << OPLL_PITCH_RESOLUTION));		// // //
	if (m_bLinearPitch && m_pNoteLookupTable != nullptr) {
		Period = LimitPeriod(GetPeriod() + Detune);		// // //
		int Note = (Period >> LINEAR_PITCH_AMOUNT) % NOTE_RANGE;
		int Sub = Period % (1 << LINEAR_PITCH_AMOUNT);
		int Offset = (GetFnum(Note + 1) << ((Note < NOTE_RANGE - 1) ? 0 : 1)) - GetFnum(Note);
		Offset = Offset * Sub >> LINEAR_PITCH_AMOUNT;
		if (Sub && Offset < (1 << OPLL_PITCH_RESOLUTION)) Offset = 1 << OPLL_PITCH_RESOLUTION;
		Period = GetFnum(Note) + Offset;
	}
	// TODO multiply pitch by harmonic if requested.
	// This may not be implemented, since OPLL's pitch system makes multiplication hard,
	// and it already has carrier/modulator sliders, making this less useful.
	return LimitRawPeriod(Period) >> OPLL_PITCH_RESOLUTION;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// OPLL Channels
///////////////////////////////////////////////////////////////////////////////////////////////////////////

const char COPLLChannel::MAX_DUTY = 0x0F;

int COPLLChannel::getDutyMax() const {
	return MAX_DUTY;
}

void COPLLChannel::RefreshChannel()
{	
//	int Note = m_iTriggeredNote;
	int Volume = CalculateVolume();
	int Fnum = CalculatePeriod();		// // //
	int Bnum = !m_bLinearPitch ? m_iOctave :
		((GetPeriod() + GetVibrato() - GetFinePitch() - GetPitch()) >> LINEAR_PITCH_AMOUNT) / NOTE_RANGE;

	if (m_iPatch != -1) {		// // //
		m_iDutyPeriod = m_iPatch;
		m_iPatch = -1;
	}

	// Write custom instrument
	if ((m_iDutyPeriod == 0 && m_iCommand == OPLL_CMD_NOTE_TRIGGER) || m_bRegsDirty) {
		for (int i = 0; i < 8; ++i)
			RegWrite(i, m_iPatchRegs[i]);
	}

	int subindex = m_iChannelID - CHANID_OPLL_CH1;
	if (subindex == 8)	// Taken from 0CC-LLTracker
	{
		//only send all percussion related writes from one of the channels

		if (g_iPercMode & 0x20)
		{
			//repeating writes will get filtered out during export

			RegWrite(0x26, 0x00);	//force key off to percussion channels
			RegWrite(0x27, 0x00);
			RegWrite(0x28, 0x00);
			RegWrite(0x16, 0x20);	//preset values for percussion
			RegWrite(0x17, 0x50);
			RegWrite(0x18, 0xc0);
			RegWrite(0x26, 0x05);
			RegWrite(0x27, 0x05);
			RegWrite(0x28, 0x01);

			RegWrite(0x0e, g_iPercMode);	//enable rhythm mode
			RegWrite(0x36, g_iPercVolumeBD);	//percussion volume
			RegWrite(0x37, g_iPercVolumeSDHH);
			RegWrite(0x38, g_iPercVolumeTOMCY);

			g_iPercMode &= ~0x1f;
		}
		else
		{
			if (g_iPercModePrev & 0x20)
			{
				RegWrite(0x0e, 0x00);	//disable rhythm mode
				RegWrite(0x26, 0x00);	//force key off to percussion channels
				RegWrite(0x27, 0x00);
				RegWrite(0x28, 0x00);
				RegWrite(0x36, 0x1f);
				RegWrite(0x37, 0x1f);
				RegWrite(0x38, 0x1f);

			}
		}

		g_iPercModePrev = g_iPercMode;
	}

	if ((g_iPercMode & 0x20) && (subindex >= 6)) return;	//don't allow notes on the percussion channels when percussion mode is enabled

	int Cmd = 0;

	switch (m_iCommand) {
		case OPLL_CMD_NOTE_TRIGGER:
			RegWrite(0x20 + m_iChannel, 0);
			m_iCommand = OPLL_CMD_NOTE_ON;
			Cmd = OPL_NOTE_ON | OPL_SUSTAIN_ON;
			break;
		case OPLL_CMD_NOTE_ON:
			Cmd = m_bHold ? OPL_NOTE_ON : OPL_SUSTAIN_ON;
			break;
		case OPLL_CMD_NOTE_HALT:
			Cmd = 0;
			break;
		case OPLL_CMD_NOTE_RELEASE:
			Cmd = OPL_SUSTAIN_ON;
			break;
	}
	
	// Write frequency
	RegWrite(0x10 + m_iChannel, Fnum & 0xFF);
	
	if (m_iCommand != OPLL_CMD_NOTE_HALT) {
		// Select volume & patch
		RegWrite(0x30 + m_iChannel, (m_iDutyPeriod << 4) | (Volume ^ 0x0F));		// // //
	}

	RegWrite(0x20 + m_iChannel, ((Fnum >> 8) & 1) | (Bnum << 1) | Cmd);

	if (m_iChannelID == CHANID_OPLL_CH9)		// // // 050B
		m_cPatchFlag = 0;
}

void COPLLChannel::ClearRegisters()
{
	for (int i = 0x10; i < 0x30; i += 0x10)
		RegWrite(i + m_iChannel, 0);
	RegWrite(0x30 + m_iChannel, 0x0F);		// // //

	m_iNote = 0;
	m_iOctave = m_iOldOctave = -1;		// // //
	m_iPatch = -1;
	m_iEffect = EF_NONE;

	m_iCommand = OPLL_CMD_NOTE_HALT;
	m_iCustomPort = 0;		// // // 050B
}

void COPLLChannel::RegWrite(unsigned char Reg, unsigned char Value)
{
	// 패밀리 노래방 https://www.nesdev.org/wiki/NES_2.0_Mapper_515
	WriteRegister(0x6000, Reg);
	WriteRegister(0x6001, Value);
}
