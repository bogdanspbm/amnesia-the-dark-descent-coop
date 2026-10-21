// Cuts the co-op recordings into one video with Media Foundation: title cards drawn with GDI, clips cut from the
// scene captures, fades through black between them. No extra software needed.
//
//   montage.exe <edit list> <out.mp4>
//
// Edit list, one entry per line ('#' starts a comment):
//   title <seconds> <big line>|<small line>
//   clip <file.mp4> <from seconds> <to seconds>
//   voice <seconds or mm:ss.s of the result> <file.wav>   (16 bit PCM; the game sound ducks under it)
// Every 10 seconds of the result a still goes to <out>_frames\NNNN.bmp for a contact sheet.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shlwapi.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

static const UINT32 kWidth = 1920, kHeight = 1080, kFps = 30;
static const UINT32 kVideoBitrate = 16000000;
static const double kVideoFade = 0.5;	// seconds to and from black at each cut
static const double kAudioFade = 0.25;	// short fades so the sound crosses the cut without a click
static const LONGLONG kHns = 10000000;
static const DWORD kFrameBytes = kWidth * kHeight * 3 / 2;	// NV12, what the decoder gives and the encoder takes

#define CHECK(x) do { HRESULT hr_ = (x); if(FAILED(hr_)) { fprintf(stderr, "%s failed: 0x%08lx (line %d)\n", #x, hr_, __LINE__); exit(1); } } while(0)

template<class T> static void Release(T*& p) { if(p) { p->Release(); p = NULL; } }

struct Entry
{
	bool mbTitle;
	bool mbVoice;			// voice-over: msFile (a wav) starts at mfFrom seconds of the result
	double mfSeconds;		// titles
	std::wstring msBig, msSmall;
	std::wstring msFile;	// clips
	double mfFrom, mfTo;
};

