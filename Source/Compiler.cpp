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

#include <map>
#include <vector>
#include "stdafx.h"
#include "version.h"		// // //
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "SeqInstrument.h"		// // //
#include "Instrument2A03.h"		// // //
#include "InstrumentFDS.h"		// // //
#include "InstrumentN163.h"		// // //
#include "PatternCompiler.h"
#include "DSample.h"		// // //
#include "Compiler.h"
#include "Chunk.h"
#include "ChunkRenderText.h"
#include "ChunkRenderBinary.h"
#include "Driver.h"
#include "SoundGen.h"
#include "APU/APU.h"

//
// This is the new NSF data compiler, music is compiled to an object list instead of a binary chunk
//
// The list can be translated to both a binary chunk and an assembly file
// 

/*
 * TODO:
 *  - Remove duplicated FDS waves
 *  - Remove the bank value in CHUNK_SONG??
 *  - Derive classes for each output format instead of separate functions
 *  - Create a config file for NSF driver optimizations
 *  - Pattern hash collisions prevents detecting similar patterns, fix that
 *  - Add bankswitching schemes for other memory mappers
 *
 */

/*
 * Notes:
 *
 *  - DPCM samples and instruments is currently stored as a linear list,
 *    which currently limits the number of possible DPCM configurations
 *    to 127.
 *  - Instrument data is non bankswitched, it might be possible to create
 *    instrument data of a size that makes export impossible.
 *
 */

/*
 * Bankswitched file layout:
 *
 * - $8000 - $AFFF: Music driver and song data (instruments, frames & patterns, unpaged)
 * - $B000 - $BFFF: Swichted part of song data (frames + patterns, 1 page only)
 * - $C000 - $EFFF: Samples (3 pages)
 * - $F000 - $FFFF: Fixed to last bank for compatibility with TNS HFC carts
 *
 * Non-bankswitched, compressed layout:
 *
 * - Music data, driver, DPCM samples
 * 
 * Non-bankswitched + bankswitched, default layout:
 *
 * - Driver, music data, DPCM samples
 *
 */

// Note: Each CCompiler object may only be used once (fix this)

// Remove duplicated patterns (default on)
#define REMOVE_DUPLICATE_PATTERNS

// Don't remove patterns across different tracks (default off)
//#define LOCAL_DUPLICATE_PATTERN_REMOVAL

// Enable bankswitching on all songs (default off)
//#define FORCE_BANKSWITCH

const int CCompiler::PATTERN_CHUNK_INDEX		= 0;		// Fixed at 0 for the moment

const int CCompiler::PAGE_SIZE					= 0x1000;
const int CCompiler::PAGE_START					= 0x8000;
const int CCompiler::PAGE_BANKED				= 0xB000;	// 0xB000 -> 0xBFFF
const int CCompiler::PAGE_SAMPLES				= 0xC000;

const int CCompiler::PATTERN_SWITCH_BANK		= 3;		// 0xB000 -> 0xBFFF

const int CCompiler::DPCM_PAGE_WINDOW			= 3;		// Number of switchable pages in the DPCM area
const int CCompiler::DPCM_SWITCH_ADDRESS		= 0xF000;	// Switch to new banks when reaching this address

const bool CCompiler::LAST_BANK_FIXED			= true;		// Fix for TNS carts

// Flag byte flags
const int CCompiler::FLAG_BANKSWITCHED	= 1 << 0;
const int CCompiler::FLAG_VIBRATO		= 1 << 1;
const int CCompiler::FLAG_LINEARPITCH	= 1 << 2;		// // //


// Enable this to simulate NSF driver export multichip for assembly, which enables all chips internally
#ifdef _DEBUG
constexpr bool UseAllChips = true;
#else
constexpr bool UseAllChips = false;
#endif

CCompiler *CCompiler::pCompiler = NULL;

CCompiler *CCompiler::GetCompiler()
{
	return pCompiler;
}

unsigned int CCompiler::AdjustSampleAddress(unsigned int Address)
{
	// Align samples to 64-byte pages
	return (0x40 - (Address & 0x3F)) & 0x3F;
}

// CCompiler

CCompiler::CCompiler(CFamiTrackerDoc *pDoc, CCompilerLog *pLogger) :
	m_pDocument(pDoc),
	m_pLogger(pLogger),
	m_iWaveTables(0),
	m_pSamplePointersChunk(NULL),
	m_pHeaderChunk(NULL),
	m_pDriverData(NULL),
	m_iLastBank(0),
	m_iHashCollisions(0),
	m_iFirstSampleBank(0)
{
	ASSERT(CCompiler::pCompiler == NULL);
	CCompiler::pCompiler = this;

	m_iActualChip = m_pDocument->GetExpansionChip();		// // //
	m_iActualNamcoChannels = m_pDocument->GetNamcoChannels();
}

CCompiler::~CCompiler()
{
	CCompiler::pCompiler = NULL;

	Cleanup();

	SAFE_RELEASE(m_pLogger);
}

template <typename... T>
void CCompiler::Print(std::string_view text, T... args) const		// // //
{
	static TCHAR buf[256];

	if (m_pLogger == NULL || text.empty())
		return;

	_sntprintf_s(buf, sizeof(buf), _TRUNCATE, text.data(), args...);

	size_t len = _tcslen(buf);

	if (buf[len - 1] == '\n' && len < (sizeof(buf) - 1)) {
		buf[len - 1] = '\r';
		buf[len] = '\n';
		buf[len + 1] = 0;
	}

	m_pLogger->WriteLog(buf);
}

void CCompiler::ClearLog() const
{
	if (m_pLogger != NULL)
		m_pLogger->Clear();
}

bool CCompiler::OpenFile(LPCTSTR lpszFileName, CFile &file) const
{
	CFileException ex;

	if (!file.Open(lpszFileName, CFile::modeWrite | CFile::modeCreate, &ex)) {
		// Display formatted file exception message
		TCHAR szCause[255];
		CString strFormatted;
		ex.GetErrorMessage(szCause, 255);
		AfxFormatString1(strFormatted, IDS_OPEN_FILE_ERROR, szCause);
		theApp.DisplayMessage(strFormatted, MB_OK | MB_ICONERROR);
		return false;
	}

	return true;
}

void CCompiler::ExportNSF(LPCTSTR lpszFileName, int MachineType)
{
	ClearLog();

	bool nsfewarning = false;

	// TODO: toggle NSFe metadata warning
	for (int i = 0; i < 8; i++) {
		nsfewarning |= m_pDocument->GetLevelOffset(i) != 0;
	}

	nsfewarning |= m_pDocument->GetExternalOPLLChipCheck();

	if (nsfewarning) {
		Print("Warning: NSFe optional metadata will not be exported in this format!\n");
		theApp.DisplayMessage(_T("NSFe optional metadata will not be exported in this format!"), 0, 0);
	}


	// Build the music data
	if (!CompileData(true)) {
		// Failed
		Cleanup();
		return;
	}

	if (m_bBankSwitched) {
		// Expand and allocate label addresses
		AddBankswitching();
		if (!ResolveLabelsBankswitched()) {
			Cleanup();
			return;
		}
		// Write bank data
		UpdateFrameBanks();
		UpdateSongBanks();
		// Make driver aware of bankswitching
		EnableBankswitching();
	}
	else {
		ResolveLabels();
		ClearSongBanks();
	}

	// Rewrite DPCM sample pointers
	UpdateSamplePointers(m_iSampleStart);

	// Compressed mode means that driver and music is located just below the sample space, no space is lost even when samples are used
	bool bCompressedMode;
	unsigned short MusicDataAddress;

	CalculateLoadAddresses(MusicDataAddress, bCompressedMode);

	// Load driver identifier
	std::unique_ptr<char[]> pNSFDRVPtr(LoadNSFDRV(m_pDriverData));
	char *pNSFDRV = pNSFDRVPtr.get();

	// Load driver
	std::unique_ptr<char[]> pDriverPtr(LoadDriver(m_pDriverData, m_iDriverAddress));		// // //
	char *pDriver = pDriverPtr.get();

	// Patch driver binary
	PatchVibratoTable(pDriver);

	// Copy the Namco table, if used
	// // // nothing here, ft_channel_type is taken care in LoadDriver

	// Write music data address
	SetDriverSongAddress(pDriver, MusicDataAddress);

	// Open output file
	CFile OutputFile;
	if (!OpenFile(lpszFileName, OutputFile)) {
		Print("Error: Could not open output file\n");
		Cleanup();
		return;
	}

	// Create NSF header
	stNSFHeader Header;
	CreateHeader(&Header, MachineType, 0x00, false);

	// Write header
	OutputFile.Write(&Header, sizeof(stNSFHeader));

	// Write NSF data
	std::unique_ptr<CChunkRenderNSF> Render(new CChunkRenderNSF(&OutputFile, m_iLoadAddress));

	Render->StoreNSFDRV(pNSFDRV, m_iNSFDRVSize);

	if (m_bBankSwitched) {
		Render->StoreDriver(pDriver, m_iDriverSize);
		Render->StoreChunksBankswitched(m_vChunks);
		Render->StoreSamplesBankswitched(m_vSamples);
	}
	else {
		if (bCompressedMode) {
			Render->StoreChunks(m_vChunks);
			Render->StoreDriver(pDriver, m_iDriverSize);
			Render->StoreSamples(m_vSamples);
		}
		else {
			Render->StoreDriver(pDriver, m_iDriverSize);
			Render->StoreChunks(m_vChunks);
			Render->StoreSamples(m_vSamples);
		}
	}

	// Writing done, print some stats
	Print(" * NSF load address: $%04X\n", m_iLoadAddress);
	Print("Writing output file...\n");
	Print(" * Driver size: %i bytes\n", m_iDriverSize);

	if (m_bBankSwitched) {
		int Percent = (100 * m_iMusicDataSize) / (0x80000 - m_iDriverSize - m_iSamplesSize - m_iNSFDRVSize);
		int Banks = Render->GetBankCount();
		Print(" * Song data size: %i bytes (%i%%)\n", m_iMusicDataSize, Percent);
		Print(" * NSF type: Bankswitched (%i banks)\n", Banks - 1);
	}
	else {
		int Percent = (100 * m_iMusicDataSize) / (0x8000 - m_iDriverSize - m_iSamplesSize - m_iNSFDRVSize);
		Print(" * Song data size: %i bytes (%i%%)\n", m_iMusicDataSize, Percent);
		if (bCompressedMode) {
			Print(" * NSF type: Non-bankswitched compressed (driver @ $%04X)\n", m_iDriverAddress);
		}
		else {
			Print(" * NSF type: Non-bankswitched (driver @ $%04X)\n", m_iDriverAddress);
		}
	}

	Print("Done, total file size: %i bytes\n", OutputFile.GetLength());

	// Done
	OutputFile.Close();

	Cleanup();
}

void CCompiler::ExportNSFE(LPCTSTR lpszFileName, int MachineType)		// // //
{
	ClearLog();

	// Build the music data
	if (!CompileData(true)) {
		// Failed
		Cleanup();
		return;
	}

	if (m_bBankSwitched) {
		// Expand and allocate label addresses
		AddBankswitching();
		if (!ResolveLabelsBankswitched()) {
			Cleanup();
			return;
		}
		// Write bank data
		UpdateFrameBanks();
		UpdateSongBanks();
		// Make driver aware of bankswitching
		EnableBankswitching();
	}
	else {
		ResolveLabels();
		ClearSongBanks();
	}

	// Rewrite DPCM sample pointers
	UpdateSamplePointers(m_iSampleStart);

	// Compressed mode means that driver and music is located just below the sample space, no space is lost even when samples are used
	bool bCompressedMode;
	unsigned short MusicDataAddress;

	CalculateLoadAddresses(MusicDataAddress, bCompressedMode);

	// Load driver identifier
	std::unique_ptr<char[]> pNSFDRVPtr(LoadNSFDRV(m_pDriverData));
	char *pNSFDRV = pNSFDRVPtr.get();

	// Load driver
	std::unique_ptr<char[]> pDriverPtr(LoadDriver(m_pDriverData, m_iDriverAddress));		// // //
	char *pDriver = pDriverPtr.get();

	// Patch driver binary
	PatchVibratoTable(pDriver);

	// Copy the Namco table, if used
	// // // nothing here, ft_channel_type is taken care in LoadDriver

	// Write music data address
	SetDriverSongAddress(pDriver, MusicDataAddress);

	// Open output file
	CFile OutputFile;
	if (!OpenFile(lpszFileName, OutputFile)) {
		Print("Error: Could not open output file\n");
		Cleanup();
		return;
	}

	// // // Create NSFe header

	// Write NSFE, INFO, BANK. and RATE chunks
	stNSFeHeader Header;
	CreateNSFeHeader(&Header, MachineType);
	OutputFile.Write(&Header, sizeof(Header));

	// Write DATA data
	std::unique_ptr<CChunkRenderNSF> Render(new CChunkRenderNSF(&OutputFile, m_iLoadAddress));

	// write DATA chunk
	uint32_t iDataSize = 0;
	ULONGLONG iDataSizePos = OutputFile.GetPosition();
	const unsigned char DataIdent[] = { 'D', 'A', 'T', 'A' };
	OutputFile.Write(reinterpret_cast<char *>(&iDataSize), sizeof(uint32_t));
	OutputFile.Write(&DataIdent, sizeof(DataIdent));

	Render->StoreNSFDRV(pNSFDRV, m_iNSFDRVSize);

	if (m_bBankSwitched) {
		Render->StoreDriver(pDriver, m_iDriverSize);
		Render->StoreChunksBankswitched(m_vChunks);
		Render->StoreSamplesBankswitched(m_vSamples);
	}
	else {
		if (bCompressedMode) {
			Render->StoreChunks(m_vChunks);
			Render->StoreDriver(pDriver, m_iDriverSize);
			Render->StoreSamples(m_vSamples);
		}
		else {
			Render->StoreDriver(pDriver, m_iDriverSize);
			Render->StoreChunks(m_vChunks);
			Render->StoreSamples(m_vSamples);
		}
	}

	ULONGLONG iPostDataPos = OutputFile.GetPosition();

	// write actual size of DATA chunk
	OutputFile.Seek(iDataSizePos, CFile::begin);
	iDataSize = Render->GetDATAChunkSize();
	OutputFile.Write(reinterpret_cast<char *>(&iDataSize), sizeof(uint32_t));

	// continue writing
	OutputFile.Seek(iPostDataPos, CFile::begin);

	// !! !! Create NSFe footer
	stNSFeFooter Footer;
	CreateNSFeFooter(&Footer);

	// TODO write NSF2 chunk?

	auto write_chunk = [&](stNSFeChunk chunk, CFile &file) {
		file.Write(&chunk.Size, sizeof(chunk.Size));
		file.Write(&chunk.Ident, sizeof(chunk.Ident));
		if (!chunk.Data.empty())
			file.Write(reinterpret_cast<char *>(chunk.Data.data()), (UINT)chunk.Data.size());
	};

	// write VRC7 chunk
	write_chunk(Footer.VRC7, OutputFile);

	// write time chunk
	write_chunk(Footer.time, OutputFile);

	// write tlbl chunk
	write_chunk(Footer.tlbl, OutputFile);

	// write auth chunk
	write_chunk(Footer.auth, OutputFile);

	// write text chunk
	write_chunk(Footer.text, OutputFile);

	// write mixe chunk
	write_chunk(Footer.mixe, OutputFile);

	// write NEND chunk
	write_chunk(Footer.NEND, OutputFile);

	// Writing done, print some stats
	Print(" * NSF load address: $%04X\n", m_iLoadAddress);
	Print("Writing output file...\n");
	Print(" * Driver size: %i bytes\n", m_iDriverSize);

	if (m_bBankSwitched) {
		int Percent = (100 * m_iMusicDataSize) / (0x80000 - m_iDriverSize - m_iSamplesSize);
		int Banks = Render->GetBankCount();
		Print(" * Song data size: %i bytes (%i%%)\n", m_iMusicDataSize, Percent);
		Print(" * NSF type: Bankswitched (%i banks)\n", Banks - 1);
	}
	else {
		int Percent = (100 * m_iMusicDataSize) / (0x8000 - m_iDriverSize - m_iSamplesSize);
		Print(" * Song data size: %i bytes (%i%%)\n", m_iMusicDataSize, Percent);
		Print(" * NSF type: Linear (driver @ $%04X)\n", m_iDriverAddress);
	}

	Print("Done, total file size: %i bytes\n", OutputFile.GetLength());

	// Done
	OutputFile.Close();

	Cleanup();
}

