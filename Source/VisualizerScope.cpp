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

// Most of codes are rewritten from E-FamiTracker by Euly

#include "VisualizerScope.h"
#include <algorithm>  // std::fill
#include <cmath>
#include <stdexcept>
#include "FamiTracker.h"
#include "Graphics.h"

/*
 * Displays a sample scope
 *
 */

CVisualizerScope::CVisualizerScope(bool bBlur) :
	m_pWindowBuf(NULL),
	m_bBlur(bBlur)
{
}

CVisualizerScope::~CVisualizerScope()
{
	SAFE_RELEASE_ARRAY(m_pWindowBuf);
}

void CVisualizerScope::Create(int Width, int Height)
{
	CVisualizerBase::Create(Width, Height);

	SAFE_RELEASE_ARRAY(m_pWindowBuf);
	m_pWindowBuf = new short[Width * 2];
	m_iWindowBufPtr = 0;
	std::fill(m_pWindowBuf, m_pWindowBuf + Width, 0);
}

void CVisualizerScope::SetSampleRate(int SampleRate)
{
}

static constexpr int TIME_SCALING = 7;

bool CVisualizerScope::SetScopeData(short const* pSamples, unsigned int iCount)
{
	m_pSamples = pSamples;
	m_iSampleCount = iCount;
	ASSERT(m_iSampleCount == (unsigned int)(m_iWidth * TIME_SCALING));
	return true;
}

void CVisualizerScope::ClearBackground()
{
	for (int y = 0; y < m_iHeight; ++y) {
		int intensity = static_cast<int>(sinf((float(y) * 3.14f) / float(m_iHeight)) * 40.0f);		// // //
		memset(&m_pBlitBuffer[y * m_iWidth], intensity, sizeof(COLORREF) * m_iWidth);
	}
}

void CVisualizerScope::RenderBuffer()
{
	const float SAMPLE_SCALING	= 1200.0f / 2;

	const COLORREF LINE_COL1 = 0xFFFFFF;
	const COLORREF LINE_COL2 = 0x808080;

	const int BLUR_COLORS[] = {3, 12, 12};

	const float HALF_HEIGHT = float(m_iHeight) / 2.0f;

	if (m_bBlur)
		BlurBuffer(m_pBlitBuffer.get(), m_iWidth, m_iHeight, BLUR_COLORS);
	else
		ClearBackground();

	float Sample = -float(m_pWindowBuf[0]) / SAMPLE_SCALING;

	float TriggerX = 0;
	float TriggerDelta = 0;


	for (float x = float(m_iWidth) / 2; x < float(m_iWidth) * 3 / 2; ++x) {
		float Sample1 = m_pWindowBuf[int(x - 1)];
		float Sample2 = m_pWindowBuf[int(x)];

		if (Sample2 - Sample1 > TriggerDelta && Sample1 <= 0 && Sample2 > 0) {
			TriggerDelta = Sample2 - Sample1;
			TriggerX = x - float(m_iWidth) / 2;
		}

	}
	if (TriggerX == 0) {
		for (float x = float(m_iWidth) / 2; x < float(m_iWidth) * 3 / 2; ++x) {
			float Sample1 = m_pWindowBuf[int(x - 1)];
			float Sample2 = m_pWindowBuf[int(x)];

			if (Sample2 - Sample1 > TriggerDelta) {
				TriggerDelta = Sample2 - Sample1;
				TriggerX = x - float(m_iWidth) / 2;
			}

		}
	}

	for (float x = 0.0f; x < float(m_iWidth) * 2; ++x) {
		float LastSample = Sample;
		Sample = -float(m_pWindowBuf[int(x)]) / SAMPLE_SCALING;

		if (Sample < -HALF_HEIGHT + 1)
			Sample = -HALF_HEIGHT + 1;
		if (Sample > HALF_HEIGHT - 1)
			Sample = HALF_HEIGHT - 1;

		if (x - TriggerX <= float(m_iWidth) && x - TriggerX >= 0.0f) {
			PutPixel(m_pBlitBuffer.get(), m_iWidth, m_iHeight, x - TriggerX, Sample + HALF_HEIGHT - 0.5f, LINE_COL2);
			PutPixel(m_pBlitBuffer.get(), m_iWidth, m_iHeight, x - TriggerX, Sample + HALF_HEIGHT + 0.5f, LINE_COL2);
			PutPixel(m_pBlitBuffer.get(), m_iWidth, m_iHeight, x - TriggerX, Sample + HALF_HEIGHT + 0.0f, LINE_COL1);
		}

		if ((Sample - LastSample) > 1.0f) {
			float frac = LastSample - floor(LastSample);
			for (float y = LastSample; y < Sample; ++y) {
				float Offset = (y - LastSample) / (Sample - LastSample);
				if (x + Offset - 1.0f - TriggerX <= float(m_iWidth) && x + Offset - 1.0f - TriggerX >= 0.0f)
					PutPixel(m_pBlitBuffer.get(), m_iWidth, m_iHeight, x + Offset - 1.0f - TriggerX, y + HALF_HEIGHT + frac, LINE_COL1);
			}
		}
		else if ((LastSample - Sample) > 1.0f) {
			float frac = Sample - floor(Sample);
			for (float y = Sample; y < LastSample; ++y) {
				float Offset = (y - Sample) / (LastSample - Sample);
				if (x - Offset - TriggerX <= float(m_iWidth) && x - Offset - TriggerX >= 0.0f)
					PutPixel(m_pBlitBuffer.get(), m_iWidth, m_iHeight, x - Offset - TriggerX, y + HALF_HEIGHT + frac, LINE_COL1);
			}
		}
	}
}

void CVisualizerScope::Draw() {
#ifdef _DEBUG
	static int _peak = 0;
	static int _min = 0;
	static int _max = 0;
#endif

	const int TIME_SCALING = 7;

	static int LastPos = 0;
	static int Accum = 0;

	for (unsigned int i = 0; i < m_iSampleCount; ++i) {

#ifdef _DEBUG
		if (_min > m_pSamples[i])
			_min = m_pSamples[i];
		if (_max < m_pSamples[i])
			_max = m_pSamples[i];
		if (abs(m_pSamples[i]) > _peak)
			_peak = abs(m_pSamples[i]);
#endif

		int Pos = m_iWindowBufPtr / TIME_SCALING;
		m_iWindowBufPtr++;

		Accum += m_pSamples[i];

		if (Pos != LastPos) {
			m_pWindowBuf[LastPos] = Accum / TIME_SCALING;
			Accum = 0;
		}

		LastPos = Pos;

		if (Pos == m_iWidth * 2) {
			m_iWindowBufPtr = 0;
			LastPos = 0;
			RenderBuffer();
		}
	}

#ifdef _DEBUG
	_peak = _max - _min;
	m_iPeak = _peak;
	_peak = 0;
	_min = 0;
	_max = 0;
#endif
}

void CVisualizerScope::Display(CDC *pDC, bool bPaintMsg)
{
	CVisualizerBase::Display(pDC, bPaintMsg);		// // //

#ifdef _DEBUG
	CString PeakText;
	PeakText.Format(_T("%i"), m_iPeak);
	pDC->TextOut(0, 0, PeakText);
	PeakText.Format(_T("-%gdB"), 20.0 * log(double(m_iPeak) / 65535.0));
	pDC->TextOut(0, 16, PeakText);
#endif
}

size_t CVisualizerScope::NeededSamples() const
{
	return (size_t)(m_iWidth * TIME_SCALING);
}
