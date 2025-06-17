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

//
// Derived channels, OPLL
//

enum OPLL_command_t {
	OPLL_CMD_NONE, 
	OPLL_CMD_NOTE_ON,
	OPLL_CMD_NOTE_TRIGGER,
	OPLL_CMD_NOTE_OFF, 
	OPLL_CMD_NOTE_HALT,
	OPLL_CMD_NOTE_RELEASE
};

class CChannelHandlerInterfaceOPLL;

class CChannelHandlerOPLL : public FrequencyChannelHandler, public CChannelHandlerInterfaceOPLL {		// // //
public:
	CChannelHandlerOPLL();
	void	SetChannelID(int ID) override;

	void	SetPatch(unsigned char Patch);		// // //
	void	SetCustomReg(size_t Index, unsigned char Val);		// // //

protected:
	void	HandleNoteData(stChanNote *pNoteData, int EffColumns) override;
	bool	HandleEffect(effect_t EffNum, unsigned char EffParam) override;		// // //
	void	HandleEmptyNote() override;
	void	HandleCut() override;
	void	HandleRelease() override;
	void	HandleNote(int Note, int Octave) override;
	int		RunNote(int Octave, int Note) override;		// // //
	bool	CreateInstHandler(inst_type_t Type) override;		// // //
	void	SetupSlide() override;		// // //
	int		CalculateVolume() const override;
	int		CalculatePeriod(bool MultiplyByHarmonic = true) const override;		// // //

	void	UpdateNoteRelease() override;		// // //
	int		TriggerNote(int Note) override;

protected:
	void CorrectOctave();		// // //
	unsigned int GetFnum(int Note) const;

protected:
	static bool m_bRegsDirty;
	static char m_cPatchFlag;		// // // 050B
	static unsigned char m_iPatchRegs[8];		// // // 050B

protected:
	unsigned char m_iChannel;
	char m_iPatch;

	bool	m_bHold;

	OPLL_command_t m_iCommand;

	int		m_iTriggeredNote;
	int		m_iOctave;
	int		m_iOldOctave;		// // //
	int		m_iCustomPort;		// // // 050B
};

class COPLLChannel : public CChannelHandlerOPLL {
public:
	COPLLChannel() : CChannelHandlerOPLL() { }
	void RefreshChannel();

	int getDutyMax() const override;
protected:
	static const char MAX_DUTY;		// TODO remove class constant, move to .cpp file

	void ClearRegisters();
private:
	void RegWrite(unsigned char Reg, unsigned char Value);
};