static std::wstring Widen(const std::string& s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring w(n, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

//BT.709, limited range, like the H.264 the game records; chroma from the top-left pixel of each 2x2 block
static void BgraToNv12(const BYTE* apBgra, std::vector<BYTE>& avOut)
{
	avOut.assign(kFrameBytes, 0);
	for(UINT32 y = 0; y < kHeight; ++y)
		for(UINT32 x = 0; x < kWidth; ++x)
		{
			const BYTE* p = apBgra + (y * kWidth + x) * 4;
			int b = p[0], g = p[1], r = p[2];
			avOut[y * kWidth + x] = (BYTE)(16 + ((47 * r + 157 * g + 16 * b) >> 8));
			if((x & 1) == 0 && (y & 1) == 0)
			{
				BYTE* pUV = &avOut[kWidth * kHeight + (y / 2) * kWidth + x];
				pUV[0] = (BYTE)(128 + ((-26 * r - 87 * g + 112 * b) >> 8));
				pUV[1] = (BYTE)(128 + ((112 * r - 102 * g - 10 * b) >> 8));
			}
		}
}

//-----------------------------------------------------------------------

class cMontage
{
public:
	cMontage() : mpWriter(NULL), mlVideoStream(0), mlAudioStream(0), mlFrame(0), mlAudioFrames(0), mlRate(48000), mlNextStill(0), mlPcmPos(0), mlSegFrame(0) {}

	void Run(const std::vector<Entry>& avEntries, const std::wstring& asOut);

private:
	void OpenWriter(const std::wstring& asOut);
	void WriteFrame(const std::vector<BYTE>& avFrame, double afFade);
	void WriteAudio(const short* apPcm, size_t alCount);
	void BeginSegment(std::vector<short>& avPcm);
	void Title(const Entry& e);
	void Clip(const Entry& e);
	void SaveStill(const std::vector<BYTE>& avFrame);
	UINT32 ProbeRate(const std::wstring& asFile);
	void AddVoice(const Entry& e);
	void BuildDuck();

	IMFSinkWriter* mpWriter;
	DWORD mlVideoStream, mlAudioStream;
	LONGLONG mlFrame, mlAudioFrames;
	UINT32 mlRate;
	std::wstring msStills;
	LONGLONG mlNextStill;
	std::vector<short> mvPcm;	// sound of the segment being written, stereo
	std::vector<float> mvVoice;	// voice-over over the whole result, mono, at mlRate
	std::vector<float> mvDuck;	// gain for the game sound at each frame of mvVoice
	std::vector<std::pair<size_t, size_t> > mvVoiceSpans;	// frames where someone talks
	size_t mlPcmPos;
	LONGLONG mlSegFrame;
};

UINT32 cMontage::ProbeRate(const std::wstring& asFile)
{
	IMFSourceReader* pReader = NULL;
	CHECK(MFCreateSourceReaderFromURL(asFile.c_str(), NULL, &pReader));
	IMFMediaType* pType = NULL;
	UINT32 lRate = 48000;
	if(SUCCEEDED(pReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &pType)))
		lRate = MFGetAttributeUINT32(pType, MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
	Release(pType);
	Release(pReader);
	return lRate;
}

void cMontage::OpenWriter(const std::wstring& asOut)
{
	IMFAttributes* pAttr = NULL;
	CHECK(MFCreateAttributes(&pAttr, 1));
	pAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
	DeleteFileW(asOut.c_str());
	CHECK(MFCreateSinkWriterFromURL(asOut.c_str(), NULL, pAttr, &mpWriter));
	Release(pAttr);

	IMFMediaType* pOut = NULL;
	CHECK(MFCreateMediaType(&pOut));
	pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	pOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	pOut->SetUINT32(MF_MT_AVG_BITRATE, kVideoBitrate);
	pOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	MFSetAttributeSize(pOut, MF_MT_FRAME_SIZE, kWidth, kHeight);
	MFSetAttributeRatio(pOut, MF_MT_FRAME_RATE, kFps, 1);
	MFSetAttributeRatio(pOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	CHECK(mpWriter->AddStream(pOut, &mlVideoStream));
	Release(pOut);

	IMFMediaType* pIn = NULL;
	CHECK(MFCreateMediaType(&pIn));
	pIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	pIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	pIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	MFSetAttributeSize(pIn, MF_MT_FRAME_SIZE, kWidth, kHeight);
	MFSetAttributeRatio(pIn, MF_MT_FRAME_RATE, kFps, 1);
	MFSetAttributeRatio(pIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	pIn->SetUINT32(MF_MT_DEFAULT_STRIDE, kWidth);
	CHECK(mpWriter->SetInputMediaType(mlVideoStream, pIn, NULL));
	Release(pIn);

	CHECK(MFCreateMediaType(&pOut));
	pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	pOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
	pOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	pOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, mlRate);
	pOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
	pOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);	// 192 kbit/s
	CHECK(mpWriter->AddStream(pOut, &mlAudioStream));
	Release(pOut);

	CHECK(MFCreateMediaType(&pIn));
	pIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	pIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	pIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	pIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, mlRate);
	pIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
	pIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
	pIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, mlRate * 4);
	CHECK(mpWriter->SetInputMediaType(mlAudioStream, pIn, NULL));
	Release(pIn);

	CHECK(mpWriter->BeginWriting());
}