void CCompiler::ExportNSF2(LPCTSTR lpszFileName, int MachineType)
{
	ClearLog();

	// Build the music data
	if (!CompileData(true)) {
		// Failed
		Cleanup();
		return;
	}

	if (m_bBankSwitched) {
		// Expand and allocate label addresses
		AddBankswitching();
		if (!ResolveLabelsBankswitched()) {
			Cleanup();
			return;
		}
		// Write bank data
		UpdateFrameBanks();
		UpdateSongBanks();
		// Make driver aware of bankswitching
		EnableBankswitching();
	}
	else {
		ResolveLabels();
		ClearSongBanks();
	}

	// Rewrite DPCM sample pointers
	UpdateSamplePointers(m_iSampleStart);

	// Compressed mode means that driver and music is located just below the sample space, no space is lost even when samples are used
	bool bCompressedMode;
	unsigned short MusicDataAddress;

	CalculateLoadAddresses(MusicDataAddress, bCompressedMode);

	// Init is located first at the driver
	m_iInitAddress = m_iDriverAddress;		// !! !!

	// Load driver identifier
	std::unique_ptr<char[]> pNSFDRVPtr(LoadNSFDRV(m_pDriverData));
	char *pNSFDRV = pNSFDRVPtr.get();

	// Load driver
	std::unique_ptr<char[]> pDriverPtr(LoadDriver(m_pDriverData, m_iDriverAddress));		// // //
	char *pDriver = pDriverPtr.get();

	// Patch driver binary
	PatchVibratoTable(pDriver);

	// Copy the Namco table, if used
	// // // nothing here, ft_channel_type is taken care in LoadDriver

	// Write music data address
	SetDriverSongAddress(pDriver, MusicDataAddress);

	// Open output file
	CFile OutputFile;
	if (!OpenFile(lpszFileName, OutputFile)) {
		Print("Error: Could not open output file\n");
		Cleanup();
		return;
	}

	// Write NSF data
	std::unique_ptr<CChunkRenderNSF> Render(new CChunkRenderNSF(&OutputFile, m_iLoadAddress));

	// Create NSF header
	stNSFHeader Header;
	CreateHeader(&Header, MachineType, 0x80, true);

	// Write header
	OutputFile.Write(&Header, sizeof(stNSFHeader));

	ULONGLONG iDataSizePos = OutputFile.GetPosition() - 3;

	Render->StoreNSFDRV(pNSFDRV, m_iNSFDRVSize);

	if (m_bBankSwitched) {
		Render->StoreDriver(pDriver, m_iDriverSize);
		Render->StoreChunksBankswitched(m_vChunks);
		Render->StoreSamplesBankswitched(m_vSamples);
	}
	else {
		if (bCompressedMode) {
			Render->StoreChunks(m_vChunks);
			Render->StoreDriver(pDriver, m_iDriverSize);
			Render->StoreSamples(m_vSamples);
		}
		else {
			Render->StoreDriver(pDriver, m_iDriverSize);
			Render->StoreChunks(m_vChunks);
			Render->StoreSamples(m_vSamples);
		}
	}

	ULONGLONG iPostDataPos = OutputFile.GetPosition();

	// write actual size of DATA
	OutputFile.Seek(iDataSizePos, CFile::begin);
	unsigned int iDataSize = Render->GetDATAChunkSize();
	unsigned char NSFSize[3] = {
		char(iDataSize & 0xFF),
		char((iDataSize >> 8) & 0xFF),
		char((iDataSize >> 16) & 0xFF) };
	OutputFile.Write(&NSFSize, sizeof(NSFSize));

	// continue writing
	OutputFile.Seek(iPostDataPos, CFile::begin);

	// !! !! Create NSFe footer
	stNSFeFooter Footer;
	CreateNSFeFooter(&Footer);

	auto write_chunk = [&](stNSFeChunk chunk, CFile &file) {
		file.Write(&chunk.Size, sizeof(chunk.Size));
		file.Write(&chunk.Ident, sizeof(chunk.Ident));
		if (!chunk.Data.empty())
			file.Write(reinterpret_cast<char *>(chunk.Data.data()), (UINT)chunk.Data.size());
	};

	// write VRC7 chunk
	write_chunk(Footer.VRC7, OutputFile);

	// write time chunk
	write_chunk(Footer.time, OutputFile);

	// write tlbl chunk
	write_chunk(Footer.tlbl, OutputFile);

	// write auth chunk
	write_chunk(Footer.auth, OutputFile);

	// write text chunk
	write_chunk(Footer.text, OutputFile);

	// write mixe chunk
	write_chunk(Footer.mixe, OutputFile);

	// write NEND chunk
	write_chunk(Footer.NEND, OutputFile);

	// Writing done, print some stats
	Print(" * NSF load address: $%04X\n", m_iLoadAddress);
	Print("Writing output file...\n");
	Print(" * Driver size: %i bytes\n", m_iDriverSize);

	if (m_bBankSwitched) {
		int Percent = (100 * m_iMusicDataSize) / (0x80000 - m_iDriverSize - m_iSamplesSize);
		int Banks = Render->GetBankCount();
		Print(" * Song data size: %i bytes (%i%%)\n", m_iMusicDataSize, Percent);
		Print(" * NSF type: Bankswitched (%i banks)\n", Banks - 1);
	}
	else {
		int Percent = (100 * m_iMusicDataSize) / (0x8000 - m_iDriverSize - m_iSamplesSize);
		Print(" * Song data size: %i bytes (%i%%)\n", m_iMusicDataSize, Percent);
		Print(" * NSF type: Linear (driver @ $%04X)\n", m_iDriverAddress);
	}

	Print("Done, total file size: %i bytes\n", OutputFile.GetLength());

	// Done
	OutputFile.Close();

	Cleanup();
}

void CCompiler::ExportNES(LPCTSTR lpszFileName, bool EnablePAL)
{
	// 32kb NROM, no CHR
	const char NES_HEADER[] = {
		0x4E, 0x45, 0x53, 0x1A, 0x02, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	ClearLog();

	bool nsfewarning = false;

	for (int i = 0; i < 8; i++) {
		nsfewarning |= m_pDocument->GetLevelOffset(i) != 0;
	}

	nsfewarning |= m_pDocument->GetExternalOPLLChipCheck();

	if (nsfewarning) {
		Print("Warning: NSFe optional metadata will not be exported in this format!\n");
		theApp.DisplayMessage(_T("NSFe optional metadata will not be exported in this format!"), 0, 0);
	}

	if (m_pDocument->GetExpansionChip() != SNDCHIP_NONE) {
		Print("Error: Expansion chips not supported.\n");
		theApp.DisplayMessage("Expansion chips are currently not supported when exporting to .NES!", 0, 0);
		Cleanup();
		return;
	}

	CFile OutputFile;
	if (!OpenFile(lpszFileName, OutputFile)) {
		Cleanup();
		return;
	}

	// Build the music data
	if (!CompileData(true)) {
		Cleanup();
		return;
	}

	if (m_bBankSwitched) {
		// Abort if larger than 32kb
		Print("Error: Song is larger than 32kB, aborted.\n");
		theApp.DisplayMessage(_T("Song is too big to fit into 32kB!"), 0, 0);
		Cleanup();
		return;
	}

	ResolveLabels();

	// Rewrite DPCM sample pointers
	UpdateSamplePointers(m_iSampleStart);

	// Compressed mode means that driver and music is located just below the sample space, no space is lost even when samples are used
	bool bCompressedMode;
	unsigned short MusicDataAddress;

	CalculateLoadAddresses(MusicDataAddress, bCompressedMode, true);

	// Load driver identifier
	std::unique_ptr<char[]> pNSFDRVPtr(LoadNSFDRV(m_pDriverData));
	char *pNSFDRV = pNSFDRVPtr.get();

	// Load driver
	std::unique_ptr<char[]> pDriverPtr(LoadDriver(m_pDriverData, m_iDriverAddress));		// // //
	char *pDriver = pDriverPtr.get();

	// Patch driver binary
	PatchVibratoTable(pDriver);

	// Copy the Namco table, if used
	// // // nothing here, ft_channel_type is taken care in LoadDriver

	// Write music data address
	SetDriverSongAddress(pDriver, MusicDataAddress);

	int Percent = (100 * m_iMusicDataSize) / (0x8000 - m_iDriverSize - m_iSamplesSize);

	Print("Writing file...\n");
	Print(" * Driver size: %i bytes\n", m_iDriverSize);
	Print(" * Song data size: %i bytes (%i%%)\n", m_iMusicDataSize, Percent);

	// Write header
	OutputFile.Write(NES_HEADER, 0x10);

	// Write NES data
	std::unique_ptr<CChunkRenderNES> Render(new CChunkRenderNES(&OutputFile, m_iLoadAddress));
	Render->StoreNSFDRV(pNSFDRV, m_iNSFDRVSize);
	Render->StoreDriver(pDriver, m_iDriverSize);
	Render->StoreChunks(m_vChunks);
	Render->StoreSamples(m_vSamples);
	Render->StoreCaller(NSF_CALLER_BIN, NSF_CALLER_SIZE);

	Print("Done, total file size: %i bytes\n", 0x8000 + 0x10);

	// Done
	OutputFile.Close();

	Cleanup();
}

void CCompiler::ExportBIN(LPCTSTR lpszBIN_File, LPCTSTR lpszDPCM_File, int MachineType, bool ExtraData)
{
	ClearLog();

	bool nsfewarning = false;

	for (int i = 0; i < 8; i++) {
		nsfewarning |= m_pDocument->GetLevelOffset(i) != 0;
	}

	nsfewarning |= m_pDocument->GetExternalOPLLChipCheck();

	if (nsfewarning) {
		Print("Warning: NSFe optional metadata will not be exported in this format!\n");
		theApp.DisplayMessage(_T("NSFe optional metadata will not be exported in this format!"), 0, 0);
	}

	// Build the music data
	if (!CompileData(false, UseAllChips)) {
		// Failed
		Cleanup();
		return;
	}

	if (m_bBankSwitched) {
		// TODO: write seperate binary banks
		Print("Error: Can't write bankswitched songs!\n");
		Cleanup();
		return;
	}
	else {
		ResolveLabels();
		ClearSongBanks();
	}

	// Rewrite DPCM sample pointers
	UpdateSamplePointers(m_iSampleStart);

	// Compressed mode means that driver and music is located just below the sample space, no space is lost even when samples are used
	bool bCompressedMode;
	unsigned short MusicDataAddress;

	CalculateLoadAddresses(MusicDataAddress, bCompressedMode, true);

	// Init is located first at the driver
	m_iInitAddress = m_iDriverAddress;		// !! !!

	stNSFHeader Header;
	CreateHeader(&Header, MachineType, 0x00, false);

	// Open output files
	CFilePtrArray OutputFiles = {};
	size_t OutputFileBINIndex = OutputFiles.size();
	std::string_view errormsg = "Error: Could not open output binary file\n";
	if (!OpenArrayFile(OutputFiles, lpszBIN_File, errormsg)) {
		return;
	}

	size_t OutputFileDPCMIndex = OutputFiles.size();	// off-by-one gets corrected
	errormsg = "Error: Could not open output binary DPCM file\n";
	if (_tcslen(lpszDPCM_File) != 0) {
		if (!OpenArrayFile(OutputFiles, lpszDPCM_File, errormsg)) {
			return;
		}
	}

	Print("Writing output files...\n");

	if (_tcslen(lpszDPCM_File) != 0)
		WriteSamplesBinary(OutputFiles.at(OutputFileDPCMIndex).get());

	if (ExtraData) {
		// Get the directory of the output BIN file
		CFile *OutputFileBin = OutputFiles.at(OutputFileBINIndex).get();
		CString FilePath = OutputFileBin->GetFilePath();
		CString FileName = OutputFileBin->GetFileName();
		int trimcount = FilePath.GetLength() - FileName.GetLength();
		int trimindex = FilePath.GetLength() - trimcount;
		FilePath.Delete(trimindex, trimcount);

		// NSF stub
		size_t OutputFileNSFStubIndex = OutputFiles.size();
		errormsg = "Error: Could not open output NSF stub file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "nsf_stub.s"), errormsg)) {
			return;
		}

		// NSF header
		size_t OutputFileNSFHeaderIndex = OutputFiles.size();
		errormsg = "Error: Could not open output NSF header file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "nsf_header.s"), errormsg)) {
			return;
		}

		// NSF config file
		size_t OutputFileNSFConfigIndex = OutputFiles.size();
		errormsg = "Error: Could not open output NSF config file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "nsf.cfg"), errormsg)) {
			return;
		}

		// period table file
		size_t OutputFilePeriodsIndex = OutputFiles.size();
		errormsg = "Error: Could not open output period table file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "periods.s"), errormsg)) {
			return;
		}

		// vibrato table file
		size_t OutputFileVibratoIndex = OutputFiles.size();
		errormsg = "Error: Could not open output vibrato table file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "vibrato.s"), errormsg)) {
			return;
		}

		// multichip enable table file
		size_t OutputFileMultiChipEnableIndex = OutputFiles.size();
		errormsg = "Error: Could not open output multichip enable table file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "enable_ext.s"), errormsg)) {
			return;
		}

		// Write multichip update handler file
		size_t OutputFileMultiChipUpdateIndex = OutputFiles.size();
		errormsg = "Error: Could not open output multichip update handler file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "update_ext.s"), errormsg)) {
			return;
		}

		WriteBinary(OutputFiles, ExtraData, Header, MachineType,
			OutputFileBINIndex,
			OutputFileNSFStubIndex,
			OutputFileNSFHeaderIndex,
			OutputFileNSFConfigIndex,
			OutputFilePeriodsIndex,
			OutputFileVibratoIndex,
			OutputFileMultiChipEnableIndex,
			OutputFileMultiChipUpdateIndex);
	}
	else
		WriteBinary(OutputFiles, ExtraData, Header, MachineType, OutputFileBINIndex);

	Print("Done\n");

	// Done
	CloseFileArray(OutputFiles);
	Cleanup();
}

