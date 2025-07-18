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

#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "GotoDlg.h"

// CGotoDlg dialog

IMPLEMENT_DYNAMIC(CGotoDlg, CDialog)

CGotoDlg::CGotoDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CGotoDlg::IDD, pParent)
{
}

CGotoDlg::~CGotoDlg()
{
	SAFE_RELEASE(m_cChipEdit);
}

void CGotoDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CGotoDlg, CDialog)
	ON_EN_CHANGE(IDC_EDIT_GOTO_FRAME, OnEnChangeEditGotoFrame)
	ON_EN_CHANGE(IDC_EDIT_GOTO_ROW, OnEnChangeEditGotoRow)
	ON_EN_CHANGE(IDC_EDIT_GOTO_CHANNEL, OnEnChangeEditGotoChannel)
	ON_CBN_SELCHANGE(IDC_COMBO_GOTO_CHIP, OnCbnSelchangeComboGotoChip)
	ON_BN_CLICKED(IDOK, &CGotoDlg::OnBnClickedOk)
END_MESSAGE_MAP()


// CGotoDlg message handlers

BOOL CGotoDlg::OnInitDialog()
{
	m_cChipEdit = new CComboBox();
	m_cChipEdit->SubclassDlgItem(IDC_COMBO_GOTO_CHIP, this);

	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(((CFrameWnd*)AfxGetMainWnd())->GetActiveView());
	CPatternEditor *pEditor = pView->GetPatternEditor();

	m_cChipEdit->AddString(_T("2A03"));
	if (pDoc->ExpansionEnabled(SNDCHIP_VRC6))
		m_cChipEdit->AddString(_T("VRC6"));
	if (pDoc->ExpansionEnabled(SNDCHIP_VRC7))
		m_cChipEdit->AddString(_T("VRC7"));
	if (pDoc->ExpansionEnabled(SNDCHIP_FDS))
		m_cChipEdit->AddString(_T("2C33"));
	if (pDoc->ExpansionEnabled(SNDCHIP_MMC5))
		m_cChipEdit->AddString(_T("MMC5"));
	if (pDoc->ExpansionEnabled(SNDCHIP_N163))
		m_cChipEdit->AddString(_T("N163"));
	if (pDoc->ExpansionEnabled(SNDCHIP_5B))
		m_cChipEdit->AddString(_T("5B"));
	if (pDoc->ExpansionEnabled(SNDCHIP_AY8930)) // Taken from E-FamiTracker by Euly
		m_cChipEdit->AddString(_T("AY8930"));
	if (pDoc->ExpansionEnabled(SNDCHIP_AY))
		m_cChipEdit->AddString(_T("AY-3-8910"));
	if (pDoc->ExpansionEnabled(SNDCHIP_SSG))
		m_cChipEdit->AddString(_T("YM2149F"));
	if (pDoc->ExpansionEnabled(SNDCHIP_5E01)) 	// Taken from E-FamiTracker by Euly
		m_cChipEdit->AddString(_T("5E01"));
	if (pDoc->ExpansionEnabled(SNDCHIP_7E02))
		m_cChipEdit->AddString(_T("7E02"));
	if (pDoc->ExpansionEnabled(SNDCHIP_OPLL))
		m_cChipEdit->AddString(_T("YM2413"));
	if (pDoc->ExpansionEnabled(SNDCHIP_6581))
		m_cChipEdit->AddString(_T("6581"));
		
	int Channel = pDoc->GetChannelType(pEditor->GetChannel());

	if (Channel >= CHANID_6581_CH1) {
		Channel -= CHANID_6581_CH1;
		m_cChipEdit->SelectString(-1, _T("6581"));
	}
	else if (Channel >= CHANID_OPLL_CH1) {
		Channel -= CHANID_OPLL_CH1;
		m_cChipEdit->SelectString(-1, _T("YM2413"));
	}
	else if (Channel >= CHANID_7E02_SQUARE1) {
		Channel -= CHANID_7E02_SQUARE1;
		m_cChipEdit->SelectString(-1, _T("7E02"));
	}
	else if (Channel >= CHANID_5E01_SQUARE1) {
		Channel -= CHANID_5E01_SQUARE1;
		m_cChipEdit->SelectString(-1, _T("5E01"));
	} 
	else if (Channel >= CHANID_YM2149F_CH1) {
		Channel -= CHANID_YM2149F_CH1;
		m_cChipEdit->SelectString(-1, _T("YM2149F"));
	}
	else if (Channel >= CHANID_AY_CH1) {
		Channel -= CHANID_AY_CH1;
		m_cChipEdit->SelectString(-1, _T("AY-3-8910"));
	}
	else if (Channel >= CHANID_AY8930_CH1) {
		Channel -= CHANID_AY8930_CH1;
		m_cChipEdit->SelectString(-1, _T("AY8930"));
	}
	else if (Channel >= CHANID_5B_CH1) {
		Channel -= CHANID_5B_CH1;
		m_cChipEdit->SelectString(-1, _T("5B"));
	}
	else if (Channel >= CHANID_VRC7_CH1) {
		Channel -= CHANID_VRC7_CH1;
		m_cChipEdit->SelectString(-1, _T("VRC7"));
	}
	else if (Channel >= CHANID_OPLL_CH1) {
		Channel -= CHANID_OPLL_CH1;
		m_cChipEdit->SelectString(-1, _T("YM2413"));
	}
	else if (Channel >= CHANID_FDS) {
		Channel -= CHANID_FDS;
		m_cChipEdit->SelectString(-1, _T("2C33"));
	}
	else if (Channel >= CHANID_N163_CH1) {
		Channel -= CHANID_N163_CH1;
		m_cChipEdit->SelectString(-1, _T("N163"));
	}
	else if (Channel >= CHANID_MMC5_SQUARE1) {
		Channel -= CHANID_MMC5_SQUARE1;
		m_cChipEdit->SelectString(-1, _T("MMC5"));
	}
	else if (Channel >= CHANID_VRC6_PULSE1) {
		Channel -= CHANID_VRC6_PULSE1;
		m_cChipEdit->SelectString(-1, _T("VRC6"));
	}
	else
		m_cChipEdit->SelectString(-1, _T("2A03"));

	SetDlgItemInt(IDC_EDIT_GOTO_FRAME, pEditor->GetFrame());
	SetDlgItemInt(IDC_EDIT_GOTO_ROW, pEditor->GetRow());
	SetDlgItemInt(IDC_EDIT_GOTO_CHANNEL, Channel + 1);

	CEdit *pEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_GOTO_CHANNEL));
	pEdit->SetLimitText(1);
	pEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_GOTO_ROW));
	pEdit->SetLimitText(3);
	pEdit = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_GOTO_FRAME));
	pEdit->SetLimitText(3);
	pEdit->SetFocus();

	return CDialog::OnInitDialog();
}