void cMontage::WriteFrame(const std::vector<BYTE>& avFrame, double afFade)
{
	const DWORD lSize = kFrameBytes;
	IMFMediaBuffer* pBuffer = NULL;
	CHECK(MFCreateMemoryBuffer(lSize, &pBuffer));
	BYTE* pData = NULL;
	CHECK(pBuffer->Lock(&pData, NULL, NULL));
	if(afFade >= 0.999)
		memcpy(pData, &avFrame[0], lSize);
	else
	{
		//To black: luma towards 16, chroma towards neutral 128
		int lMul = (int)(afFade * 256);
		const DWORD lLuma = kWidth * kHeight;
		for(DWORD i = 0; i < lLuma; ++i) pData[i] = (BYTE)(16 + (((avFrame[i] - 16) * lMul) >> 8));
		for(DWORD i = lLuma; i < lSize; ++i) pData[i] = (BYTE)(128 + (((avFrame[i] - 128) * lMul) >> 8));
	}
	pBuffer->Unlock();
	pBuffer->SetCurrentLength(lSize);

	IMFSample* pSample = NULL;
	CHECK(MFCreateSample(&pSample));
	pSample->AddBuffer(pBuffer);
	pSample->SetSampleTime(mlFrame * kHns / kFps);
	pSample->SetSampleDuration(kHns / kFps);
	CHECK(mpWriter->WriteSample(mlVideoStream, pSample));
	Release(pSample);
	Release(pBuffer);

	if(mlFrame >= mlNextStill)
	{
		SaveStill(avFrame);
		mlNextStill += 10 * kFps;
	}
	++mlFrame;

	//The segment's sound goes along with the pictures; the writer interleaves them and would otherwise hold
	//every raw frame in memory until the sound for it arrives
	++mlSegFrame;
	size_t lWant = min((size_t)(mlSegFrame * mlRate / kFps) * 2, mvPcm.size());
	if(lWant > mlPcmPos)
	{
		WriteAudio(&mvPcm[mlPcmPos], lWant - mlPcmPos);
		mlPcmPos = lWant;
	}
}

void cMontage::BeginSegment(std::vector<short>& avPcm)
{
	mvPcm.swap(avPcm);
	mlPcmPos = 0;
	mlSegFrame = 0;
}

void cMontage::WriteAudio(const short* apPcm, size_t alCount)
{
	const size_t lChunk = mlRate / 10 * 2;	// 0.1 s of stereo
	for(size_t lPos = 0; lPos < alCount; lPos += lChunk)
	{
		size_t lCount = min(lChunk, alCount - lPos);
		DWORD lBytes = (DWORD)(lCount * sizeof(short));
		IMFMediaBuffer* pBuffer = NULL;
		CHECK(MFCreateMemoryBuffer(lBytes, &pBuffer));
		BYTE* pData = NULL;
		CHECK(pBuffer->Lock(&pData, NULL, NULL));
		//Voice-over mixed in by position in the result, the game sound turned down under it
		short* pOut = (short*)pData;
		for(size_t i = 0; i < lCount; ++i)
		{
			size_t lFrame = (size_t)mlAudioFrames + i / 2;
			float fGame = apPcm[lPos + i];
			float fMix = fGame;
			if(lFrame < mvDuck.size())
				fMix = fGame * mvDuck[lFrame] + mvVoice[lFrame];
			pOut[i] = (short)max(-32768.0f, min(32767.0f, fMix));
		}
		pBuffer->Unlock();
		pBuffer->SetCurrentLength(lBytes);

		IMFSample* pSample = NULL;
		CHECK(MFCreateSample(&pSample));
		pSample->AddBuffer(pBuffer);
		LONGLONG lFrames = lCount / 2;
		pSample->SetSampleTime(mlAudioFrames * kHns / mlRate);
		pSample->SetSampleDuration(lFrames * kHns / mlRate);
		CHECK(mpWriter->WriteSample(mlAudioStream, pSample));
		Release(pSample);
		Release(pBuffer);
		mlAudioFrames += lFrames;
	}
}

static void ApplyAudioFades(std::vector<short>& avPcm, UINT32 alRate)
{
	size_t lFrames = avPcm.size() / 2;
	size_t lFade = min((size_t)(kAudioFade * alRate), lFrames / 2);
	for(size_t i = 0; i < lFade; ++i)
	{
		float f = (float)i / lFade;
		for(int c = 0; c < 2; ++c)
		{
			avPcm[i*2+c] = (short)(avPcm[i*2+c] * f);
			avPcm[(lFrames-1-i)*2+c] = (short)(avPcm[(lFrames-1-i)*2+c] * f);
		}
	}
}