void CCompiler::ExportPRG(LPCTSTR lpszFileName, bool EnablePAL)
{
	// Same as export to .NES but without the header

	ClearLog();

	bool nsfewarning = false;

	for (int i = 0; i < 8; i++) {
		nsfewarning |= m_pDocument->GetLevelOffset(i) != 0;
	}

	nsfewarning |= m_pDocument->GetExternalOPLLChipCheck();

	if (nsfewarning) {
		Print("Warning: NSFe optional metadata will not be exported in this format!\n");
		theApp.DisplayMessage(_T("NSFe optional metadata will not be exported in this format!"), 0, 0);
	}

	if (m_pDocument->GetExpansionChip() != SNDCHIP_NONE) {
		Print("Error: Expansion audio not supported.\n");
		theApp.DisplayMessage(_T("Error: Expansion chips is currently not supported when exporting to PRG!"), 0, 0);
		Cleanup();
		return;
	}

	CFile OutputFile;
	if (!OpenFile(lpszFileName, OutputFile)) {
		Cleanup();
		return;
	}

	// Build the music data
	if (!CompileData(true)) {
		Cleanup();
		return;
	}

	if (m_bBankSwitched) {
		// Abort if larger than 32kb
		Print("Error: Can't write bankswitched songs!\n");
		theApp.DisplayMessage(_T("Error: Can't write bankswitched songs!"), 0, 0);
		Cleanup();
		return;
	}

	ResolveLabels();

	// Rewrite DPCM sample pointers
	UpdateSamplePointers(m_iSampleStart);

	// Compressed mode means that driver and music is located just below the sample space, no space is lost even when samples are used
	bool bCompressedMode;
	unsigned short MusicDataAddress;

	CalculateLoadAddresses(MusicDataAddress, bCompressedMode, true);

	// Load driver identifier
	std::unique_ptr<char[]> pNSFDRVPtr(LoadNSFDRV(m_pDriverData));
	char *pNSFDRV = pNSFDRVPtr.get();

	// Load driver
	std::unique_ptr<char[]> pDriverPtr(LoadDriver(m_pDriverData, m_iDriverAddress));		// // //
	char *pDriver = pDriverPtr.get();

	// Patch driver binary
	PatchVibratoTable(pDriver);

	// Copy the Namco table, if used
	// // // nothing here, ft_channel_type is taken care in LoadDriver

	// Write music data address
	SetDriverSongAddress(pDriver, MusicDataAddress);

	int Percent = (100 * m_iMusicDataSize) / (0x8000 - m_iDriverSize - m_iSamplesSize);

	Print("Writing file...\n");
	Print(" * Driver size: %i bytes\n", m_iDriverSize);
	Print(" * Song data size: %i bytes (%i%%)\n", m_iMusicDataSize, Percent);

	// Write NES data
	std::unique_ptr<CChunkRenderNES> Render(new CChunkRenderNES(&OutputFile, m_iLoadAddress));
	Render->StoreNSFDRV(pNSFDRV, m_iNSFDRVSize);
	Render->StoreDriver(pDriver, m_iDriverSize);
	Render->StoreChunks(m_vChunks);
	Render->StoreSamples(m_vSamples);
	Render->StoreCaller(NSF_CALLER_BIN, NSF_CALLER_SIZE);

	// Done
	OutputFile.Close();

	Cleanup();
}

void CCompiler::ExportASM(LPCTSTR lpszFileName, int MachineType, bool ExtraData)
{
	ClearLog();

	bool nsfewarning = false;

	for (int i = 0; i < 8; i++) {
		nsfewarning |= m_pDocument->GetLevelOffset(i) != 0;
	}

	nsfewarning |= m_pDocument->GetExternalOPLLChipCheck();

	if (nsfewarning) {
		Print("Warning: NSFe optional metadata will not be exported in this format!\n");
		theApp.DisplayMessage(_T("NSFe optional metadata will not be exported in this format!"), 0, 0);
	}

	// Build the music data
	if (!CompileData(false, UseAllChips)) {
		// Failed
		Cleanup();
		return;
	}

	if (m_bBankSwitched) {
		// Expand and allocate label addresses
		AddBankswitching();
		if (!ResolveLabelsBankswitched()) {
			Cleanup();
			return;
		}
		// Make driver aware of bankswitching
		EnableBankswitching();
		// Write bank data
		UpdateFrameBanks();
		UpdateSongBanks();
	}
	else {
		ResolveLabels();
		ClearSongBanks();
	}

	// Rewrite DPCM sample pointers
	UpdateSamplePointers(m_iSampleStart);

	// Compressed mode means that driver and music is located just below the sample space, no space is lost even when samples are used
	bool bCompressedMode;
	unsigned short MusicDataAddress;

	CalculateLoadAddresses(MusicDataAddress, bCompressedMode, true);

	stNSFHeader Header;
	CreateHeader(&Header, MachineType, 0x00, false);

	CFilePtrArray OutputFiles = {};
	size_t OutputFileASMIndex = OutputFiles.size();
	std::string_view errormsg = "Error: Could not open output assembly file\n";
	if (!OpenArrayFile(OutputFiles, lpszFileName, errormsg)) {
		return;
	}

	// Write output file
	Print("Writing output files...\n");

	if (ExtraData) {
		// Get the directory of the output assembly file
		CFile *OutputFileBin = OutputFiles.at(OutputFileASMIndex).get();
		CString FilePath = OutputFileBin->GetFilePath();
		CString FileName = OutputFileBin->GetFileName();
		int trimcount = FileName.GetLength();
		int trimindex = FilePath.GetLength() - trimcount;
		FilePath.Delete(trimindex, trimcount);

		// NSF stub
		size_t OutputFileNSFStubIndex = OutputFiles.size();
		errormsg = "Error: Could not open output NSF stub file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "nsf_stub.s"), errormsg)) {
			return;
		}

		// NSF header
		size_t OutputFileNSFHeaderIndex = OutputFiles.size();
		errormsg = "Error: Could not open output NSF header file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "nsf_header.s"), errormsg)) {
			return;
		}

		// NSF config file
		size_t OutputFileNSFConfigIndex = OutputFiles.size();
		errormsg = "Error: Could not open output NSF config file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "nsf.cfg"), errormsg)) {
			return;
		}

		// period table file
		size_t OutputFilePeriodsIndex = OutputFiles.size();
		errormsg = "Error: Could not open output period table file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "periods.s"), errormsg)) {
			return;
		}

		// vibrato table file
		size_t OutputFileVibratoIndex = OutputFiles.size();
		errormsg = "Error: Could not open output vibrato table file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "vibrato.s"), errormsg)) {
			return;
		}

		// multichip enable table file
		size_t OutputFileMultiChipEnableIndex = OutputFiles.size();
		errormsg = "Error: Could not open output multichip enable table file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "enable_ext.s"), errormsg)) {
			return;
		}

		// Write multichip update handler file
		size_t OutputFileMultiChipUpdateIndex = OutputFiles.size();
		errormsg = "Error: Could not open output multichip update handler file\n";
		if (!OpenArrayFile(OutputFiles, LPCTSTR(FilePath + "update_ext.s"), errormsg)) {
			return;
		}

		WriteAssembly(OutputFiles, ExtraData, Header, MachineType,
			OutputFileASMIndex,
			OutputFileNSFStubIndex,
			OutputFileNSFHeaderIndex,
			OutputFileNSFConfigIndex,
			OutputFilePeriodsIndex,
			OutputFileVibratoIndex,
			OutputFileMultiChipEnableIndex,
			OutputFileMultiChipUpdateIndex);
	}
	else {
		WriteAssembly(OutputFiles, ExtraData, Header, MachineType, OutputFileASMIndex);
	}

	Print("Done\n");

	// Done
	CloseFileArray(OutputFiles);
	Cleanup();
}

char *CCompiler::LoadDriver(const driver_t *pDriver, unsigned short Origin) const
{
	// Copy embedded driver
	unsigned char *pData = new unsigned char[pDriver->driver_size];
	memcpy(pData, pDriver->driver, pDriver->driver_size);

	// // // Custom pitch tables
	CSoundGen *pSoundGen = theApp.GetSoundGenerator();
	for (size_t i = 0; i < pDriver->freq_table_size; i += 2) {		// // //
		int Table = pDriver->freq_table[i + 1];
		switch (Table) {
		case CDetuneTable::DETUNE_NTSC:
		case CDetuneTable::DETUNE_PAL:
		case CDetuneTable::DETUNE_SAW:
		case CDetuneTable::DETUNE_FDS:
		case CDetuneTable::DETUNE_N163:
			for (int j = 0; j < NOTE_COUNT; ++j) {
				int Reg = pSoundGen->ReadPeriodTable(j, Table);
				pData[pDriver->freq_table[i] + 2 * j] = Reg & 0xFF;
				pData[pDriver->freq_table[i] + 2 * j + 1] = Reg >> 8;
			} break;
		case CDetuneTable::DETUNE_VRC7:
			for (int j = 0; j <= NOTE_RANGE; ++j) { // one extra item
				int Reg = pSoundGen->ReadPeriodTable(j % NOTE_RANGE, Table) * 4;
				if (j == NOTE_RANGE) Reg <<= 1;
				pData[pDriver->freq_table[i] + j] = Reg & 0xFF;
				pData[pDriver->freq_table[i] + j + NOTE_RANGE + 1] = Reg >> 8;
			} break;
		default:
			AfxDebugBreak();
		}
	}

	// Relocate driver
	for (size_t i = 0; i < pDriver->word_reloc_size; ++i) {
		// Words
		unsigned short value = pData[pDriver->word_reloc[i]] + (pData[pDriver->word_reloc[i] + 1] << 8);
		value += Origin - pDriver->nsfdrv_size;
		pData[pDriver->word_reloc[i]] = value & 0xFF;
		pData[pDriver->word_reloc[i] + 1] = value >> 8;
	}

	for (size_t i = 0; i < pDriver->adr_reloc_size; i += 2) {		// // //
		unsigned short value = pData[pDriver->adr_reloc[i]] + (pData[pDriver->adr_reloc[i + 1]] << 8);
		value += Origin - pDriver->nsfdrv_size;
		pData[pDriver->adr_reloc[i]] = value & 0xFF;
		pData[pDriver->adr_reloc[i + 1]] = value >> 8;
	}

	if (m_iActualChip == SNDCHIP_N163) {
		pData[m_iDriverSize - 2 - 0x100 - 0xC0 * 2 - 8 - 1 - 8 + m_iActualNamcoChannels] = 3;
	}

	if (m_iActualChip & (m_iActualChip - 1)) {		// // // special processing for multichip
		int ptr = FT_UPDATE_EXT_ADR;
		for (int i = 0; i < 6; ++i) {
			ASSERT(pData[ptr] == 0x20); // jsr
			if (!(m_iActualChip & (1 << i))) {
				pData[ptr++] = 0xEA; // nop
				pData[ptr++] = 0xEA;
				pData[ptr++] = 0xEA;
			}
			else
				ptr += 3;
		}

		const int CH_MAP[] = {
			0, 1, 2, 3, 27,
			6, 7, 8,
			4, 5, -1,
			9, 10, 11, 12, 13, 14, 15, 16,
			17,
			21, 22, 23, 24, 25, 26,
			18, 19, 20,
		};

		for (int i = 0; i < CHANNELS; ++i)
			pData[FT_CH_ENABLE_ADR + i] = 0;
		for (const int x : m_vChanOrder)
			pData[FT_CH_ENABLE_ADR + CH_MAP[m_pDocument->GetChannelType(x)]] = 1;
	}

	return (char *)pData;
}

