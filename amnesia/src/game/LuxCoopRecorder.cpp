/*
 * Copyright © 2011-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "LuxCoopRecorder.h"

#if defined(WIN32)

#include <windows.h>
#include <GL/gl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <process.h>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "opengl32.lib")

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

//////////////////////////////////////////////////////////////////////////
// CONSTANTS
//////////////////////////////////////////////////////////////////////////

static const UINT32 kVideoBitrate = 16000000;		//1080p30 for YouTube
static const UINT32 kAudioBytesPerSec = 24000;		//192 kbit/s AAC
static const LONGLONG kHns = 10000000;				//Media Foundation time unit is 100 ns

//////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION
//////////////////////////////////////////////////////////////////////////

class cLuxCoopRecorderImpl
{
public:
	cLuxCoopRecorderImpl() : mpWriter(NULL), mbRecording(false), mhAudioThread(NULL), mbAudioStop(false), mbMFStarted(false),
							 mpAudioClient(NULL), mpCapture(NULL), mpMixFormat(NULL), mbHasAudio(false), mfStillEvery(0), mlNextStill(0),
							 mpSessionVolume(NULL), mbSessionMute(FALSE), mfSessionVolume(1)
	{
		InitializeCriticalSection(&mWriterLock);
	}
	~cLuxCoopRecorderImpl()
	{
		Stop();
		DeleteCriticalSection(&mWriterLock);
	}

	bool Start(const tString& asFile, int alWidth, int alHeight, int alFps);
	void Stop();
	void CaptureFrame();
	bool IsRecording(){ return mbRecording; }
	void SetStills(float afEvery){ mfStillEvery = afEvery; }

private:
	void WriteStill(LONGLONG alTime);
	bool InitAudioCapture();
	void UnmuteOwnSound();
	void RestoreOwnSound();
	static unsigned __stdcall AudioThreadFunc(void *apThis);
	void AudioLoop();
	void WriteAudio(const short *apData, UINT32 alFrames);
	LONGLONG Now();

	IMFSinkWriter *mpWriter;
	DWORD mlVideoStream;
	DWORD mlAudioStream;
	bool mbHasAudio;
	CRITICAL_SECTION mWriterLock;
	bool mbRecording;
	bool mbMFStarted;

	int mlWidth, mlHeight, mlFps;
	LONGLONG mlNextFrame;		//Wall time of the next frame to capture, in 100 ns
	LARGE_INTEGER mStart, mFreq;
	std::vector<unsigned char> mvPixels;
	float mfStillEvery;
	LONGLONG mlNextStill;
	tString msStillDir;

	//Audio
	IAudioClient *mpAudioClient;
	IAudioCaptureClient *mpCapture;
	WAVEFORMATEX *mpMixFormat;
	UINT32 mlRate;
	UINT32 mlChannels;
	bool mbFloat;
	LONGLONG mlAudioFrames;		//Frames written so far, including silence
	HANDLE mhAudioThread;
	volatile bool mbAudioStop;

	//This game's volume in the Windows mixer, turned up while recording and put back after
	ISimpleAudioVolume *mpSessionVolume;
	BOOL mbSessionMute;
	float mfSessionVolume;
};

//-----------------------------------------------------------------------

LONGLONG cLuxCoopRecorderImpl::Now()
{
	LARGE_INTEGER t; QueryPerformanceCounter(&t);
	return (LONGLONG)((double)(t.QuadPart - mStart.QuadPart) * kHns / (double)mFreq.QuadPart);
}

//-----------------------------------------------------------------------

bool cLuxCoopRecorderImpl::Start(const tString& asFile, int alWidth, int alHeight, int alFps)
{
	if(mbRecording) return true;
	mlWidth = alWidth & ~1;
	mlHeight = alHeight & ~1;
	mlFps = alFps > 0 ? alFps : 30;

	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if(FAILED(MFStartup(MF_VERSION))) { Error("Coop: recorder: Media Foundation is not available\n"); return false; }
	mbMFStarted = true;

	mbHasAudio = InitAudioCapture();
	if(mbHasAudio) UnmuteOwnSound();

	IMFAttributes *pAttr = NULL;
	MFCreateAttributes(&pAttr, 1);
	pAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
	tWString sFile = cString::To16Char(asFile);
	HRESULT hr = MFCreateSinkWriterFromURL(sFile.c_str(), NULL, pAttr, &mpWriter);
	pAttr->Release();
	if(FAILED(hr)) { Error("Coop: recorder: could not create '%s' (0x%08x)\n", asFile.c_str(), (unsigned)hr); Stop(); return false; }

	////////////////////////
	// Video: RGB32 from the back buffer in, H.264 out
	IMFMediaType *pOut = NULL, *pIn = NULL;
	MFCreateMediaType(&pOut);
	pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	pOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	pOut->SetUINT32(MF_MT_AVG_BITRATE, kVideoBitrate);
	pOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	MFSetAttributeSize(pOut, MF_MT_FRAME_SIZE, mlWidth, mlHeight);
	MFSetAttributeRatio(pOut, MF_MT_FRAME_RATE, mlFps, 1);
	MFSetAttributeRatio(pOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	hr = mpWriter->AddStream(pOut, &mlVideoStream);
	pOut->Release();

	MFCreateMediaType(&pIn);
	pIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	pIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	pIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	MFSetAttributeSize(pIn, MF_MT_FRAME_SIZE, mlWidth, mlHeight);
	MFSetAttributeRatio(pIn, MF_MT_FRAME_RATE, mlFps, 1);
	MFSetAttributeRatio(pIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	//OpenGL rows run bottom up, which is what a negative stride says
	pIn->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(-(INT32)(mlWidth*4)));
	if(SUCCEEDED(hr)) hr = mpWriter->SetInputMediaType(mlVideoStream, pIn, NULL);
	pIn->Release();
	if(FAILED(hr)) { Error("Coop: recorder: no H.264 encoder for %dx%d (0x%08x)\n", mlWidth, mlHeight, (unsigned)hr); Stop(); return false; }

	////////////////////////
	// Audio: 16 bit PCM in, AAC out
	if(mbHasAudio)
	{
		MFCreateMediaType(&pOut);
		pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		pOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
		pOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
		pOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, mlRate);
		pOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
		pOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioBytesPerSec);
		hr = mpWriter->AddStream(pOut, &mlAudioStream);
		pOut->Release();

		MFCreateMediaType(&pIn);
		pIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		pIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
		pIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
		pIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, mlRate);
		pIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
		pIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
		pIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, mlRate * 4);
		if(SUCCEEDED(hr)) hr = mpWriter->SetInputMediaType(mlAudioStream, pIn, NULL);
		pIn->Release();
		if(FAILED(hr))
		{
			Warning("Coop: recorder: no AAC encoder for %u Hz, recording without sound (0x%08x)\n", mlRate, (unsigned)hr);
			mbHasAudio = false;
		}
	}

	if(FAILED(mpWriter->BeginWriting())) { Error("Coop: recorder: could not start writing\n"); Stop(); return false; }

	QueryPerformanceFrequency(&mFreq);
	QueryPerformanceCounter(&mStart);
	mlNextFrame = 0;
	mlNextStill = 0;
	mlAudioFrames = 0;
	if(mfStillEvery > 0)
	{
		msStillDir = cString::SetFileExt(asFile, "") + "_frames";
		CreateDirectoryA(msStillDir.c_str(), NULL);
	}
	mbRecording = true;

	if(mbHasAudio)
	{
		mbAudioStop = false;
		mpAudioClient->Start();
		mhAudioThread = (HANDLE)_beginthreadex(NULL, 0, AudioThreadFunc, this, 0, NULL);
	}

	Log("Coop: recording %dx%d at %d fps to '%s'%s\n", mlWidth, mlHeight, mlFps, asFile.c_str(), mbHasAudio ? "" : " (no sound)");
	return true;
}

//-----------------------------------------------------------------------

void cLuxCoopRecorderImpl::Stop()
{
	if(mhAudioThread)
	{
		mbAudioStop = true;
		WaitForSingleObject(mhAudioThread, 2000);
		CloseHandle(mhAudioThread);
		mhAudioThread = NULL;
	}
	if(mpAudioClient){ mpAudioClient->Stop(); }
	if(mpCapture){ mpCapture->Release(); mpCapture = NULL; }
	if(mpAudioClient){ mpAudioClient->Release(); mpAudioClient = NULL; }
	if(mpMixFormat){ CoTaskMemFree(mpMixFormat); mpMixFormat = NULL; }
	RestoreOwnSound();

	if(mpWriter)
	{
		if(mbRecording)
		{
			HRESULT hr = mpWriter->Finalize();
			Log("Coop: recording finished (%.1f s)%s\n", (double)Now() / kHns, FAILED(hr) ? ", but the file could not be finalized" : "");
		}
		mpWriter->Release();
		mpWriter = NULL;
	}
	mbRecording = false;
	if(mbMFStarted){ MFShutdown(); mbMFStarted = false; }
}

//-----------------------------------------------------------------------

void cLuxCoopRecorderImpl::CaptureFrame()
{
	if(mbRecording==false) return;

	LONGLONG lNow = Now();
	if(lNow < mlNextFrame) return;
	LONGLONG lFrameTime = kHns / mlFps;
	//Next slot on the frame grid; a slow frame skips slots rather than bunching them up
	mlNextFrame = (lNow / lFrameTime + 1) * lFrameTime;

	size_t lSize = (size_t)mlWidth * mlHeight * 4;
	mvPixels.resize(lSize);
	glReadBuffer(GL_BACK);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(0, 0, mlWidth, mlHeight, GL_BGRA, GL_UNSIGNED_BYTE, &mvPixels[0]);

	//A still now and then, to check a recording without playing it
	if(mfStillEvery > 0 && lNow >= mlNextStill)
	{
		mlNextStill = lNow + (LONGLONG)(mfStillEvery * kHns);
		WriteStill(lNow);
	}

	IMFMediaBuffer *pBuffer = NULL;
	if(FAILED(MFCreateMemoryBuffer((DWORD)lSize, &pBuffer))) return;
	BYTE *pData = NULL;
	pBuffer->Lock(&pData, NULL, NULL);
	memcpy(pData, &mvPixels[0], lSize);
	pBuffer->Unlock();
	pBuffer->SetCurrentLength((DWORD)lSize);

	IMFSample *pSample = NULL;
	MFCreateSample(&pSample);
	pSample->AddBuffer(pBuffer);
	pSample->SetSampleTime(lNow);
	pSample->SetSampleDuration(lFrameTime);

	EnterCriticalSection(&mWriterLock);
	mpWriter->WriteSample(mlVideoStream, pSample);
	LeaveCriticalSection(&mWriterLock);

	pSample->Release();
	pBuffer->Release();
}

//-----------------------------------------------------------------------

void cLuxCoopRecorderImpl::WriteStill(LONGLONG alTime)
{
	//32 bit BMP: rows bottom up like the OpenGL read back, so the pixels go in as they are
	char sName[32];
	sprintf(sName, "t%05.1f.bmp", (double)alTime / kHns);
	tString sPath = msStillDir + "/" + sName;
	FILE *pFile = fopen(sPath.c_str(), "wb");
	if(pFile==NULL) return;

	DWORD lImage = (DWORD)mvPixels.size();
	BITMAPFILEHEADER fh = {0};
	BITMAPINFOHEADER ih = {0};
	fh.bfType = 0x4D42;
	fh.bfOffBits = sizeof(fh) + sizeof(ih);
	fh.bfSize = fh.bfOffBits + lImage;
	ih.biSize = sizeof(ih);
	ih.biWidth = mlWidth;
	ih.biHeight = mlHeight;
	ih.biPlanes = 1;
	ih.biBitCount = 32;
	ih.biCompression = BI_RGB;
	ih.biSizeImage = lImage;
	fwrite(&fh, sizeof(fh), 1, pFile);
	fwrite(&ih, sizeof(ih), 1, pFile);
	fwrite(&mvPixels[0], 1, lImage, pFile);
	fclose(pFile);
}

//-----------------------------------------------------------------------

bool cLuxCoopRecorderImpl::InitAudioCapture()
{
	mpAudioClient = NULL; mpCapture = NULL; mpMixFormat = NULL;

	IMMDeviceEnumerator *pEnum = NULL;
	IMMDevice *pDevice = NULL;
	if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum))) return false;
	HRESULT hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	pEnum->Release();
	if(FAILED(hr)) { Warning("Coop: recorder: no audio output to record\n"); return false; }

	hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&mpAudioClient);
	pDevice->Release();
	if(FAILED(hr)) return false;

	mpAudioClient->GetMixFormat(&mpMixFormat);
	mlRate = mpMixFormat->nSamplesPerSec;
	mlChannels = mpMixFormat->nChannels;
	mbFloat = mpMixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
			  (mpMixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ((WAVEFORMATEXTENSIBLE*)mpMixFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
	if(mlRate != 44100 && mlRate != 48000)
	{
		Warning("Coop: recorder: audio runs at %u Hz, AAC needs 44.1 or 48 kHz, recording without sound\n", mlRate);
		return false;
	}

	//Loopback: what the PC plays, the game included
	hr = mpAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kHns / 5, 0, mpMixFormat, NULL);
	if(FAILED(hr)) { Warning("Coop: recorder: audio loopback failed (0x%08x)\n", (unsigned)hr); return false; }
	hr = mpAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&mpCapture);
	return SUCCEEDED(hr);
}

//Loopback hears the game only as loud as the Windows mixer lets it play; test windows are often muted there
void cLuxCoopRecorderImpl::UnmuteOwnSound()
{
	IMMDeviceEnumerator *pEnum = NULL;
	IMMDevice *pDevice = NULL;
	IAudioSessionManager2 *pManager = NULL;
	IAudioSessionEnumerator *pSessions = NULL;
	if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum))) return;
	if(SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)) &&
	   SUCCEEDED(pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pManager)) &&
	   SUCCEEDED(pManager->GetSessionEnumerator(&pSessions)))
	{
		int lCount = 0;
		pSessions->GetCount(&lCount);
		for(int i=0; i<lCount && mpSessionVolume==NULL; ++i)
		{
			IAudioSessionControl *pControl = NULL;
			IAudioSessionControl2 *pControl2 = NULL;
			DWORD lPid = 0;
			if(SUCCEEDED(pSessions->GetSession(i, &pControl)) &&
			   SUCCEEDED(pControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pControl2)) &&
			   SUCCEEDED(pControl2->GetProcessId(&lPid)) && lPid == GetCurrentProcessId())
			{
				ISimpleAudioVolume *pVolume = NULL;
				if(SUCCEEDED(pControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pVolume)))
				{
					pVolume->GetMute(&mbSessionMute);
					pVolume->GetMasterVolume(&mfSessionVolume);
					if(mbSessionMute || mfSessionVolume < 0.99f)
					{
						pVolume->SetMute(FALSE, NULL);
						pVolume->SetMasterVolume(1.0f, NULL);
						Log("Coop: recorder: game sound unmuted in the Windows mixer while recording (was %s, %.0f%%)\n",
							mbSessionMute ? "muted" : "on", mfSessionVolume * 100);
						mpSessionVolume = pVolume;
					}
					else pVolume->Release();
				}
			}
			if(pControl2) pControl2->Release();
			if(pControl) pControl->Release();
		}
	}
	if(pSessions) pSessions->Release();
	if(pManager) pManager->Release();
	if(pDevice) pDevice->Release();
	pEnum->Release();
}

void cLuxCoopRecorderImpl::RestoreOwnSound()
{
	if(mpSessionVolume==NULL) return;
	mpSessionVolume->SetMasterVolume(mfSessionVolume, NULL);
	mpSessionVolume->SetMute(mbSessionMute, NULL);
	mpSessionVolume->Release();
	mpSessionVolume = NULL;
	Log("Coop: recorder: game sound in the Windows mixer put back\n");
}

unsigned __stdcall cLuxCoopRecorderImpl::AudioThreadFunc(void *apThis)
{
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	static_cast<cLuxCoopRecorderImpl*>(apThis)->AudioLoop();
	CoUninitialize();
	return 0;
}

void cLuxCoopRecorderImpl::AudioLoop()
{
	std::vector<short> vConverted;
	while(mbAudioStop==false)
	{
		Sleep(10);

		UINT32 lPacket = 0;
		while(SUCCEEDED(mpCapture->GetNextPacketSize(&lPacket)) && lPacket > 0)
		{
			BYTE *pData = NULL; UINT32 lFrames = 0; DWORD lFlags = 0;
			if(FAILED(mpCapture->GetBuffer(&pData, &lFrames, &lFlags, NULL, NULL))) break;

			vConverted.resize(lFrames * 2);
			for(UINT32 i=0; i<lFrames; ++i)
			{
				for(UINT32 c=0; c<2; ++c)
				{
					UINT32 lSrc = c < mlChannels ? c : 0;
					float f = 0;
					if((lFlags & AUDCLNT_BUFFERFLAGS_SILENT)==0)
					{
						if(mbFloat)	f = ((float*)pData)[i*mlChannels + lSrc];
						else		f = ((short*)pData)[i*mlChannels + lSrc] / 32768.0f;
					}
					vConverted[i*2 + c] = (short)(cMath::Clamp(f, -1.0f, 1.0f) * 32767.0f);
				}
			}
			mpCapture->ReleaseBuffer(lFrames);
			WriteAudio(&vConverted[0], lFrames);
		}

		//Windows sends nothing while nothing plays; keep the sound track as long as the picture
		LONGLONG lWant = Now() * mlRate / kHns;
		if(lWant - mlAudioFrames > (LONGLONG)(mlRate / 10))
		{
			UINT32 lGap = (UINT32)(lWant - mlAudioFrames);
			vConverted.assign(lGap * 2, 0);
			WriteAudio(&vConverted[0], lGap);
		}
	}
}

void cLuxCoopRecorderImpl::WriteAudio(const short *apData, UINT32 alFrames)
{
	DWORD lBytes = alFrames * 4;
	IMFMediaBuffer *pBuffer = NULL;
	if(FAILED(MFCreateMemoryBuffer(lBytes, &pBuffer))) return;
	BYTE *pDst = NULL;
	pBuffer->Lock(&pDst, NULL, NULL);
	memcpy(pDst, apData, lBytes);
	pBuffer->Unlock();
	pBuffer->SetCurrentLength(lBytes);

	IMFSample *pSample = NULL;
	MFCreateSample(&pSample);
	pSample->AddBuffer(pBuffer);
	pSample->SetSampleTime(mlAudioFrames * kHns / mlRate);
	pSample->SetSampleDuration((LONGLONG)alFrames * kHns / mlRate);
	mlAudioFrames += alFrames;

	EnterCriticalSection(&mWriterLock);
	if(mpWriter) mpWriter->WriteSample(mlAudioStream, pSample);
	LeaveCriticalSection(&mWriterLock);

	pSample->Release();
	pBuffer->Release();
}

#else

class cLuxCoopRecorderImpl
{
public:
	void SetStills(float){}
	bool Start(const tString&, int, int, int){ Warning("Coop: recording is only available on Windows\n"); return false; }
	void Stop(){}
	void CaptureFrame(){}
	bool IsRecording(){ return false; }
};

#endif

//////////////////////////////////////////////////////////////////////////
// PUBLIC
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxCoopRecorder::cLuxCoopRecorder() { mpImpl = new cLuxCoopRecorderImpl(); }
cLuxCoopRecorder::~cLuxCoopRecorder() { delete mpImpl; }

bool cLuxCoopRecorder::Start(const tString& asFile, int alWidth, int alHeight, int alFps){ return mpImpl->Start(asFile, alWidth, alHeight, alFps); }
void cLuxCoopRecorder::Stop(){ mpImpl->Stop(); }
bool cLuxCoopRecorder::IsRecording(){ return mpImpl->IsRecording(); }
void cLuxCoopRecorder::CaptureFrame(){ mpImpl->CaptureFrame(); }
void cLuxCoopRecorder::SetStills(float afEvery){ mpImpl->SetStills(afEvery); }

//-----------------------------------------------------------------------