static double VideoFade(double afT, double afLength)
{
	double f = min(afT / kVideoFade, (afLength - afT) / kVideoFade);
	return f < 0 ? 0 : (f > 1 ? 1 : f);
}

void cMontage::Title(const Entry& e)
{
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth = kWidth;
	bmi.bmiHeader.biHeight = -(LONG)kHeight;	// top-down, like the writer's input
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	void* pBits = NULL;
	HDC hDC = CreateCompatibleDC(NULL);
	HBITMAP hBmp = CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
	HGDIOBJ hOld = SelectObject(hDC, hBmp);
	memset(pBits, 0, kWidth * kHeight * 4);

	SetBkMode(hDC, TRANSPARENT);
	HFONT hBig = CreateFontW(84, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Georgia");
	HFONT hSmall = CreateFontW(40, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Georgia");
	RECT rBig = { 0, 0, (LONG)kWidth, (LONG)kHeight / 2 + 30 };
	RECT rSmall = { 0, (LONG)kHeight / 2 + 50, (LONG)kWidth, (LONG)kHeight / 2 + 120 };
	SelectObject(hDC, hBig);
	SetTextColor(hDC, RGB(235, 230, 220));
	DrawTextW(hDC, e.msBig.c_str(), -1, &rBig, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
	SelectObject(hDC, hSmall);
	SetTextColor(hDC, RGB(160, 150, 135));
	DrawTextW(hDC, e.msSmall.c_str(), -1, &rSmall, DT_CENTER | DT_TOP | DT_SINGLELINE);
	GdiFlush();

	std::vector<BYTE> vFrame;
	BgraToNv12((const BYTE*)pBits, vFrame);
	SelectObject(hDC, hOld);
	DeleteObject(hBig);
	DeleteObject(hSmall);
	DeleteObject(hBmp);
	DeleteDC(hDC);

	LONGLONG lFrames = (LONGLONG)(e.mfSeconds * kFps + 0.5);
	std::vector<short> vSilence((size_t)(lFrames * mlRate / kFps) * 2, 0);
	BeginSegment(vSilence);
	for(LONGLONG i = 0; i < lFrames; ++i)
		WriteFrame(vFrame, VideoFade((i + 0.5) / kFps, e.mfSeconds));
}

void cMontage::Clip(const Entry& e)
{
	double fLength = e.mfTo - e.mfFrom;
	LONGLONG lFrames = (LONGLONG)(fLength * kFps + 0.5);
	LONGLONG lFrom = (LONGLONG)(e.mfFrom * kHns);

	//Sound: decoded to 16 bit PCM and put where its time stamps say
	{
		std::vector<short> vPcm((size_t)(lFrames * mlRate / kFps) * 2, 0);
		IMFSourceReader* pReader = NULL;
		CHECK(MFCreateSourceReaderFromURL(e.msFile.c_str(), NULL, &pReader));
		pReader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
		if(SUCCEEDED(pReader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE)))
		{
			IMFMediaType* pType = NULL;
			CHECK(MFCreateMediaType(&pType));
			pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
			pType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
			pType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
			CHECK(pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pType));
			Release(pType);
			CHECK(pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pType));
			UINT32 lChannels = MFGetAttributeUINT32(pType, MF_MT_AUDIO_NUM_CHANNELS, 2);
			UINT32 lRate = MFGetAttributeUINT32(pType, MF_MT_AUDIO_SAMPLES_PER_SECOND, mlRate);
			Release(pType);
			if(lRate != mlRate) fwprintf(stderr, L"warning: %s has sound at %u Hz, the video %u Hz\n", e.msFile.c_str(), lRate, mlRate);

			PROPVARIANT var;
			PropVariantInit(&var);
			var.vt = VT_I8;
			var.hVal.QuadPart = lFrom;
			CHECK(pReader->SetCurrentPosition(GUID_NULL, var));
			LONGLONG lTo = (LONGLONG)(e.mfTo * kHns);
			for(;;)
			{
				DWORD lFlags = 0;
				LONGLONG lTime = 0;
				IMFSample* pSample = NULL;
				CHECK(pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &lFlags, &lTime, &pSample));
				if(lFlags & MF_SOURCE_READERF_ENDOFSTREAM) { Release(pSample); break; }
				if(pSample == NULL) continue;
				if(lTime > lTo) { Release(pSample); break; }
				IMFMediaBuffer* pBuffer = NULL;
				CHECK(pSample->ConvertToContiguousBuffer(&pBuffer));
				BYTE* pData = NULL;
				DWORD lBytes = 0;
				CHECK(pBuffer->Lock(&pData, NULL, &lBytes));
				const short* pIn = (const short*)pData;
				LONGLONG lCount = lBytes / (2 * lChannels);
				LONGLONG lStart = (lTime - lFrom) * mlRate / kHns;
				for(LONGLONG s = 0; s < lCount; ++s)
				{
					LONGLONG lDst = lStart + s;
					if(lDst < 0 || lDst * 2 + 1 >= (LONGLONG)vPcm.size()) continue;
					vPcm[lDst*2] = pIn[s * lChannels];
					vPcm[lDst*2+1] = pIn[s * lChannels + (lChannels > 1 ? 1 : 0)];
				}
				pBuffer->Unlock();
				Release(pBuffer);
				Release(pSample);
			}
		}
		Release(pReader);
		ApplyAudioFades(vPcm, mlRate);
		BeginSegment(vPcm);
	}

	//Video: NV12 straight from the decoder, the latest frame at or before each output time is used
	{
		IMFSourceReader* pReader = NULL;
		CHECK(MFCreateSourceReaderFromURL(e.msFile.c_str(), NULL, &pReader));
		pReader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
		pReader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
		IMFMediaType* pType = NULL;
		CHECK(MFCreateMediaType(&pType));
		pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		CHECK(pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType));
		Release(pType);
		CHECK(pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType));
		UINT32 lW = 0, lH = 0;
		MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &lW, &lH);
		LONG lPitch = (LONG)MFGetAttributeUINT32(pType, MF_MT_DEFAULT_STRIDE, lW);
		if(lPitch < 0) lPitch = -lPitch;
		Release(pType);
		if(lW < kWidth || lH < kHeight) { fwprintf(stderr, L"%s is %ux%u, need %ux%u\n", e.msFile.c_str(), lW, lH, kWidth, kHeight); exit(1); }

		PROPVARIANT var;
		PropVariantInit(&var);
		var.vt = VT_I8;
		var.hVal.QuadPart = lFrom;
		CHECK(pReader->SetCurrentPosition(GUID_NULL, var));

		std::vector<BYTE> vCur(kFrameBytes, 0), vNext(kFrameBytes, 0);
		LONGLONG lNextTime = -1;
		bool bEnd = false;
		auto ReadNext = [&]() -> bool
		{
			for(;;)
			{
				DWORD lFlags = 0;
				LONGLONG lTime = 0;
				IMFSample* pSample = NULL;
				CHECK(pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, &lFlags, &lTime, &pSample));
				if(lFlags & MF_SOURCE_READERF_ENDOFSTREAM) { Release(pSample); return false; }
				if(pSample == NULL) continue;
				IMFMediaBuffer* pBuffer = NULL;
				CHECK(pSample->ConvertToContiguousBuffer(&pBuffer));
				BYTE* pData = NULL;
				DWORD lBytes = 0;
				CHECK(pBuffer->Lock(&pData, NULL, &lBytes));
				//Luma rows, then the interleaved chroma plane that starts after all coded rows (1088 for 1080,
				//whatever the media type says), so the row count comes from the buffer size
				UINT32 lRows = (UINT32)(lBytes * 2 / 3 / lPitch);
				for(UINT32 y = 0; y < kHeight; ++y)
					memcpy(&vNext[y * kWidth], pData + y * lPitch, kWidth);
				const BYTE* pUV = pData + lRows * lPitch;
				for(UINT32 y = 0; y < kHeight / 2; ++y)
					memcpy(&vNext[kWidth * kHeight + y * kWidth], pUV + y * lPitch, kWidth);
				pBuffer->Unlock();
				Release(pBuffer);
				Release(pSample);
				lNextTime = lTime;
				return true;
			}
		};

		bEnd = !ReadNext();
		vCur = vNext;
		for(LONGLONG i = 0; i < lFrames; ++i)
		{
			LONGLONG lWant = lFrom + i * kHns / kFps;
			while(!bEnd && lNextTime <= lWant)
			{
				vCur.swap(vNext);
				bEnd = !ReadNext();
			}
			WriteFrame(vCur, VideoFade((i + 0.5) / kFps, fLength));
		}
		Release(pReader);
	}

	wprintf(L"  %s %.1f-%.1f s\n", PathFindFileNameW(e.msFile.c_str()), e.mfFrom, e.mfTo);
}