char *CCompiler::LoadNSFDRV(const driver_t *pDriver) const
{
	// Copy embedded driver
	unsigned char *pData = new unsigned char[pDriver->nsfdrv_size];
	memcpy(pData, pDriver->nsfdrv, pDriver->nsfdrv_size);

	return (char *)pData;
}

void CCompiler::SetDriverSongAddress(char *pDriver, unsigned short Address) const
{
	// Write start address of music data
	pDriver[m_iDriverSize - 2] = Address & 0xFF;
	pDriver[m_iDriverSize - 1] = Address >> 8;
}

void CCompiler::PatchVibratoTable(char *pDriver) const
{
	// Copy the vibrato table, the stock one only works for new vibrato mode
	const CSoundGen *pSoundGen = theApp.GetSoundGenerator();

	for (int i = 0; i < 256; ++i) {
		*(pDriver + m_iVibratoTableLocation + i) = (char)pSoundGen->ReadVibratoTable(i);
	}
}

#pragma warning (push)
#pragma warning (disable : 4996)
void CCompiler::CreateHeader(stNSFHeader *pHeader, int MachineType, unsigned int NSF2Flags, bool NSF2) const
{
	// Fill the NSF header
	//
	// Speed will be the same for NTSC/PAL
	//

	unsigned int SpeedPAL, SpeedNTSC, Speed;
	Speed = m_pDocument->GetEngineSpeed();

	// If speed is default, write correct NTSC/PAL speed periods
	// else, set the same custom speed for both
	SpeedNTSC = (Speed == 0) ? 1000000 / CAPU::FRAME_RATE_NTSC : 1000000 / Speed;
	SpeedPAL = (Speed == 0) ? 1000000 / CAPU::FRAME_RATE_PAL : 1000000 / Speed;

	memset(pHeader, 0, 0x80);

	pHeader->Ident[0]	= 0x4E;
	pHeader->Ident[1]	= 0x45;
	pHeader->Ident[2]	= 0x53;
	pHeader->Ident[3]	= 0x4D;
	pHeader->Ident[4]	= 0x1A;

	pHeader->Version	= NSF2 ? 0x02 : 0x01;
	pHeader->TotalSongs	= m_pDocument->GetTrackCount();
	pHeader->StartSong	= 1;
	pHeader->LoadAddr	= m_iLoadAddress;
	pHeader->InitAddr	= m_iInitAddress;
	pHeader->PlayAddr	= m_iInitAddress + 3;

	memset(pHeader->SongName, 0x00, 32);
	memset(pHeader->ArtistName, 0x00, 32);
	memset(pHeader->Copyright, 0x00, 32);

	strncpy((char *)pHeader->SongName, m_pDocument->GetSongName(), 32);
	strncpy((char *)pHeader->ArtistName, m_pDocument->GetSongArtist(), 32);
	strncpy((char *)pHeader->Copyright, m_pDocument->GetSongCopyright(), 32);

	pHeader->Speed_NTSC = SpeedNTSC; //0x411A; // default ntsc speed

	if (m_bBankSwitched) {
		for (int i = 0; i < 4; ++i) {
			unsigned int SampleBank = m_iFirstSampleBank + i;
			pHeader->BankValues[i] = i;
			pHeader->BankValues[i + 4] = (SampleBank < m_iLastBank) ? SampleBank : m_iLastBank;
		}
		if (LAST_BANK_FIXED) {
			// Bind last page to last bank
			pHeader->BankValues[7] = m_iLastBank;
		}
	}
	else {
		for (int i = 0; i < 8; ++i) {
			pHeader->BankValues[i] = 0;
		}
	}

	pHeader->Speed_PAL = SpeedPAL; //0x4E20; // default pal speed

	// Allow PAL or dual tunes only if no expansion chip is selected
	// Expansion chips weren't available in PAL areas
	if (m_pDocument->GetExpansionChip() == SNDCHIP_NONE) {
		switch (MachineType) {
			case 0:	// NTSC
				pHeader->Flags = 0x00;
				break;
			case 1:	// PAL
				pHeader->Flags = 0x01;
				break;
			case 2:	// Dual
				pHeader->Flags = 0x02;
				break;
		}
	}
	else {
		pHeader->Flags = 0x00;
	}

	// Expansion chip
	pHeader->SoundChip = m_iActualChip;		// // //

	pHeader->NSF2Flags = NSF2Flags;
	pHeader->NSFDataLength[0] = 0;		// !! !! Allow this to be overwritten
	pHeader->NSFDataLength[1] = 0;
	pHeader->NSFDataLength[2] = 0;
}
#pragma warning (pop)

void CCompiler::CreateNSFeHeader(stNSFeHeader *pHeader, int MachineType)		// // //
{
	memset(pHeader, 0, 40);

	unsigned int SpeedPAL, SpeedNTSC, Speed;
	Speed = m_pDocument->GetEngineSpeed();
	SpeedNTSC = (Speed == 0) ? 1000000 / CAPU::FRAME_RATE_NTSC : 1000000 / Speed;
	SpeedPAL = (Speed == 0) ? 1000000 / CAPU::FRAME_RATE_PAL : 1000000 / Speed;

	pHeader->InfoSize = 12;
	pHeader->BankSize = 8;
	pHeader->RateSize = 4;

	pHeader->NSFeIdent[0] = 'N';
	pHeader->NSFeIdent[1] = 'S';
	pHeader->NSFeIdent[2] = 'F';
	pHeader->NSFeIdent[3] = 'E';

	pHeader->InfoIdent[0] = 'I';
	pHeader->InfoIdent[1] = 'N';
	pHeader->InfoIdent[2] = 'F';
	pHeader->InfoIdent[3] = 'O';

	pHeader->BankIdent[0] = 'B';
	pHeader->BankIdent[1] = 'A';
	pHeader->BankIdent[2] = 'N';
	pHeader->BankIdent[3] = 'K';

	pHeader->RateIdent[0] = 'R';
	pHeader->RateIdent[1] = 'A';
	pHeader->RateIdent[2] = 'T';
	pHeader->RateIdent[3] = 'E';

	pHeader->TotalSongs	= m_pDocument->GetTrackCount();
	pHeader->StartSong	= 0;
	pHeader->LoadAddr	= m_iLoadAddress;
	pHeader->InitAddr	= m_iInitAddress;
	pHeader->PlayAddr	= m_iInitAddress + 3;

	if (m_bBankSwitched) {
		for (int i = 0; i < 4; ++i) {
			pHeader->BankValues[i] = i;
			pHeader->BankValues[i + 4] = m_iFirstSampleBank + i;
		}
	}
	else {
		for (int i = 0; i < 8; ++i) {
			pHeader->BankValues[i] = 0;
		}
	}

	if (m_pDocument->GetExpansionChip() == SNDCHIP_NONE) {
		switch (MachineType) {
			case 0:	// NTSC
				pHeader->Flags = 0x00;
				break;
			case 1:	// PAL
				pHeader->Flags = 0x01;
				break;
			case 2:	// Dual
				pHeader->Flags = 0x02;
				break;
		}
	}
	else {
		pHeader->Flags = 0x00;
	}

	pHeader->Speed_NTSC = SpeedNTSC;
	pHeader->Speed_PAL = SpeedPAL;
	pHeader->SoundChip = m_iActualChip;		// // //
}

// technically NSFe has no footer
// but the same data will be written in the footer of NSF2
void CCompiler::CreateNSFeFooter(stNSFeFooter *pFooter)
{
	auto emplace_str = [&](auto &bytedata, std::string_view str) {
		bytedata.insert(bytedata.end(), str.begin(), str.end());

		// safely null terminate instead of fudging range
		bytedata.emplace_back(0);
	};

	auto emplace_int32 = [&](auto &bytedata, uint32_t integer) {
		// little endian
		bytedata.emplace_back(uint8_t(integer >> 0x00));
		bytedata.emplace_back(uint8_t(integer >> 0x08));
		bytedata.emplace_back(uint8_t(integer >> 0x10));
		bytedata.emplace_back(uint8_t(integer >> 0x18));
	};

	auto emplace_int16 = [&](auto &bytedata, uint16_t integer) {
		// little endian
		bytedata.emplace_back(uint8_t(integer >> 0x00));
		bytedata.emplace_back(uint8_t(integer >> 0x08));
	};

	// write VRC7 chunk
	memcpy(pFooter->VRC7.Ident, "VRC7", 4);
	{
		// "The first byte designates a variant device replacing the VRC7 at the same register addresses."
		size_t patch_byte_size = size_t(19 * 8);
		bool extOPLL = m_pDocument->GetExternalOPLLChipCheck()
			// YM2413 and YMF281B are considered external OPLL
			|| (theApp.GetSettings()->Emulation.iVRC7Patch > 6);
		pFooter->VRC7.Data.emplace_back(uint8_t(extOPLL));
		if (extOPLL)
			for (unsigned int byte = 0; byte < patch_byte_size; byte++)
				pFooter->VRC7.Data.emplace_back(m_pDocument->GetOPLLPatchByte(byte));
	}

	// write time chunk
	memcpy(pFooter->time.Ident, "time", 4);

	for (unsigned int i = 0; i < m_pDocument->GetTrackCount(); i++) {
		emplace_int32(pFooter->time.Data, static_cast<int32_t>(m_pDocument->GetStandardLength(i, 1) * 1000.0 + 0.5));
	}

	// write tlbl chunk
	memcpy(pFooter->tlbl.Ident, "tlbl", 4);
	for (unsigned int i = 0; i < m_pDocument->GetTrackCount(); i++) {
		emplace_str(pFooter->tlbl.Data, LPCTSTR(m_pDocument->GetTrackTitle(i)));
	}

	// write auth chunk
	memcpy(pFooter->auth.Ident, "auth", 4);
	emplace_str(pFooter->auth.Data, m_pDocument->GetSongName());
	emplace_str(pFooter->auth.Data, m_pDocument->GetSongArtist());
	emplace_str(pFooter->auth.Data, m_pDocument->GetSongCopyright());
	emplace_str(pFooter->auth.Data, APP_NAME_VERSION);

	// write text chunk, if available
	if (strlen(m_pDocument->GetComment())) {
		memcpy(pFooter->text.Ident, "text", 4);
		emplace_str(pFooter->text.Data, LPCTSTR(m_pDocument->GetComment()));
	}

	// write mixe chunk
	memcpy(pFooter->mixe.Ident, "mixe", 4);
	CSoundGen *pSoundGen = theApp.GetSoundGenerator();

	for (uint8_t i = 0; i < CHIP_LEVEL_COUNT; i++) {
		if (m_pDocument->GetLevelOffset(i) != 0) {
			pFooter->mixe.Data.emplace_back(i);
			emplace_int16(pFooter->mixe.Data, int16_t(m_pDocument->GetLevelOffset(i) * 10 + pSoundGen->SurveyMixLevels[i]));
		}
	}

	// write NEND chunk
	memcpy(pFooter->NEND.Ident, "NEND", 4);

	auto size_little_endian = [&](stNSFeChunk &chunk) {
		chunk.Size[0] = uint8_t(chunk.Data.size() >> 0x00);
		chunk.Size[1] = uint8_t(chunk.Data.size() >> 0x08);
		chunk.Size[2] = uint8_t(chunk.Data.size() >> 0x10);
		chunk.Size[3] = uint8_t(chunk.Data.size() >> 0x18);
	};

	// write size in little endian
	size_little_endian(pFooter->VRC7);
	size_little_endian(pFooter->time);
	size_little_endian(pFooter->auth);
	size_little_endian(pFooter->tlbl);
	size_little_endian(pFooter->text);
	size_little_endian(pFooter->mixe);
	size_little_endian(pFooter->NEND);
}

void CCompiler::UpdateSamplePointers(unsigned int Origin)
{
	// Rewrite sample pointer list with valid addresses
	//
	// TODO: rewrite this to utilize the CChunkDataBank to resolve bank numbers automatically
	//

	ASSERT(m_pSamplePointersChunk != NULL);

	unsigned int Address = Origin;
	unsigned int Bank = m_iFirstSampleBank;

	if (!m_bBankSwitched)
		Bank = 0;			// Disable DPCM bank switching

	m_pSamplePointersChunk->Clear();

	// The list is stored in the same order as the samples vector

	Print(" * DPCM samples:\n");

	for (auto pDSample : m_vSamples) {
		unsigned int Size = pDSample->GetSize();

		if (m_bBankSwitched) {
			if ((Address + Size) >= DPCM_SWITCH_ADDRESS) {
				Address = PAGE_SAMPLES;
				Bank += DPCM_PAGE_WINDOW;
			}
		}

		// Store
		m_pSamplePointersChunk->StoreByte(Address >> 6);
		m_pSamplePointersChunk->StoreByte(Size >> 4);
		m_pSamplePointersChunk->StoreByte(Bank);

		Print("      %s\n", pDSample->GetName());
		Print("         Address $%04X, bank %i (%i bytes)\n", Address, Bank, Size);

		Address += Size;
		Address += AdjustSampleAddress(Address);
	}

	Print("      Sample banks: %i\n", Bank - m_iFirstSampleBank + DPCM_PAGE_WINDOW);

	// Save last bank number for NSF header
	m_iLastBank = Bank + 1;
}

void CCompiler::UpdateFrameBanks()
{
	// Write bank numbers to frame lists (can only be used when bankswitching is used)
	ASSERT(m_bBankSwitched);

	int Channels = m_pDocument->GetAvailableChannels();

	for (CChunk *pChunk : m_vFrameChunks) {
		if (pChunk->GetType() == CHUNK_FRAME) {
			// Add bank data
			for (int j = 0; j < Channels; ++j) {
				unsigned char bank = GetObjectByRef(pChunk->GetDataRefName(j))->GetBank();
				if (bank < PATTERN_SWITCH_BANK)
					bank = PATTERN_SWITCH_BANK;
				pChunk->SetupBankData(j + Channels, bank);
			}
		}
	}
}

