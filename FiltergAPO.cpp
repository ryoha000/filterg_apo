#include "stdafx.h"
#include "debug.h"
#include "FilterProcess.h"
#include "HANDLE_NAME.h"

#include <vector>
#include <windows.h>

#include "FiltergAPO.h"

using namespace std;

long FiltergAPO::instCount = 0;
const CRegAPOProperties<1> FiltergAPO::regProperties(
	__uuidof(FiltergAPO), L"FiltergAPO", L"" /* no copyright specified, see License.txt */, 1, 0, __uuidof(IAudioProcessingObject),
	(APO_FLAG)(/* APO_FLAG_SAMPLESPERFRAME_MUST_MATCH | */APO_FLAG_FRAMESPERSECOND_MUST_MATCH | APO_FLAG_BITSPERSAMPLE_MUST_MATCH | APO_FLAG_INPLACE));

FiltergAPO::FiltergAPO(IUnknown* pUnkOuter)
	: CBaseAudioProcessingObject(regProperties)
{
	refCount = 1;
	if (pUnkOuter != NULL)
	{
		this->pUnkOuter = pUnkOuter;
	}
	else
	{
		this->pUnkOuter = reinterpret_cast<IUnknown*>(static_cast<INonDelegatingUnknown*>(this));
	}

	hTerminate = CreateEvent(NULL, FALSE, FALSE, TerminateHandleName);
	if (hTerminate == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hTerminate == NULL in ROOT");
	}

	scheduler = make_shared<ProcessScheduler>(channelCount);

	InterlockedIncrement(&instCount);
}

FiltergAPO::~FiltergAPO()
{
	if (hTerminate != NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] hTerminate != NULL will Terminate");
		SetEvent(hTerminate);
		CloseHandle(hTerminate);
	}

	InterlockedDecrement(&instCount);
}

HRESULT FiltergAPO::QueryInterface(const IID& iid, void** ppv)
{
	return pUnkOuter->QueryInterface(iid, ppv);
}

ULONG FiltergAPO::AddRef()
{
	return pUnkOuter->AddRef();
}

ULONG FiltergAPO::Release()
{
	return pUnkOuter->Release();
}

HRESULT FiltergAPO::GetLatency(HNSTIME* pTime)
{
	if (!pTime)
		return E_POINTER;

	if (!m_bIsLocked)
		return APOERR_ALREADY_UNLOCKED;

	*pTime = 0;

	return S_OK;
}

HRESULT FiltergAPO::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
	OutputDebugStringFW(L"FiltergAPO::Initialize(UINT32 cbDataSize, BYTE* pbyData)");
	if ((NULL == pbyData) && (0 != cbDataSize))
		return E_INVALIDARG;
	if ((NULL != pbyData) && (0 == cbDataSize))
		return E_POINTER;
	if (cbDataSize != sizeof(APOInitSystemEffects))
		return E_INVALIDARG;

	return S_OK;
}

HRESULT FiltergAPO::IsInputFormatSupported(IAudioMediaType* pOutputFormat,
	IAudioMediaType* pRequestedInputFormat, IAudioMediaType** ppSupportedInputFormat)
{
	if (!pRequestedInputFormat)
		return E_POINTER;

	UNCOMPRESSEDAUDIOFORMAT inFormat;
	HRESULT hr = pRequestedInputFormat->GetUncompressedAudioFormat(&inFormat);
	if (FAILED(hr))
	{
		return hr;
	}

	UNCOMPRESSEDAUDIOFORMAT outFormat;
	hr = pOutputFormat->GetUncompressedAudioFormat(&outFormat);
	if (FAILED(hr))
	{
		return hr;
	}

	hr = CBaseAudioProcessingObject::IsInputFormatSupported(pOutputFormat, pRequestedInputFormat, ppSupportedInputFormat);

	return hr;
}

HRESULT FiltergAPO::LockForProcess(UINT32 u32NumInputConnections,
	APO_CONNECTION_DESCRIPTOR** ppInputConnections, UINT32 u32NumOutputConnections,
	APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
	HRESULT hr;

	UNCOMPRESSEDAUDIOFORMAT outFormat;
	hr = ppOutputConnections[0]->pFormat->GetUncompressedAudioFormat(&outFormat);
	if (FAILED(hr))
		return hr;

	hr = CBaseAudioProcessingObject::LockForProcess(u32NumInputConnections, ppInputConnections,
			u32NumOutputConnections, ppOutputConnections);
	if (FAILED(hr))
		return hr;

	channelCount = outFormat.dwSamplesPerFrame;
	scheduler->UpdateChannels(channelCount);

	return hr;
}

HRESULT FiltergAPO::UnlockForProcess()
{
	return CBaseAudioProcessingObject::UnlockForProcess();
}

#pragma AVRT_CODE_BEGIN
void FiltergAPO::APOProcess(UINT32 u32NumInputConnections,
	APO_CONNECTION_PROPERTY** ppInputConnections, UINT32 u32NumOutputConnections,
	APO_CONNECTION_PROPERTY** ppOutputConnections)
{
	switch (ppInputConnections[0]->u32BufferFlags)
	{
	case BUFFER_VALID:
		{
			float* inputFrames = reinterpret_cast<float*>(ppInputConnections[0]->pBuffer);
			float* outputFrames = reinterpret_cast<float*>(ppOutputConnections[0]->pBuffer);

			scheduler->ProcessFrame(inputFrames, ppOutputConnections[0]->u32ValidFrameCount);
			scheduler->GetProcessedFrames(outputFrames);

			ppOutputConnections[0]->u32ValidFrameCount = ppInputConnections[0]->u32ValidFrameCount;
			ppOutputConnections[0]->u32BufferFlags = ppInputConnections[0]->u32BufferFlags;

			break;
		}
	case BUFFER_SILENT:
		ppOutputConnections[0]->u32ValidFrameCount = ppInputConnections[0]->u32ValidFrameCount;
		ppOutputConnections[0]->u32BufferFlags = ppInputConnections[0]->u32BufferFlags;

		break;
	}
}
#pragma AVRT_CODE_END

HRESULT FiltergAPO::NonDelegatingQueryInterface(const IID& iid, void** ppv)
{
	if (iid == __uuidof(IUnknown))
		*ppv = static_cast<INonDelegatingUnknown*>(this);
	else if (iid == __uuidof(IAudioProcessingObject))
		*ppv = static_cast<IAudioProcessingObject*>(this);
	else if (iid == __uuidof(IAudioProcessingObjectRT))
		*ppv = static_cast<IAudioProcessingObjectRT*>(this);
	else if (iid == __uuidof(IAudioProcessingObjectConfiguration))
		*ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
	else if (iid == __uuidof(IAudioSystemEffects))
		*ppv = static_cast<IAudioSystemEffects*>(this);
	else
	{
		*ppv = NULL;
		return E_NOINTERFACE;
	}

	reinterpret_cast<IUnknown*>(*ppv)->AddRef();
	return S_OK;
}

ULONG FiltergAPO::NonDelegatingAddRef()
{
	return InterlockedIncrement(&refCount);
}

ULONG FiltergAPO::NonDelegatingRelease()
{
	if (InterlockedDecrement(&refCount) == 0)
	{
		delete this;
		return 0;
	}

	return refCount;
}