void cMontage::SaveStill(const std::vector<BYTE>& avFrame)
{
	wchar_t sName[MAX_PATH];
	swprintf_s(sName, L"%s\\%04lld.bmp", msStills.c_str(), mlFrame / kFps);
	BITMAPFILEHEADER bf = {};
	BITMAPINFOHEADER bi = {};
	bi.biSize = sizeof(bi);
	bi.biWidth = kWidth;
	bi.biHeight = -(LONG)kHeight;
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	DWORD lSize = kWidth * kHeight * 4;
	bf.bfType = 0x4D42;
	bf.bfOffBits = sizeof(bf) + sizeof(bi);
	bf.bfSize = bf.bfOffBits + lSize;

	std::vector<BYTE> vBgra(lSize);
	for(UINT32 y = 0; y < kHeight; ++y)
		for(UINT32 x = 0; x < kWidth; ++x)
		{
			int Y = avFrame[y * kWidth + x] - 16;
			const BYTE* pUV = &avFrame[kWidth * kHeight + (y / 2) * kWidth + (x & ~1)];
			int U = pUV[0] - 128, V = pUV[1] - 128;
			int r = (298 * Y + 459 * V + 128) >> 8;
			int g = (298 * Y - 55 * U - 136 * V + 128) >> 8;
			int b = (298 * Y + 541 * U + 128) >> 8;
			BYTE* p = &vBgra[(y * kWidth + x) * 4];
			p[0] = (BYTE)max(0, min(255, b));
			p[1] = (BYTE)max(0, min(255, g));
			p[2] = (BYTE)max(0, min(255, r));
			p[3] = 255;
		}

	FILE* pFile = NULL;
	if(_wfopen_s(&pFile, sName, L"wb") != 0) return;
	fwrite(&bf, sizeof(bf), 1, pFile);
	fwrite(&bi, sizeof(bi), 1, pFile);
	fwrite(&vBgra[0], lSize, 1, pFile);
	fclose(pFile);
}