void CCompiler::UpdateSongBanks()
{
	// Write bank numbers to song lists (can only be used when bankswitching is used)
	ASSERT(m_bBankSwitched);
	for (CChunk *pChunk : m_vSongChunks) {
		int bank = GetObjectByRef(pChunk->GetDataRefName(0))->GetBank();
		if (bank < PATTERN_SWITCH_BANK)
			bank = PATTERN_SWITCH_BANK;
		pChunk->SetupBankData(m_iSongBankReference, bank);
	}
}

void CCompiler::ClearSongBanks()
{
	// Clear bank data in song chunks
	for (CChunk *pChunk : m_vSongChunks)
		pChunk->SetupBankData(m_iSongBankReference, 0);
}

void CCompiler::EnableBankswitching()
{
	// Set bankswitching flag in the song header
	ASSERT(m_pHeaderChunk != NULL);
	unsigned char flags = (unsigned char)m_pHeaderChunk->GetData(m_iHeaderFlagOffset);
	flags |= FLAG_BANKSWITCHED;
	m_pHeaderChunk->ChangeByte(m_iHeaderFlagOffset, flags);
}

void CCompiler::ResolveLabels()
{
	// Resolve label addresses, no banks since bankswitching is disabled
	CMap<CStringA, LPCSTR, int, int> labelMap;

	// Pass 1, collect labels
	CollectLabels(labelMap);

	// Pass 2
	AssignLabels(labelMap);
}

bool CCompiler::ResolveLabelsBankswitched()
{
	// Resolve label addresses and banks
	CMap<CStringA, LPCSTR, int, int> labelMap;

	// Pass 1, collect labels
	if (!CollectLabelsBankswitched(labelMap))
		return false;

	// Pass 2
	AssignLabels(labelMap);

	return true;
}

void CCompiler::CollectLabels(CMap<CStringA, LPCSTR, int, int> &labelMap) const
{
	// Collect labels and assign offsets
	int Offset = 0;
	for (const CChunk *pChunk : m_vChunks) {
		labelMap[pChunk->GetLabel()] = Offset;
		Offset += pChunk->CountDataSize();
	}
}

bool CCompiler::CollectLabelsBankswitched(CMap<CStringA, LPCSTR, int, int> &labelMap)
{
	int Offset = 0;
	int Bank = PATTERN_SWITCH_BANK;

	int DriverSizeAndNSFDRV = m_iDriverSize + m_iNSFDRVSize;

	// Instruments and stuff
	for (const CChunk *pChunk : m_vChunks) {
		int Size = pChunk->CountDataSize();

		switch (pChunk->GetType()) {
			case CHUNK_FRAME_LIST:
			case CHUNK_FRAME:
			case CHUNK_PATTERN:
				break;
			default:
				labelMap[pChunk->GetLabel()] = Offset;
				Offset += Size;
		}
	}


	int PatternSwitchBankMaxSize = PAGE_SAMPLES - PAGE_BANKED;
	int FixedBankMaxSize = PAGE_BANKED - PAGE_START;
	int FixedBankPages = PATTERN_SWITCH_BANK + 1;

	if (Offset + DriverSizeAndNSFDRV > 0xFFFF) {
		// Instrument data did not fit within the limit, display an error and abort?
		Print("Error: Instrument, frame & pattern data can't fit within bank allocation, can't export file!\n");
		Print(" * $%02X bytes used out of $FFFF allowed\n", Offset + DriverSizeAndNSFDRV);
		return false;
	}

	unsigned int Track = 0;

	// The switchable area is $B000-$C000
	for (CChunk *pChunk : m_vChunks) {
		int Size = pChunk->CountDataSize();

		switch (pChunk->GetType()) {
			case CHUNK_FRAME_LIST:
				// Make sure the entire frame list will fit, if not then allocate a new bank
				if (Offset + DriverSizeAndNSFDRV + (int)m_iTrackFrameSize[Track++] > FixedBankMaxSize + PatternSwitchBankMaxSize) {
					Offset = FixedBankMaxSize - DriverSizeAndNSFDRV;
					++Bank;
				}
				// fall through
			case CHUNK_FRAME:
				labelMap[pChunk->GetLabel()] = Offset;
				pChunk->SetBank(Bank < FixedBankPages ? ((Offset + DriverSizeAndNSFDRV) >> 12) : Bank);
				Offset += Size;
				break;
			case CHUNK_PATTERN:
				// Make sure entire pattern will fit
				if (Offset + DriverSizeAndNSFDRV + Size > FixedBankMaxSize + PatternSwitchBankMaxSize) {
					Offset = FixedBankMaxSize - DriverSizeAndNSFDRV;
					++Bank;
				}
				labelMap[pChunk->GetLabel()] = Offset;
				pChunk->SetBank(Bank < FixedBankPages ? ((Offset + DriverSizeAndNSFDRV) >> 12) : Bank);
				Offset += Size;
				// fall through
			default:
				break;
		}
	}

	if (m_bBankSwitched)
		m_iFirstSampleBank = ((Bank < FixedBankPages) ? ((Offset + DriverSizeAndNSFDRV) >> 12) : Bank) + 1;

	m_iLastBank = m_iFirstSampleBank;

	return true;
}

void CCompiler::AssignLabels(CMap<CStringA, LPCSTR, int, int> &labelMap)
{
	// Pass 2: assign addresses to labels
	for (CChunk *pChunk : m_vChunks)
		pChunk->AssignLabels(labelMap);
}

bool CCompiler::CompileData(bool bUseNSFDRV, bool bUseAllExp)
{
	// Compile music data to an object tree
	//

	// // // Full chip export
	m_iActualChip = m_pDocument->GetExpansionChip();
	m_iActualNamcoChannels = m_pDocument->GetNamcoChannels();

	// Select driver and channel order
	switch (m_pDocument->GetExpansionChip()) {
		case SNDCHIP_NONE:
			m_pDriverData = &DRIVER_PACK_2A03;
			m_iVibratoTableLocation = VIBRATO_TABLE_LOCATION_2A03;
			Print(" * No expansion chip\n");
			break;
		case SNDCHIP_VRC6:
			m_pDriverData = &DRIVER_PACK_VRC6;
			m_iVibratoTableLocation = VIBRATO_TABLE_LOCATION_VRC6;
			Print(" * VRC6 expansion enabled\n");
			break;
		case SNDCHIP_MMC5:
			m_pDriverData = &DRIVER_PACK_MMC5;
			m_iVibratoTableLocation = VIBRATO_TABLE_LOCATION_MMC5;
			Print(" * MMC5 expansion enabled\n");
			break;
		case SNDCHIP_VRC7:
			m_pDriverData = &DRIVER_PACK_VRC7;
			m_iVibratoTableLocation = VIBRATO_TABLE_LOCATION_VRC7;
			Print(" * VRC7 expansion enabled\n");
			break;
		case SNDCHIP_FDS:
			m_pDriverData = &DRIVER_PACK_FDS;
			m_iVibratoTableLocation = VIBRATO_TABLE_LOCATION_FDS;
			Print(" * FDS expansion enabled\n");
			break;
		case SNDCHIP_N163:
			m_pDriverData = &DRIVER_PACK_N163;
			m_iVibratoTableLocation = VIBRATO_TABLE_LOCATION_N163;
			Print(" * N163 expansion enabled\n");
			break;
		case SNDCHIP_5B:
			m_pDriverData = &DRIVER_PACK_S5B;
			m_iVibratoTableLocation = VIBRATO_TABLE_LOCATION_S5B;
			Print(" * S5B expansion enabled\n");
			break;
		default:		// // // crude, not meant for release
			m_pDriverData = &DRIVER_PACK_ALL;
			m_iVibratoTableLocation = VIBRATO_TABLE_LOCATION_ALL;
			Print(" * Multiple expansion chips enabled\n");
//			if (m_pDocument->ExpansionEnabled(SNDCHIP_N163))
//				m_pDocument->SetNamcoChannels(8, true);
//			m_pDocument->SelectExpansionChip(0x3F, true);
			break;
	}

	// // // Setup channel order list, DPCM is located last
	const int Channels = m_pDocument->GetAvailableChannels();
	const int Chip = m_pDocument->GetExpansionChip(); // 0CC: use m_iActualChip once cc65 is embedded
	int Channel = 0;
	for (int i = 0; i < 4; i++) {
		int Channel = m_pDocument->GetChannelIndex(CHANID_2A03_SQUARE1 + i);
		m_vChanOrder.push_back(Channel);
	}
	if (Chip & SNDCHIP_MMC5) for (int i = 0; i < 3; i++) {
		int Channel = m_pDocument->GetChannelIndex(CHANID_MMC5_SQUARE1 + i);
		m_vChanOrder.push_back(Channel);
	}
	if (Chip & SNDCHIP_VRC6) for (int i = 0; i < 3; i++) {
		int Channel = m_pDocument->GetChannelIndex(CHANID_VRC6_PULSE1 + i);
		m_vChanOrder.push_back(Channel);
	}
	if (Chip & SNDCHIP_N163) {
		int lim = m_iActualNamcoChannels;
//		if (Chip & ~SNDCHIP_N163) lim = 8;
		for (int i = 0; i < lim; i++) { // 0CC: use m_iActualNamcoChannels once cc65 is embedded
			int Channel = m_pDocument->GetChannelIndex(CHANID_N163_CH1 + i);
			m_vChanOrder.push_back(Channel);
		}
	}
	if (Chip & SNDCHIP_FDS) {
		int Channel = m_pDocument->GetChannelIndex(CHANID_FDS);
		m_vChanOrder.push_back(Channel);
	}
	if (Chip & SNDCHIP_5B) for (int i = 0; i < 3; i++) {
		int Channel = m_pDocument->GetChannelIndex(CHANID_5B_CH1 + i);
		m_vChanOrder.push_back(Channel);
	}
	if (Chip & SNDCHIP_VRC7) for (int i = 0; i < 6; i++) {
		int Channel = m_pDocument->GetChannelIndex(CHANID_VRC7_CH1 + i);
		m_vChanOrder.push_back(Channel);
	}
	m_vChanOrder.push_back(CHANID_2A03_DPCM);

	// set NSFDRV header offset, if used
	SetNSFDRVHeaderSize(bUseNSFDRV);

	// Driver size
	m_iDriverSize = m_pDriverData->driver_size;

	// Scan and optimize song
	ScanSong();

	Print("Building music data...\n");

	// Build music data
	CreateMainHeader(bUseAllExp);
	CreateSequenceList();
	CreateInstrumentList();
	CreateSampleList();
	StoreSamples();
	StoreGrooves();		// // //
	StoreSongs();

	// Determine if bankswitching is needed
	m_bBankSwitched = false;
	m_iMusicDataSize = CountData();

	// Get samples start address
	m_iSampleStart = m_iNSFDRVSize + m_iDriverSize + m_iMusicDataSize;

	// Align to closest 64-byte page after $C000
	if (m_iSampleStart < 0x4000)
		m_iSampleStart = PAGE_SAMPLES;
	else
		m_iSampleStart += AdjustSampleAddress(m_iSampleStart) + PAGE_START;

	// If sample size exceeds 16KiB, enable bankswitching
	if (m_iSampleStart + m_iSamplesSize > 0xFFFF)
		m_bBankSwitched = true;

	if (m_iSamplesSize > 0x4000)
		m_bBankSwitched = true;

	// if driver, music, and sample data exceeds 32KiB, enable bankswitching
	if ((m_iNSFDRVSize + m_iMusicDataSize + m_iSamplesSize + m_iDriverSize) > 0x8000)
		m_bBankSwitched = true;

	if (m_bBankSwitched)
		m_iSampleStart = PAGE_SAMPLES;

	// Compiling done
	Print(" * Samples located at: $%04X\n", m_iSampleStart);

#ifdef FORCE_BANKSWITCH
	m_bBankSwitched = true;
#endif /* FORCE_BANKSWITCH */

	return true;
}

void CCompiler::Cleanup()
{
	// Delete objects

	for (CChunk *pChunk : m_vChunks)
		delete pChunk;

	m_vChunks.clear();
	m_vSequenceChunks.clear();
	m_vInstrumentChunks.clear();
	m_vGrooveChunks.clear();		// // //
	m_vSongChunks.clear();
	m_vFrameChunks.clear();
	m_vPatternChunks.clear();

	m_pSamplePointersChunk = NULL;	// This pointer is also stored in m_vChunks
	m_pHeaderChunk = NULL;

	// // // Full chip export
	if (m_pDocument->GetNamcoChannels() != m_iActualNamcoChannels ||
		m_pDocument->GetExpansionChip() != m_iActualChip)
	{
		m_pDocument->SetNamcoChannels(m_iActualNamcoChannels, true);
		m_pDocument->SelectExpansionChip(m_iActualChip, true);
	}
}

void CCompiler::CalculateLoadAddresses(unsigned short &MusicDataAddress, bool &bCompressedMode, bool ForceDecompress)
{
	// Find out load address

	// if we can fit the entire music and driver within the first 16kB of data,
	// enable compressed mode
	bCompressedMode = !((PAGE_SAMPLES - m_iDriverSize - m_iMusicDataSize - m_iNSFDRVSize) < 0x8000
		|| m_bBankSwitched || m_iActualChip != m_pDocument->GetExpansionChip());

	if (bCompressedMode && !ForceDecompress) {
		// Locate driver at $C000 - (driver size)
		m_iLoadAddress = PAGE_SAMPLES - m_iDriverSize - m_iMusicDataSize - m_iNSFDRVSize;
		m_iDriverAddress = PAGE_SAMPLES - m_iDriverSize;
		MusicDataAddress = m_iLoadAddress + m_iNSFDRVSize;
	}
	else {
		// Locate driver at $8000
		m_iLoadAddress = PAGE_START;
		m_iDriverAddress = m_iLoadAddress + m_iNSFDRVSize;
		MusicDataAddress = m_iDriverAddress + m_iDriverSize;
	}

	// Init is located at the driver
	m_iInitAddress = m_iDriverAddress;		// !! !!
}

void CCompiler::SetNSFDRVHeaderSize(bool bUseNSFDRV)
{
	m_iNSFDRVSize = bUseNSFDRV ? m_pDriverData->nsfdrv_size : 0;
}

