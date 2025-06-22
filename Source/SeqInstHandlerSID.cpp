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

#include "stdafx.h"
#include "Sequence.h"
#include "Instrument.h"
#include "SeqInstrument.h"
#include "InstrumentSID.h"
#include "ChannelHandlerInterface.h"
#include "SeqInstHandlerSID.h"

/*
 * Class CSeqInstHandlerSID
 */

void CSeqInstHandlerSID::LoadInstrument(std::shared_ptr<CInstrument> pInst)		// // //
{
	CSeqInstHandler::LoadInstrument(pInst);

	//	if (auto pSIDInst = std::dynamic_pointer_cast<const CInstrumentSID>(m_pInstrument))
		//	UpdateTables(pSIDInst.get());
}

void CSeqInstHandlerSID::TriggerInstrument()
{
	CSeqInstHandler::TriggerInstrument();

	auto* pInterface = dynamic_cast<CChannelHandlerInterfaceSID*>(m_pInterface);
	if (pInterface == nullptr) return;
	auto pSIDInst = std::dynamic_pointer_cast<const CInstrumentSID>(m_pInstrument);
	if (pSIDInst == nullptr) return;
	if (pSIDInst.get()->GetPWMMode() != PWM_SUSTAIN) {
		m_pPWMValue = pSIDInst.get()->GetPWMStart();
		m_pPWMStart = pSIDInst.get()->GetPWMStart();
		m_pPWMEnd = pSIDInst.get()->GetPWMEnd();
		m_pPWMSpeed = pSIDInst.get()->GetPWMSpeed();
		m_pPWMMode = pSIDInst.get()->GetPWMMode();
		m_pPWMDirection = m_pPWMEnd > m_pPWMStart ? 1 : -1;
	}
	if (pSIDInst.get()->GetFilterMode() != PWM_SUSTAIN) {
		m_pFilterValue = pSIDInst.get()->GetFilterStart();
		m_pFilterStart = pSIDInst.get()->GetFilterStart();
		m_pFilterEnd = pSIDInst.get()->GetFilterEnd();
		m_pFilterSpeed = pSIDInst.get()->GetFilterSpeed();
		m_pFilterMode = pSIDInst.get()->GetFilterMode();
		//m_pFilterPass= pSIDInst.get()->GetFilterPass();
		m_pFilterDirection = m_pFilterEnd > m_pFilterStart ? 1 : -1;
	}
	pInterface->SetADSR(
		(pSIDInst->GetEnvParam(ENV_ATTACK) << 4) | pSIDInst->GetEnvParam(ENV_DECAY),
		(pSIDInst->GetEnvParam(ENV_SUSTAIN) << 4) | pSIDInst->GetEnvParam(ENV_RELEASE)
	);
	UpdateTables(pSIDInst.get());

}

void CSeqInstHandlerSID::UpdateInstrument()
{
	CSeqInstHandler::UpdateInstrument();

	if (auto pSIDInst = std::dynamic_pointer_cast<const CInstrumentSID>(m_pInstrument))
		UpdateTables(pSIDInst.get());

}

void CSeqInstHandlerSID::UpdateTables(const CInstrumentSID* pInst)
{
	auto* pInterface = dynamic_cast<CChannelHandlerInterfaceSID*>(m_pInterface);
	if (pInterface == nullptr) return;
	if (m_pPWMMode != PWM_DISABLED)
		pInterface->SetPulseWidth(m_pPWMValue);
	m_pPWMValue += m_pPWMDirection * m_pPWMSpeed;
	if (m_pPWMMode == PWM_LOOP) {
		if ((m_pPWMValue > m_pPWMEnd && m_pPWMDirection > 0) || (m_pPWMValue < m_pPWMEnd && m_pPWMDirection < 0)) {
			m_pPWMValue = m_pPWMStart;
		}
	}
	else if (m_pPWMMode == PWM_PINGPONG) {
		if ((m_pPWMValue > m_pPWMEnd && m_pPWMDirection > 0) || (m_pPWMValue < m_pPWMEnd && m_pPWMDirection < 0)) {
			m_pPWMValue = m_pPWMEnd;
			m_pPWMSpeed *= -1;
		}
		else if ((m_pPWMValue < m_pPWMStart && m_pPWMDirection > 0) || (m_pPWMValue > m_pPWMStart && m_pPWMDirection < 0)) {
			m_pPWMValue = m_pPWMStart;
			m_pPWMSpeed *= -1;
		}
	}
	else if (m_pPWMMode == PWM_ONCE) {
		if ((m_pPWMValue > m_pPWMEnd && m_pPWMDirection > 0) || (m_pPWMValue < m_pPWMEnd && m_pPWMDirection < 0)) {
			m_pPWMValue = m_pPWMEnd;
			m_pPWMSpeed = 0;
		}
	}
	if (m_pFilterMode != PWM_DISABLED) {
		pInterface->SetFilterCutoff(m_pFilterValue);
	}
	m_pFilterValue += m_pFilterDirection * m_pFilterSpeed;
	if (m_pFilterMode == PWM_LOOP) {
		if ((m_pFilterValue > m_pFilterEnd && m_pFilterDirection > 0) || (m_pFilterValue < m_pFilterEnd && m_pFilterDirection < 0)) {
			m_pFilterValue = m_pFilterStart;
		}
	}
	else if (m_pFilterMode == PWM_PINGPONG) {
		if ((m_pFilterValue > m_pFilterEnd && m_pFilterDirection > 0) || (m_pFilterValue < m_pFilterEnd && m_pFilterDirection < 0)) {
			m_pFilterValue = m_pFilterEnd;
			m_pFilterSpeed *= -1;
		}
		else if ((m_pFilterValue < m_pFilterStart && m_pFilterDirection > 0) || (m_pFilterValue > m_pFilterStart && m_pFilterDirection < 0)) {
			m_pFilterValue = m_pFilterStart;
			m_pFilterSpeed *= -1;
		}
	}
	else if (m_pFilterMode == PWM_ONCE) {
		if ((m_pFilterValue > m_pFilterEnd && m_pFilterDirection > 0) || (m_pFilterValue < m_pFilterEnd && m_pFilterDirection < 0)) {
			m_pFilterValue = m_pFilterEnd;
			m_pFilterSpeed = 0;
		}
	}
}