static const float kDuckGain = 0.3f;	// game sound under the voice
static const double kDuckRamp = 0.2;	// seconds to go down and back up

//A wav as TTS services give it: 16 bit PCM, any rate and channel count. The data size may be left unset
//(0 or 0xFFFFFFFF) when it was streamed, then the data runs to the end of the file.
static bool LoadWav(const std::wstring& asFile, std::vector<float>& avMono, UINT32& alRate)
{
	FILE* pFile = NULL;
	if(_wfopen_s(&pFile, asFile.c_str(), L"rb") != 0) return false;
	std::vector<BYTE> vData;
	fseek(pFile, 0, SEEK_END);
	vData.resize(ftell(pFile));
	fseek(pFile, 0, SEEK_SET);
	fread(vData.data(), 1, vData.size(), pFile);
	fclose(pFile);
	if(vData.size() < 12 || memcmp(vData.data(), "RIFF", 4) != 0 || memcmp(vData.data() + 8, "WAVE", 4) != 0) return false;

	UINT16 lFormat = 0, lChannels = 0, lBits = 0;
	size_t lPos = 12;
	while(lPos + 8 <= vData.size())
	{
		UINT32 lSize = *(const UINT32*)&vData[lPos + 4];
		const BYTE* pChunk = &vData[lPos + 8];
		if(memcmp(&vData[lPos], "fmt ", 4) == 0)
		{
			lFormat = *(const UINT16*)pChunk;
			lChannels = *(const UINT16*)(pChunk + 2);
			alRate = *(const UINT32*)(pChunk + 4);
			lBits = *(const UINT16*)(pChunk + 14);
		}
		else if(memcmp(&vData[lPos], "data", 4) == 0)
		{
			if(lFormat != 1 || lBits != 16 || lChannels == 0) return false;
			size_t lAvail = vData.size() - (lPos + 8);
			size_t lBytes = (lSize == 0 || lSize > lAvail) ? lAvail : lSize;
			size_t lFrames = lBytes / (2 * lChannels);
			const short* pIn = (const short*)pChunk;
			avMono.resize(lFrames);
			for(size_t i = 0; i < lFrames; ++i)
			{
				float f = 0;
				for(UINT16 c = 0; c < lChannels; ++c) f += pIn[i * lChannels + c];
				avMono[i] = f / lChannels;
			}
			return true;
		}
		lPos += 8 + lSize + (lSize & 1);
	}
	return false;
}