void CCompiler::AddBankswitching()
{
	// Add bankswitching data

	for (CChunk *pChunk : m_vChunks) {
		// Frame chunks
		if (pChunk->GetType() == CHUNK_FRAME) {
			int Length = pChunk->GetLength();
			// Bank data is located at end
			for (int j = 0; j < Length; ++j) {
				pChunk->StoreBankReference(pChunk->GetDataRefName(j), 0);
			}
		}
	}

	// Frame lists sizes has changed
	const int TrackCount = m_pDocument->GetTrackCount();
	for (int i = 0; i < TrackCount; ++i) {
		m_iTrackFrameSize[i] += m_pDocument->GetChannelCount() * m_pDocument->GetFrameCount(i);
	}

	// Data size has changed
	m_iMusicDataSize = CountData();
}

void CCompiler::ScanSong()
{
	// Scan and optimize song
	//

	// Re-assign instruments
	m_iInstruments = 0;

	memset(m_iAssignedInstruments, 0, sizeof(int) * MAX_INSTRUMENTS);
	// TODO: remove these
	memset(m_bSequencesUsed2A03, false, sizeof(bool) * MAX_SEQUENCES * SEQ_COUNT);
	memset(m_bSequencesUsedVRC6, false, sizeof(bool) * MAX_SEQUENCES * SEQ_COUNT);
	memset(m_bSequencesUsedN163, false, sizeof(bool) * MAX_SEQUENCES * SEQ_COUNT);
	memset(m_bSequencesUsedS5B, false, sizeof(bool) * MAX_SEQUENCES * SEQ_COUNT);		// // //
	static const inst_type_t inst[] = { INST_2A03, INST_VRC6, INST_N163, INST_S5B };		// // //
	bool *used[] = { *m_bSequencesUsed2A03, *m_bSequencesUsedVRC6, *m_bSequencesUsedN163, *m_bSequencesUsedS5B };

	for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
		if (m_pDocument->IsInstrumentUsed(i) && IsInstrumentInPattern(i)) {

			// List of used instruments
			m_iAssignedInstruments[m_iInstruments++] = i;

			// Create a list of used sequences
			inst_type_t it = m_pDocument->GetInstrumentType(i);		// // //
			for (size_t z = 0; z < sizeof(used) / sizeof(bool *); z++) if (it == inst[z]) {
				auto pInstrument = std::static_pointer_cast<CSeqInstrument>(m_pDocument->GetInstrument(i));
				for (int j = 0; j < SEQ_COUNT; ++j) if (pInstrument->GetSeqEnable(j))
					*(used[z] + pInstrument->GetSeqIndex(j) * SEQ_COUNT + j) = true;
				break;
			}
		}
	}

	// See which samples are used
	m_iSamplesUsed = 0;

	memset(m_bSamplesAccessed, 0, MAX_INSTRUMENTS * OCTAVE_RANGE * NOTE_RANGE * sizeof(bool));

	// Get DPCM channel index
	const int DpcmChannel = m_pDocument->GetChannelIndex(CHANID_2A03_DPCM);
	const int TrackCount = m_pDocument->GetTrackCount();
	unsigned int Instrument = 0;

	for (int i = 0; i < TrackCount; ++i) {
		const int patternlen = m_pDocument->GetPatternLength(i);
		const int frames = m_pDocument->GetFrameCount(i);
		for (int j = 0; j < frames; ++j) {
			int p = m_pDocument->GetPatternAtFrame(i, j, DpcmChannel);
			for (int k = 0; k < patternlen; ++k) {
				stChanNote Note;
				m_pDocument->GetDataAtPattern(i, p, DpcmChannel, k, &Note);
				if (Note.Instrument < MAX_INSTRUMENTS)
					Instrument = Note.Instrument;
				if (Note.Note > 0) {
					m_bSamplesAccessed[Instrument][Note.Octave][Note.Note - 1] = true;
				}
			}
		}
	}
}

bool CCompiler::IsInstrumentInPattern(int index) const
{
	// Returns true if the instrument is used in a pattern

	const int TrackCount = m_pDocument->GetTrackCount();
	const int Channels = m_pDocument->GetAvailableChannels();

	// Scan patterns in entire module
	for (int i = 0; i < TrackCount; ++i) {
		int PatternLength = m_pDocument->GetPatternLength(i);
		for (int j = 0; j < Channels; ++j) {
			for (int k = 0; k < MAX_PATTERN; ++k) {
				for (int l = 0; l < PatternLength; ++l) {
					stChanNote Note;
					m_pDocument->GetDataAtPattern(i, k, j, l, &Note);
					if (Note.Instrument == index)
						return true;
				}
			}
		}
	}

	return false;
}

void CCompiler::CreateMainHeader(bool UseAllExp)
{
	// Writes the music header
	int TicksPerSec = m_pDocument->GetEngineSpeed();

	unsigned short DividerNTSC, DividerPAL;

	int Chip = m_pDocument->GetExpansionChip();		// // //
	bool bMultichip = ((Chip & (Chip - 1)) != 0) && UseAllExp;

	CChunk *pChunk = CreateChunk(CHUNK_HEADER, "");

	if (TicksPerSec == 0) {
		// Default
		DividerNTSC = CAPU::FRAME_RATE_NTSC * 60;
		DividerPAL	= CAPU::FRAME_RATE_PAL * 60;
	}
	else {
		// Custom
		DividerNTSC = TicksPerSec * 60;
		DividerPAL = TicksPerSec * 60;
	}

	unsigned char Flags = 0; // bankswitch flag is set later
	if (m_pDocument->GetVibratoStyle() == VIBRATO_OLD) Flags |= FLAG_VIBRATO;
	if (m_pDocument->GetLinearPitch()) Flags |= FLAG_LINEARPITCH;		// // //

	// FLAG_USEPAL is dynamically set during runtime

	// Write header

	pChunk->StoreReference(CChunkRenderText::LABEL_SONG_LIST);
	pChunk->StoreReference(CChunkRenderText::LABEL_INSTRUMENT_LIST);
	pChunk->StoreReference(CChunkRenderText::LABEL_SAMPLES_LIST);
	pChunk->StoreReference(CChunkRenderText::LABEL_SAMPLES);
	pChunk->StoreReference(CChunkRenderText::LABEL_GROOVE_LIST);		// // //

	m_iHeaderFlagOffset = pChunk->GetLength();		// Save the flags offset
	pChunk->StoreByte(Flags);

	// FDS table, only if FDS is enabled
	if ((Chip & SNDCHIP_FDS) || bMultichip)
		if (!(Chip & SNDCHIP_FDS))
			pChunk->StoreReference("0");
		else
			pChunk->StoreReference(CChunkRenderText::LABEL_WAVETABLE);

	pChunk->StoreWord(DividerNTSC);
	pChunk->StoreWord(DividerPAL);

	// N163 channel count
	if ((Chip & SNDCHIP_N163) || bMultichip) {
		/*if (m_pDocument->GetExpansionChip() != SNDCHIP_N163)		// // //
			pChunk->StoreByte(8);
		else*/ pChunk->StoreByte(std::max(m_iActualNamcoChannels, 1));
	}

	m_pHeaderChunk = pChunk;
}

// Sequences

void CCompiler::CreateSequenceList()
{
	// Create sequence lists
	//

	unsigned int Size = 0, StoredCount = 0;
	static const inst_type_t inst[] = { INST_2A03, INST_VRC6, INST_N163, INST_S5B };
	const bool *used[] = { *m_bSequencesUsed2A03, *m_bSequencesUsedVRC6, *m_bSequencesUsedN163, *m_bSequencesUsedS5B };
	static const char *format[] = {
		CChunkRenderText::LABEL_SEQ_2A03, CChunkRenderText::LABEL_SEQ_VRC6,
		CChunkRenderText::LABEL_SEQ_N163, CChunkRenderText::LABEL_SEQ_S5B
	};

	// TODO: use the CSeqInstrument::GetSequence
	// TODO: merge identical sequences from all chips
	for (size_t c = 0; c < sizeof(inst) / sizeof(inst_type_t); c++) {
		for (int i = 0; i < MAX_SEQUENCES; ++i)  for (int j = 0; j < SEQ_COUNT; ++j) {
			CSequence *pSeq = m_pDocument->GetSequence(inst[c], i, j);
			int Index = i * SEQ_COUNT + j;
			if (*(used[c] + Index) && pSeq->GetItemCount() > 0) {
				CStringA label;
				label.Format(format[c], Index);
				Size += StoreSequence(pSeq, label);
				++StoredCount;
			}
		}
	}

	for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
		if (auto pInstrument = std::dynamic_pointer_cast<CInstrumentFDS>(m_pDocument->GetInstrument(i))) {
			for (int j = 0; j < CInstrumentFDS::SEQUENCE_COUNT; ++j) {
				const CSequence *pSeq = pInstrument->GetSequence(j);		// // //
				if (pSeq->GetItemCount() > 0) {
					int Index = i * SEQ_COUNT + j;
					CStringA label;
					label.Format(CChunkRenderText::LABEL_SEQ_FDS, Index);
					Size += StoreSequence(pSeq, label);
					++StoredCount;
				}
			}
		}
	}

	Print(" * Sequences used: %i (%i bytes)\n", StoredCount, Size);
}

int CCompiler::StoreSequence(const CSequence *pSeq, CStringA &label)
{
	CChunk *pChunk = CreateChunk(CHUNK_SEQUENCE, label);
	m_vSequenceChunks.push_back(pChunk);

	// Store the sequence
	int iItemCount	  = pSeq->GetItemCount();
	int iLoopPoint	  = pSeq->GetLoopPoint();
	int iReleasePoint = pSeq->GetReleasePoint();
	int iSetting	  = pSeq->GetSetting();

	if (iReleasePoint != -1)
		iReleasePoint += 1;
	else
		iReleasePoint = 0;

	if (iLoopPoint > iItemCount)
		iLoopPoint = -1;

	pChunk->StoreByte((unsigned char)iItemCount);
	pChunk->StoreByte((unsigned char)iLoopPoint);
	pChunk->StoreByte((unsigned char)iReleasePoint);
	pChunk->StoreByte((unsigned char)iSetting);

	for (int i = 0; i < iItemCount; ++i) {
		pChunk->StoreByte(pSeq->GetItem(i));
	}

	// Return size of this chunk
	return iItemCount + 4;
}

// Instruments

void CCompiler::CreateInstrumentList()
{
	/*
	 * Create the instrument list
	 *
	 * The format of instruments depends on the type
	 *
	 */

	unsigned int iTotalSize = 0;
	CChunk *pWavetableChunk = NULL;	// FDS
	CChunk *pWavesChunk = NULL;		// N163
	int iWaveSize = 0;				// N163 waves size

	CChunk *pInstListChunk = CreateChunk(CHUNK_INSTRUMENT_LIST, CChunkRenderText::LABEL_INSTRUMENT_LIST);

	if (m_pDocument->ExpansionEnabled(SNDCHIP_FDS)) {
		pWavetableChunk = CreateChunk(CHUNK_WAVETABLE, CChunkRenderText::LABEL_WAVETABLE);
	}

	memset(m_iWaveBanks, -1, MAX_INSTRUMENTS * sizeof(int));

	// Collect N163 waves
	for (unsigned int i = 0; i < m_iInstruments; ++i) {
		int iIndex = m_iAssignedInstruments[i];
		if (m_pDocument->GetInstrumentType(iIndex) == INST_N163 && m_iWaveBanks[i] == -1) {
			auto pInstrument = std::static_pointer_cast<CInstrumentN163>(m_pDocument->GetInstrument(iIndex));
			for (unsigned int j = i + 1; j < m_iInstruments; ++j) {
				int inst = m_iAssignedInstruments[j];
				if (m_pDocument->GetInstrumentType(inst) == INST_N163 && m_iWaveBanks[j] == -1) {
					auto pNewInst = std::static_pointer_cast<CInstrumentN163>(m_pDocument->GetInstrument(inst));
					if (pInstrument->IsWaveEqual(pNewInst.get())) {
						m_iWaveBanks[j] = iIndex;
					}
				}
			}
			if (m_iWaveBanks[i] == -1) {
				m_iWaveBanks[i] = iIndex;
				// Store wave
				CStringA label;
				label.Format(CChunkRenderText::LABEL_WAVES, iIndex);
				pWavesChunk = CreateChunk(CHUNK_WAVES, label);
				// Store waves
				iWaveSize += pInstrument->StoreWave(pWavesChunk);
			}
		}
	}

	// Store instruments
	for (unsigned int i = 0; i < m_iInstruments; ++i) {
		// Add reference to instrument list
		CStringA label;
		label.Format(CChunkRenderText::LABEL_INSTRUMENT, i);
		pInstListChunk->StoreReference(label);
		iTotalSize += 2;

		// Actual instrument
		CChunk *pChunk = CreateChunk(CHUNK_INSTRUMENT, label);
		m_vInstrumentChunks.push_back(pChunk);

		int iIndex = m_iAssignedInstruments[i];
		auto pInstrument = m_pDocument->GetInstrument(iIndex);
/*
		if (pInstrument->GetType() == INST_N163) {
			CString label;
			label.Format(LABEL_WAVES, iIndex);
			pWavesChunk = CreateChunk(CHUNK_WAVES, label);
			// Store waves
			iWaveSize += ((CInstrumentN163*)pInstrument)->StoreWave(pWavesChunk);
		}
*/

		if (pInstrument->GetType() == INST_N163) {
			// Translate wave index
			iIndex = m_iWaveBanks[i];
		}

		// Returns number of bytes 
		iTotalSize += pInstrument->Compile(pChunk, iIndex);		// // //

		// // // Check if FDS
		if (pInstrument->GetType() == INST_FDS && pWavetableChunk != NULL) {
			// Store wave
			AddWavetable(std::static_pointer_cast<CInstrumentFDS>(pInstrument).get(), pWavetableChunk);
			pChunk->StoreByte(m_iWaveTables - 1);
		}
	}

	Print(" * Instruments used: %i (%i bytes)\n", m_iInstruments, iTotalSize);

	if (iWaveSize > 0)
		Print(" * N163 waves size: %i bytes\n", iWaveSize);
}