void CGotoDlg::CheckDestination() const
{
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	int Track = static_cast<CMainFrame*>(AfxGetMainWnd())->GetSelectedTrack();

	bool Valid = true;
	if (m_iDestFrame >= pDoc->GetFrameCount(Track))
		Valid = false;
	else if (m_iDestRow >= static_cast<unsigned>(pDoc->GetFrameLength(Track, m_iDestFrame)))
		Valid = false;
	else if (GetFinalChannel() == -1)
		Valid = false;

	GetDlgItem(IDOK)->EnableWindow(Valid);
}

int CGotoDlg::GetChipFromString(const CString str)
{
	if (str == _T("2A03"))
		return SNDCHIP_NONE;
	else if (str == _T("VRC6"))
		return SNDCHIP_VRC6;
	else if (str == _T("VRC7"))
		return SNDCHIP_VRC7;
	else if (str == _T("2C33"))
		return SNDCHIP_FDS;
	else if (str == _T("MMC5"))
		return SNDCHIP_MMC5;
	else if (str == _T("N163"))
		return SNDCHIP_N163;
	else if (str == _T("5B"))
		return SNDCHIP_5B;
	else if (str == _T("AY8930"))
		return SNDCHIP_AY8930;
	else if (str == _T("AY-3-8910"))
		return SNDCHIP_AY;
	else if (str == _T("YM2149F"))
		return SNDCHIP_SSG;
	else if (str == _T("5E01"))
		return SNDCHIP_5E01;
	else if (str == _T("7E02"))
		return SNDCHIP_7E02;
	else if (str == _T("YM2413"))
		return SNDCHIP_OPLL;
	else if (str == _T("6581"))
		return SNDCHIP_6581;

	else
		return SNDCHIP_NONE;
}

int CGotoDlg::GetFinalChannel() const
{
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();

	int Channel = m_iDestChannel;
	switch (m_iDestChip) {
		case SNDCHIP_VRC6:	Channel += CHANID_VRC6_PULSE1; break;
		case SNDCHIP_VRC7:	Channel += CHANID_VRC7_CH1; break;
		case SNDCHIP_FDS:	Channel += CHANID_FDS; break;
		case SNDCHIP_MMC5:	Channel += CHANID_MMC5_SQUARE1; break;
		case SNDCHIP_N163:	Channel += CHANID_N163_CH1; break;
		case SNDCHIP_5B:	Channel += CHANID_5B_CH1; break;
		case SNDCHIP_AY8930:Channel += CHANID_AY8930_CH1; break;
		case SNDCHIP_AY:	Channel += CHANID_AY_CH1; break;
		case SNDCHIP_SSG:	Channel += CHANID_YM2149F_CH1; break;
		case SNDCHIP_5E01:	Channel += CHANID_5E01_SQUARE1; break; // Taken from E-FamiTracker by Euly
		case SNDCHIP_7E02:	Channel += CHANID_7E02_SQUARE1; break;
		case SNDCHIP_OPLL:	Channel += CHANID_OPLL_CH1; break;
		case SNDCHIP_6581:	Channel += CHANID_6581_CH1; break;
	}

	return pDoc->GetChannelIndex(Channel);
}

void CGotoDlg::OnEnChangeEditGotoFrame()
{
	m_iDestFrame = GetDlgItemInt(IDC_EDIT_GOTO_FRAME);
	CheckDestination();
}

void CGotoDlg::OnEnChangeEditGotoRow()
{
	m_iDestRow = GetDlgItemInt(IDC_EDIT_GOTO_ROW);
	CheckDestination();
}

void CGotoDlg::OnEnChangeEditGotoChannel()
{
	m_iDestChannel = GetDlgItemInt(IDC_EDIT_GOTO_CHANNEL) - 1;
	CheckDestination();
}

void CGotoDlg::OnCbnSelchangeComboGotoChip()
{
	CString str;
	m_cChipEdit->GetWindowText(str);
	m_iDestChip = GetChipFromString(str);
	CheckDestination();
}

void CGotoDlg::OnBnClickedOk()
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(((CFrameWnd*)AfxGetMainWnd())->GetActiveView());
	pView->SelectFrame(m_iDestFrame);
	pView->SelectRow(m_iDestRow);
	pView->SelectChannel(GetFinalChannel());

	CDialog::OnOK();
}