void cMontage::AddVoice(const Entry& e)
{
	std::vector<float> vIn;
	UINT32 lInRate = 0;
	if(!LoadWav(e.msFile, vIn, lInRate) || vIn.empty()) { fwprintf(stderr, L"cannot read %s (16 bit PCM wav)\n", e.msFile.c_str()); exit(1); }

	//Brought to a common loudness (peak at about -2 dB), then resampled linearly to the video's rate
	float fPeak = 1;
	for(size_t i = 0; i < vIn.size(); ++i) fPeak = max(fPeak, fabsf(vIn[i]));
	float fGain = 26000.0f / fPeak;
	size_t lOutFrames = (size_t)((double)vIn.size() * mlRate / lInRate);
	size_t lStart = (size_t)(e.mfFrom * mlRate);
	if(mvVoice.size() < lStart + lOutFrames) mvVoice.resize(lStart + lOutFrames, 0);
	for(size_t i = 0; i < lOutFrames; ++i)
	{
		double fSrc = (double)i * lInRate / mlRate;
		size_t j = (size_t)fSrc;
		float t = (float)(fSrc - j);
		float a = vIn[min(j, vIn.size() - 1)], b = vIn[min(j + 1, vIn.size() - 1)];
		mvVoice[lStart + i] += (a + (b - a) * t) * fGain;
	}
	mvVoiceSpans.push_back(std::make_pair(lStart, lStart + lOutFrames));
	wprintf(L"  voice %s at %.1f s, %.1f s long\n", PathFindFileNameW(e.msFile.c_str()), e.mfFrom, (double)lOutFrames / mlRate);
}

void cMontage::BuildDuck()
{
	size_t lRamp = (size_t)(kDuckRamp * mlRate);
	mvVoice.resize(mvVoice.size() + lRamp, 0);
	mvDuck.assign(mvVoice.size(), 1.0f);
	for(size_t s = 0; s < mvVoiceSpans.size(); ++s)
	{
		size_t a = mvVoiceSpans[s].first, b = mvVoiceSpans[s].second;
		size_t lFrom = a > lRamp ? a - lRamp : 0;
		for(size_t f = lFrom; f < min(b + lRamp, mvDuck.size()); ++f)
		{
			size_t lDist = f < a ? a - f : (f > b ? f - b : 0);
			float fGain = kDuckGain + (1 - kDuckGain) * (float)lDist / lRamp;
			mvDuck[f] = min(mvDuck[f], fGain);
		}
	}
}