// Samples

void CCompiler::CreateSampleList()
{
	/*
	 * DPCM instrument list
	 *
	 * Each item is stored as a pair of the sample pitch and pointer to the sample table
	 *
	 */

	const int SAMPLE_ITEM_WIDTH = 3;	// 3 bytes / sample item

	// Clear the sample list
	memset(m_iSampleBank, 0xFF, MAX_DSAMPLES);

	CChunk *pChunk = CreateChunk(CHUNK_SAMPLE_LIST, CChunkRenderText::LABEL_SAMPLES_LIST);

	// Store sample instruments
	unsigned int Item = 0;
	for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
		if (m_pDocument->IsInstrumentUsed(i) && m_pDocument->GetInstrumentType(i) == INST_2A03) {
			auto pInstrument = std::static_pointer_cast<CInstrument2A03>(m_pDocument->GetInstrument(i));

			for (int j = 0; j < OCTAVE_RANGE; ++j) {
				for (int k = 0; k < NOTE_RANGE; ++k) {
					// Get sample
					unsigned char iSample = pInstrument->GetSampleIndex(j, k);
					if ((iSample > 0) && m_bSamplesAccessed[i][j][k] && m_pDocument->IsSampleUsed(iSample - 1)) {

						unsigned char SamplePitch = pInstrument->GetSamplePitch(j, k);
						unsigned char SampleIndex = GetSampleIndex(iSample - 1);
						unsigned int  SampleDelta = pInstrument->GetSampleDeltaValue(j, k);
						SamplePitch |= (SamplePitch & 0x80) >> 1;

						// Save a reference to this item
						m_iSamplesLookUp[i][j][k] = ++Item;

						pChunk->StoreByte(SamplePitch);
						pChunk->StoreByte(SampleDelta);
						pChunk->StoreByte(SampleIndex * SAMPLE_ITEM_WIDTH);
					}
					else
						// No instrument here
						m_iSamplesLookUp[i][j][k] = 0;
				}
			}
		}
	}
}

void CCompiler::StoreSamples()
{
	/*
	 * DPCM sample list
	 *
	 * Each sample is stored as a pair of the sample address and sample size
	 *
	 */

	unsigned int iAddedSamples = 0;
	unsigned int iSampleAddress = 0x0000;

	// Get sample start address
	m_iSamplesSize = 0;

	CChunk *pChunk = CreateChunk(CHUNK_SAMPLE_POINTERS, CChunkRenderText::LABEL_SAMPLES);
	m_pSamplePointersChunk = pChunk;

	// Store DPCM samples in a separate array
	for (unsigned int i = 0; i < m_iSamplesUsed; ++i) {

		unsigned int iIndex = m_iSampleBank[i];
		ASSERT(iIndex != 0xFF);
		const CDSample *pDSample = m_pDocument->GetSample(iIndex);
		unsigned int iSize = pDSample->GetSize();

		if (iSize > 0) {
			// Fill sample list
			unsigned char iSampleAddr = iSampleAddress >> 6;
			unsigned char iSampleSize = iSize >> 4;
			unsigned char iSampleBank = 0;

			// Update SAMPLE_ITEM_WIDTH here
			pChunk->StoreByte(iSampleAddr);
			pChunk->StoreByte(iSampleSize);
			pChunk->StoreByte(iSampleBank);

			// Add this sample to storage
			m_vSamples.push_back(pDSample);

			// Pad end of samples
			unsigned int iAdjust = AdjustSampleAddress(iSampleAddress + iSize);

			iAddedSamples++;
			iSampleAddress += iSize + iAdjust;
			m_iSamplesSize += iSize + iAdjust;
		}
	}

	Print(" * DPCM samples used: %i (%i bytes)\n", m_iSamplesUsed, m_iSamplesSize);
}

int CCompiler::GetSampleIndex(int SampleNumber)
{
	// Returns a sample pos from the sample bank
	for (int i = 0; i < MAX_DSAMPLES; i++) {
		if (m_iSampleBank[i] == SampleNumber)
			return i;							// Sample is already stored
		else if (m_iSampleBank[i] == 0xFF) {
			m_iSampleBank[i] = SampleNumber;	// Allocate new position
			m_iSamplesUsed++;
			return i;
		}
	}

	// TODO: Fail if getting here!!!
	return SampleNumber;
}

// // // Groove list

void CCompiler::StoreGrooves()
{
	/*
	 * Store grooves
	 */

	unsigned int Size = 1, Count = 0;

	CChunk *pGrooveListChunk = CreateChunk(CHUNK_GROOVE_LIST, CChunkRenderText::LABEL_GROOVE_LIST);
	pGrooveListChunk->StoreByte(0); // padding; possibly used to disable groove

	for (int i = 0; i < MAX_GROOVE; i++) {
		unsigned int Pos = Size;
		CGroove *Groove = m_pDocument->GetGroove(i);
		if (Groove == NULL) continue;

		CStringA label;
		label.Format(CChunkRenderText::LABEL_GROOVE, i);
		// pGrooveListChunk->StoreReference(label);

		CChunk *pChunk = CreateChunk(CHUNK_GROOVE, label);
		m_vGrooveChunks.push_back(pChunk);
		for (int j = 0; j < Groove->GetSize(); j++) {
			pChunk->StoreByte(Groove->GetEntry(j));
			Size++;
		}
		pChunk->StoreByte(0);
		pChunk->StoreByte(Pos);
		Size += 2;
		Count++;
	}

	Print(" * Grooves used: %i (%i bytes)\n", Count, Size);
}

// Songs

void CCompiler::StoreSongs()
{
	/*
	 * Store patterns and frames for each song
	 *
	 */

	const int TrackCount = m_pDocument->GetTrackCount();

	CChunk *pSongListChunk = CreateChunk(CHUNK_SONG_LIST, CChunkRenderText::LABEL_SONG_LIST);

	m_iDuplicatePatterns = 0;

	// Store song info
	for (int i = 0; i < TrackCount; ++i) {
		// Add reference to song list
		CStringA label;
		label.Format(CChunkRenderText::LABEL_SONG, i);
		pSongListChunk->StoreReference(label);

		// Create song
		CChunk *pChunk = CreateChunk(CHUNK_SONG, label);
		m_vSongChunks.push_back(pChunk);

		// Store reference to song
		label.Format(CChunkRenderText::LABEL_SONG_FRAMES, i);
		pChunk->StoreReference(label);
		pChunk->StoreByte(m_pDocument->GetFrameCount(i));
		pChunk->StoreByte(m_pDocument->GetPatternLength(i));

		if (m_pDocument->GetSongGroove(i))		// // //
			if (m_pDocument->GetGroove(m_pDocument->GetSongSpeed(i)) != NULL)
				pChunk->StoreByte(0);
			else pChunk->StoreByte(DEFAULT_SPEED);
		else pChunk->StoreByte(m_pDocument->GetSongSpeed(i));

		pChunk->StoreByte(m_pDocument->GetSongTempo(i));

		if (m_pDocument->GetSongGroove(i) && m_pDocument->GetGroove(m_pDocument->GetSongSpeed(i)) != NULL) {		// // //
			int Pos = 1;
			for (unsigned int j = 0; j < m_pDocument->GetSongSpeed(i); j++)
				if (m_pDocument->GetGroove(j) != NULL) Pos += m_pDocument->GetGroove(j)->GetSize() + 2;
			pChunk->StoreByte(Pos);
		}
		else pChunk->StoreByte(0);

		pChunk->StoreBankReference(label, 0);
	}

	m_iSongBankReference = m_vSongChunks[0]->GetLength() - 1;	// Save bank value position (all songs are equal)

	// Store actual songs
	for (int i = 0; i < TrackCount; ++i) {
		Print(" * Song %i:\n", i);
		// Store frames
		CreateFrameList(i);
		// Store pattern data
		StorePatterns(i);
	}

	if (m_iDuplicatePatterns > 0)
		Print(" * %i duplicated pattern(s) removed\n", m_iDuplicatePatterns);

	Print("      Hash collisions: %i (of %i items)\n", m_iHashCollisions, m_PatternMap.GetCount());
}

// Frames

void CCompiler::CreateFrameList(unsigned int Track)
{
	/*
	 * Creates a frame list
	 *
	 * The pointer list is just pointing to each item in the frame list
	 * and the frame list holds the offset addresses for the patterns for all channels
	 *
	 * ---------------------
	 *  Frame entry pointers
	 *  $XXXX (2 bytes, offset to a frame entry)
	 *  ...
	 * ---------------------
	 *
	 * ---------------------
	 *  Frame entries
	 *  $XXXX * 4 (2 * 2 bytes, each pair is an offset to the pattern)
	 * ---------------------
	 *
	 */
	const int FrameCount   = m_pDocument->GetFrameCount(Track);
	const int ChannelCount = m_pDocument->GetAvailableChannels();

	// Create frame list
	CStringA label;
	label.Format(CChunkRenderText::LABEL_SONG_FRAMES, Track);
	CChunk *pFrameListChunk = CreateChunk(CHUNK_FRAME_LIST, label);

	unsigned int TotalSize = 0;

	// Store addresses to patterns
	for (int i = 0; i < FrameCount; ++i) {
		// Add reference to frame list
		label.Format(CChunkRenderText::LABEL_SONG_FRAME, Track, i);
		pFrameListChunk->StoreReference(label);
		TotalSize += 2;

		// Store frame item
		CChunk *pChunk = CreateChunk(CHUNK_FRAME, label);
		m_vFrameChunks.push_back(pChunk);

		// Pattern pointers
		for (int j = 0; j < ChannelCount; ++j) {
			int Chan = m_vChanOrder[j];
			int Pattern = m_pDocument->GetPatternAtFrame(Track, i, Chan);
			label.Format(CChunkRenderText::LABEL_PATTERN, Track, Pattern, Chan);
			pChunk->StoreReference(label);
			TotalSize += 2;
		}
	}

	m_iTrackFrameSize[Track] = TotalSize;

	Print("      %i frames (%i bytes)\n", FrameCount, TotalSize);
}

// Patterns

void CCompiler::StorePatterns(unsigned int Track)
{
	/*
	 * Store patterns and save references to them for the frame list
	 *
	 */

	const int iChannels = m_pDocument->GetAvailableChannels();

	CPatternCompiler PatternCompiler(m_pDocument, m_iAssignedInstruments, (DPCM_List_t *)&m_iSamplesLookUp, m_pLogger);

	int PatternCount = 0;
	int PatternSize = 0;

	// Iterate through all patterns
	for (int i = 0; i < MAX_PATTERN; ++i) {
		for (int j = 0; j < iChannels; ++j) {
			// And store only used ones
			if (IsPatternAddressed(Track, i, j)) {

				// Compile pattern data
				PatternCompiler.CompileData(Track, i, j);

				CStringA label;
				label.Format(CChunkRenderText::LABEL_PATTERN, Track, i, j);

				bool StoreNew = true;

#ifdef REMOVE_DUPLICATE_PATTERNS
				unsigned int Hash = PatternCompiler.GetHash();

				// Check for duplicate patterns
				CChunk *pDuplicate = m_PatternMap[Hash];

				if (pDuplicate != NULL) {
					// Hash only indicates that patterns may be equal, check exact data
					if (PatternCompiler.CompareData(pDuplicate->GetStringData(PATTERN_CHUNK_INDEX))) {
						// Duplicate was found, store a reference to existing pattern
						m_DuplicateMap[label] = pDuplicate->GetLabel();
						++m_iDuplicatePatterns;
						StoreNew = false;
					}
				}
#endif /* REMOVE_DUPLICATE_PATTERNS */

				if (StoreNew) {
					// Store new pattern
					CChunk *pChunk = CreateChunk(CHUNK_PATTERN, label);
					m_vPatternChunks.push_back(pChunk);

#ifdef REMOVE_DUPLICATE_PATTERNS
					if (m_PatternMap[Hash] != NULL)
						m_iHashCollisions++;
					m_PatternMap[Hash] = pChunk;
#endif /* REMOVE_DUPLICATE_PATTERNS */

					// Store pattern data as string
					pChunk->StoreString(PatternCompiler.GetData());

					PatternSize += PatternCompiler.GetDataSize();
					++PatternCount;
				}
			}
		}
	}

#ifdef REMOVE_DUPLICATE_PATTERNS
	// Update references to duplicates
	for (const auto pChunk : m_vFrameChunks) {
		for (int j = 0, n = pChunk->GetLength(); j < n; ++j) {
			CStringA str = m_DuplicateMap[pChunk->GetDataRefName(j)];
			if (str.GetLength() != 0) {
				// Update reference
				pChunk->UpdateDataRefName(j, str);
			}
		}
	}
#endif /* REMOVE_DUPLICATE_PATTERNS */

#ifdef LOCAL_DUPLICATE_PATTERN_REMOVAL
	// Forget patterns when one whole track is stored
	m_PatternMap.RemoveAll();
	m_DuplicateMap.RemoveAll();
#endif /* LOCAL_DUPLICATE_PATTERN_REMOVAL */

	Print("      %i patterns (%i bytes)\n", PatternCount, PatternSize);
}

bool CCompiler::IsPatternAddressed(unsigned int Track, int Pattern, int Channel) const
{
	// Scan the frame list to see if a pattern is accessed for that frame
	const int FrameCount = m_pDocument->GetFrameCount(Track);

	for (int i = 0; i < FrameCount; ++i) {
		if (m_pDocument->GetPatternAtFrame(Track, i, Channel) == Pattern)
			return true;
	}

	return false;
}

void CCompiler::AddWavetable(CInstrumentFDS *pInstrument, CChunk *pChunk)
{
	// TODO Find equal existing waves
	/*
	for (int i = 0; i < m_iWaveTables; ++i) {
		if (!memcmp(Wave, m_iWaveTable[i], 64))
			return i;
	}
	*/

	// Allocate new wave
	for (int i = 0; i < 64; ++i)
		pChunk->StoreByte(pInstrument->GetSample(i));

	m_iWaveTables++;
}

