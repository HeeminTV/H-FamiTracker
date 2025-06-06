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

// CConfigEmulation dialog

class CConfigEmulation : public CPropertyPage
{
	DECLARE_DYNAMIC(CConfigEmulation)

public:
	CConfigEmulation();   // standard constructor
	virtual ~CConfigEmulation();

// Dialog Data
	enum { IDD = IDD_CONFIG_EMULATION };

private:

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	// N163
	bool	m_bDisableNamcoMultiplex;

	void UpdateSliderTexts();

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnBnClickedN163Multiplexer();
	afx_msg void OnCbnSelchangeComboVrc7Patch();
	afx_msg void OnEnKillfocusEditLowpassFDS();
	afx_msg void OnEnKillfocusEditLowpassN163();
	afx_msg void OnEnChangeEditLowpassFDS();
	afx_msg void OnEnChangeEditLowpassN163();
	afx_msg void OnDeltaposSpinLowpassFDS(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDeltaposSpinLowpassN163(NMHDR* pNMHDR, LRESULT* pResult);
};