void cMontage::Run(const std::vector<Entry>& avEntries, const std::wstring& asOut)
{
	for(size_t i = 0; i < avEntries.size(); ++i)
		if(!avEntries[i].mbTitle && !avEntries[i].mbVoice) { mlRate = ProbeRate(avEntries[i].msFile); break; }

	for(size_t i = 0; i < avEntries.size(); ++i)
		if(avEntries[i].mbVoice) AddVoice(avEntries[i]);
	if(!mvVoiceSpans.empty()) BuildDuck();

	msStills = asOut.substr(0, asOut.find_last_of(L'.')) + L"_frames";
	CreateDirectoryW(msStills.c_str(), NULL);

	OpenWriter(asOut);
	for(size_t i = 0; i < avEntries.size(); ++i)
	{
		if(avEntries[i].mbVoice) continue;
		if(avEntries[i].mbTitle) Title(avEntries[i]);
		else Clip(avEntries[i]);
	}
	CHECK(mpWriter->Finalize());
	Release(mpWriter);
	wprintf(L"done: %s, %.1f s\n", asOut.c_str(), (double)mlFrame / kFps);
}

//-----------------------------------------------------------------------

int wmain(int argc, wchar_t** argv)
{
	if(argc < 3) { fprintf(stderr, "montage <edit list> <out.mp4>\n"); return 1; }

	std::ifstream file(argv[1]);
	if(!file) { fwprintf(stderr, L"cannot read %s\n", argv[1]); return 1; }
	std::wstring sBase = argv[1];
	sBase = sBase.substr(0, sBase.find_last_of(L"\\/") + 1);

	std::vector<Entry> vEntries;
	std::string sLine;
	while(std::getline(file, sLine))
	{
		if(sLine.size() >= 3 && (BYTE)sLine[0] == 0xEF) sLine = sLine.substr(3);	// BOM
		if(!sLine.empty() && sLine.back() == '\r') sLine.pop_back();
		if(sLine.empty() || sLine[0] == '#') continue;
		std::istringstream ss(sLine);
		std::string sKind;
		ss >> sKind;
		Entry e = {};
		if(sKind == "title")
		{
			e.mbTitle = true;
			ss >> e.mfSeconds;
			std::string sRest;
			std::getline(ss, sRest);
			sRest.erase(0, sRest.find_first_not_of(' '));
			size_t lBar = sRest.find('|');
			e.msBig = Widen(sRest.substr(0, lBar));
			if(lBar != std::string::npos) e.msSmall = Widen(sRest.substr(lBar + 1));
		}
		else if(sKind == "clip")
		{
			std::string sFile;
			ss >> sFile >> e.mfFrom >> e.mfTo;
			std::wstring w = Widen(sFile);
			e.msFile = PathIsRelativeW(w.c_str()) ? sBase + w : w;
			if(e.mfTo <= e.mfFrom) { fprintf(stderr, "bad clip line: %s\n", sLine.c_str()); return 1; }
		}
		else if(sKind == "voice")
		{
			e.mbVoice = true;
			std::string sTime, sFile;
			ss >> sTime >> sFile;
			size_t lColon = sTime.find(':');
			e.mfFrom = lColon == std::string::npos ? atof(sTime.c_str())
				: atof(sTime.substr(0, lColon).c_str()) * 60 + atof(sTime.substr(lColon + 1).c_str());
			std::wstring w = Widen(sFile);
			e.msFile = PathIsRelativeW(w.c_str()) ? sBase + w : w;
		}
		else { fprintf(stderr, "unknown line: %s\n", sLine.c_str()); return 1; }
		vEntries.push_back(e);
	}

	CHECK(CoInitializeEx(NULL, COINIT_MULTITHREADED));
	CHECK(MFStartup(MF_VERSION));
	cMontage montage;
	montage.Run(vEntries, argv[2]);
	MFShutdown();
	CoUninitialize();
	return 0;
}