// File writing

void CCompiler::WriteAssembly(CFilePtrArray &files, bool bExtraData, stNSFHeader Header, int MachineType,
	size_t OutputFileASMIndex,
	size_t FileNSFStubIndex,
	size_t FileNSFHeaderIndex,
	size_t FileNSFConfigIndex,
	size_t FilePeriodsIndex,
	size_t FileVibratoIndex,
	size_t FileMultiChipEnableIndex,
	size_t FileMultiChipUpdateIndex)
{
	// Dump all chunks and samples as assembly text
	CFile *pFile = files.at(OutputFileASMIndex).get();
	CChunkRenderText Render(pFile);

	// Write export comments
	// !! !! use CCompiler's document pointer instead of poking the main UI
	Render.WriteFileString(CStringA("; " APP_NAME " exported music data: "), pFile);
	Render.WriteFileString(m_pDocument->GetTitle(), pFile);
	Render.WriteFileString(CStringA("\n;\n\n"), pFile);
	Render.SetBankSwitching(m_bBankSwitched);
	Render.StoreChunks(m_vChunks);
	Print(" * Music data size: %i bytes\n", m_iMusicDataSize);
	// !! !! bank info must be included
	for (const auto pChunk : m_vChunks)
		if (pChunk->GetType() == CHUNK_SAMPLE_POINTERS)
			Render.StoreSamples(m_vSamples, pChunk);
	Print(" * DPCM samples size: %i bytes\n", m_iSamplesSize);

	if (bExtraData) {
		CFile *pFileNSFStub = files.at(FileNSFStubIndex).get();
		CFile *pFileNSFHeader = files.at(FileNSFHeaderIndex).get();
		CFile *pFileNSFConfig = files.at(FileNSFConfigIndex).get();
		CFile *pFilePeriods = files.at(FilePeriodsIndex).get();
		CFile *pFileVibrato = files.at(FileVibratoIndex).get();
		CFile *pFileMultiChipEnable = files.at(FileMultiChipEnableIndex).get();
		CFile *pFileMultiChipUpdate = files.at(FileMultiChipUpdateIndex).get();

		unsigned int LUTNTSC[NOTE_COUNT]{};
		unsigned int LUTPAL[NOTE_COUNT]{};
		unsigned int LUTSaw[NOTE_COUNT]{};
		unsigned int LUTVRC7[NOTE_RANGE]{};
		unsigned int LUTFDS[NOTE_COUNT]{};
		unsigned int LUTN163[NOTE_COUNT]{};
		unsigned int LUTVibrato[VIBRATO_LENGTH]{};

		ReadPeriodVibratoTables(MachineType, LUTNTSC, LUTPAL, LUTSaw, LUTVRC7, LUTFDS, LUTN163, LUTVibrato);

		Render.SetExtraDataFiles(pFileNSFStub, pFileNSFHeader, pFileNSFConfig, pFilePeriods, pFileVibrato, pFileMultiChipEnable, pFileMultiChipUpdate);
		Render.StoreNSFStub(Header.SoundChip, m_pDocument->GetVibratoStyle(), m_pDocument->GetLinearPitch(), m_iActualNamcoChannels, UseAllChips, true);
		Render.StoreNSFHeader(Header);
		Render.StoreNSFConfig(m_iSampleStart, Header);
		Render.StorePeriods(LUTNTSC, LUTPAL, LUTSaw, LUTVRC7, LUTFDS, LUTN163);
		Render.StoreVibrato(LUTVibrato);
		if (UseAllChips) {
			Render.StoreEnableExt(Header.SoundChip);
			Render.StoreUpdateExt(Header.SoundChip);
		}
	}
}

void CCompiler::WriteBinary(CFilePtrArray &files, bool bExtraData, stNSFHeader Header, int MachineType,
	size_t OutputFileBINIndex,
	size_t FileNSFStubIndex,
	size_t FileNSFHeaderIndex,
	size_t FileNSFConfigIndex,
	size_t FilePeriodsIndex,
	size_t FileVibratoIndex,
	size_t FileMultiChipEnableIndex,
	size_t FileMultiChipUpdateIndex)
{
	// Dump all chunks as binary
	CFile *pFile = files.at(OutputFileBINIndex).get();
	CChunkRenderBinary Render(pFile);
	if (!m_bBankSwitched)
		Render.StoreChunks(m_vChunks);
	else {
		theApp.DisplayMessage("Error: bankswitched BIN export not implemented yet!", 0, 0);
		Cleanup();
		return;
	}
	Print(" * Music data size: %i bytes\n", m_iMusicDataSize);

	if (bExtraData) {
		CFile *pFileNSFStub = files.at(FileNSFStubIndex).get();
		CFile *pFileNSFHeader = files.at(FileNSFHeaderIndex).get();
		CFile *pFileNSFConfig = files.at(FileNSFConfigIndex).get();
		CFile *pFilePeriods = files.at(FilePeriodsIndex).get();
		CFile *pFileVibrato = files.at(FileVibratoIndex).get();
		CFile *pFileMultiChipEnable = files.at(FileMultiChipEnableIndex).get();
		CFile *pFileMultiChipUpdate = files.at(FileMultiChipUpdateIndex).get();

		unsigned int LUTNTSC[NOTE_COUNT]{};
		unsigned int LUTPAL[NOTE_COUNT]{};
		unsigned int LUTSaw[NOTE_COUNT]{};
		unsigned int LUTVRC7[NOTE_RANGE]{};
		unsigned int LUTFDS[NOTE_COUNT]{};
		unsigned int LUTN163[NOTE_COUNT]{};
		unsigned int LUTVibrato[VIBRATO_LENGTH]{};

		ReadPeriodVibratoTables(MachineType, LUTNTSC, LUTPAL, LUTSaw, LUTVRC7, LUTFDS, LUTN163, LUTVibrato);

		// get an instance of CChunkRenderText to use its extra data plotting
		CChunkRenderText RenderText(nullptr);
		RenderText.SetExtraDataFiles(pFileNSFStub, pFileNSFHeader, pFileNSFConfig, pFilePeriods, pFileVibrato, pFileMultiChipEnable, pFileMultiChipUpdate);
		RenderText.StoreNSFStub(Header.SoundChip, m_pDocument->GetVibratoStyle(), m_pDocument->GetLinearPitch(), m_iActualNamcoChannels, UseAllChips);
		RenderText.StoreNSFHeader(Header);
		RenderText.StorePeriods(LUTNTSC, LUTPAL, LUTSaw, LUTVRC7, LUTFDS, LUTN163);
		RenderText.StoreVibrato(LUTVibrato);
		if (UseAllChips) {
			RenderText.StoreEnableExt(Header.SoundChip);
			RenderText.StoreUpdateExt(Header.SoundChip);
		}
	}
}

void CCompiler::WriteSamplesBinary(CFile *pFile)
{
	// Dump all samples as binary
	CChunkRenderBinary Render(pFile);
	Render.StoreSamples(m_vSamples);
	Print(" * DPCM samples size: %i bytes\n", m_iSamplesSize);
}

void CCompiler::ReadPeriodVibratoTables(int MachineType,
	unsigned int *LUTNTSC,
	unsigned int *LUTPAL,
	unsigned int *LUTSaw,
	unsigned int *LUTVRC7,
	unsigned int *LUTFDS,
	unsigned int *LUTN163,
	unsigned int *LUTVibrato) const
{
	const CSoundGen *pSoundGen = theApp.GetSoundGenerator();
	for (int i = 0; i <= CDetuneTable::DETUNE_N163; ++i) {		// // //
		switch (i) {
		case CDetuneTable::DETUNE_NTSC:
			for (int j = 0; j < NOTE_COUNT; ++j)
				LUTNTSC[j] = pSoundGen->ReadPeriodTable(j, i); break;
		case CDetuneTable::DETUNE_PAL:
			if (MachineType != 0)
				for (int j = 0; j < NOTE_COUNT; ++j)
					LUTPAL[j] = pSoundGen->ReadPeriodTable(j, i); break;
		case CDetuneTable::DETUNE_SAW:
			if (m_iActualChip & SNDCHIP_VRC6)
				for (int j = 0; j < NOTE_COUNT; ++j)
					LUTSaw[j] = pSoundGen->ReadPeriodTable(j, i); break;
		case CDetuneTable::DETUNE_VRC7:
			if (m_iActualChip & SNDCHIP_VRC7)
				for (int j = 0; j < NOTE_RANGE; ++j)
					LUTVRC7[j] = pSoundGen->ReadPeriodTable(j, i); break;
		case CDetuneTable::DETUNE_FDS:
			if (m_iActualChip & SNDCHIP_FDS)
				for (int j = 0; j < NOTE_COUNT; ++j)
					LUTFDS[j] = pSoundGen->ReadPeriodTable(j, i); break;
		case CDetuneTable::DETUNE_N163:
			if (m_iActualChip & SNDCHIP_N163)
				for (int j = 0; j < NOTE_COUNT; ++j)
					LUTN163[j] = pSoundGen->ReadPeriodTable(j, i); break;
		default:
			AfxDebugBreak();
		}
	}

	for (int i = 0; i < VIBRATO_LENGTH; ++i) {
		LUTVibrato[i] = pSoundGen->ReadVibratoTable(i);
	}
}

bool CCompiler::OpenArrayFile(CFilePtrArray &files, LPCTSTR filepath, std::string_view message)
{
	auto index = files.size();
	auto file = files.emplace_back(std::make_unique<CFile>()).get();

	if (!OpenFile(filepath, *file)) {
		CloseFileArray(files);
		Print(message);
		Cleanup();
		return false;
	}
	return true;
}

void CCompiler::CloseFileArray(CFilePtrArray &files)
{
	for (auto &file : files) {
		file->Close();
	}
}

// Object list functions

CChunk *CCompiler::CreateChunk(chunk_type_t Type, CStringA label)
{
	CChunk *pChunk = new CChunk(Type, label);
	m_vChunks.push_back(pChunk);
	return pChunk;
}

int CCompiler::CountData() const
{
	// Only count data
	int Offset = 0;

	for (const auto pChunk : m_vChunks)
		Offset += pChunk->CountDataSize();

	return Offset;
}

CChunk *CCompiler::GetObjectByRef(CStringA label) const
{
	for (const auto pChunk : m_vChunks)
		if (!label.Compare(pChunk->GetLabel()))
			return pChunk;
	return nullptr;
}

#if 0

void CCompiler::WriteChannelMap()
{
	CChunk *pChunk = CreateChunk(CHUNK_CHANNEL_MAP, "");

	pChunk->StoreByte(CHANID_2A03_SQUARE1 + 1);
	pChunk->StoreByte(CHANID_2A03_SQUARE2 + 1);
	pChunk->StoreByte(CHANID_2A03_TRIANGLE + 1);
	pChunk->StoreByte(CHANID_2A03_NOISE + 1);

	if (m_pDocument->ExpansionEnabled(SNDCHIP_VRC6)) {
		pChunk->StoreByte(CHANID_VRC6_PULSE1 + 1);
		pChunk->StoreByte(CHANID_VRC6_PULSE2 + 1);
		pChunk->StoreByte(CHANID_VRC6_SAWTOOTH + 1);
	}

	if (m_pDocument->ExpansionEnabled(SNDCHIP_VRC7)) {
		pChunk->StoreByte(CHANID_VRC7_CH1 + 1);
		pChunk->StoreByte(CHANID_VRC7_CH2 + 1);
		pChunk->StoreByte(CHANID_VRC7_CH3 + 1);
		pChunk->StoreByte(CHANID_VRC7_CH4 + 1);
		pChunk->StoreByte(CHANID_VRC7_CH5 + 1);
		pChunk->StoreByte(CHANID_VRC7_CH6 + 1);
	}

	if (m_pDocument->ExpansionEnabled(SNDCHIP_FDS)) {
		pChunk->StoreByte(CHANID_FDS + 1);
	}

	if (m_pDocument->ExpansionEnabled(SNDCHIP_MMC5)) {
		pChunk->StoreByte(CHANID_MMC5_SQUARE1 + 1);
		pChunk->StoreByte(CHANID_MMC5_SQUARE2 + 1);
	}

	if (m_pDocument->ExpansionEnabled(SNDCHIP_N163)) {
		for (unsigned int i = 0; i < m_pDocument->GetNamcoChannels(); ++i) {
			pChunk->StoreByte(CHANID_N163_CH1 + i + 1);
		}
	}

	pChunk->StoreByte(CHANID_2A03_DPCM + 1);
}

void CCompiler::WriteChannelTypes()
{
	const int TYPE_2A03 = 0;
	const int TYPE_VRC6 = 2;
	const int TYPE_VRC7 = 4;
	const int TYPE_FDS	= 6;
	const int TYPE_MMC5 = 8;
	const int TYPE_N163 = 10;
	const int TYPE_S5B	= 12;

	CChunk *pChunk = CreateChunk(CHUNK_CHANNEL_TYPES, "");

	for (int i = 0; i < 4; ++i)
		pChunk->StoreByte(TYPE_2A03);

	if (m_pDocument->ExpansionEnabled(SNDCHIP_VRC6)) {
		for (int i = 0; i < 3; ++i)
			pChunk->StoreByte(TYPE_VRC6);
	}

	if (m_pDocument->ExpansionEnabled(SNDCHIP_VRC7)) {
		for (int i = 0; i < 3; ++i)
			pChunk->StoreByte(TYPE_VRC7);
	}

	if (m_pDocument->ExpansionEnabled(SNDCHIP_FDS)) {
		pChunk->StoreByte(TYPE_FDS);
	}

	if (m_pDocument->ExpansionEnabled(SNDCHIP_MMC5)) {
		for (int i = 0; i < 2; ++i)
			pChunk->StoreByte(TYPE_MMC5);
	}

	if (m_pDocument->ExpansionEnabled(SNDCHIP_N163)) {
		for (unsigned int i = 0; i < m_pDocument->GetNamcoChannels(); ++i)
			pChunk->StoreByte(TYPE_N163);
	}

	pChunk->StoreByte(TYPE_2A03);
}

#endif
