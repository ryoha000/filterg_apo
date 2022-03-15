#include "stdafx.h"
#include "debug.h"

#include "MinimalAPO.h"

using namespace std;

void MinimalAPO::DlegateWave(float* content, unsigned frames, unsigned channels)
{
	loop++;

	if (loop % 100 == 0)
	{
		OutputDebugStringFW(L"[MinimalAPO] frames: %d, channels: %d", frames, channels);
	}
	return;
}

long MinimalAPO::instCount = 0;
const CRegAPOProperties<1> MinimalAPO::regProperties(
	__uuidof(MinimalAPO), L"MinimalAPO", L"" /* no copyright specified, see License.txt */, 1, 0, __uuidof(IAudioProcessingObject),
	(APO_FLAG)(/* APO_FLAG_SAMPLESPERFRAME_MUST_MATCH | */APO_FLAG_FRAMESPERSECOND_MUST_MATCH | APO_FLAG_BITSPERSAMPLE_MUST_MATCH | APO_FLAG_INPLACE));

MinimalAPO::MinimalAPO(IUnknown* pUnkOuter)
	: CBaseAudioProcessingObject(regProperties)
{
	OutputDebugStringFW(L"MinimalAPO::MinimalAPO(IUnknown* pUnkOuter): CBaseAudioProcessingObject(regProperties)");
	refCount = 1;
	if (pUnkOuter != NULL)
	{
		this->pUnkOuter = pUnkOuter;
		OutputDebugStringFW(L"MinimalAPO::MinimalAPO(IUnknown* pUnkOuter): CBaseAudioProcessingObject(regProperties) pUnkOuter != NULL");
	}
	else
	{
		this->pUnkOuter = reinterpret_cast<IUnknown*>(static_cast<INonDelegatingUnknown*>(this));
		OutputDebugStringFW(L"MinimalAPO::MinimalAPO(IUnknown* pUnkOuter): CBaseAudioProcessingObject(regProperties) pUnkOuter == NULL");
	}

	loop = 1;

	InterlockedIncrement(&instCount);
}

MinimalAPO::~MinimalAPO()
{
	OutputDebugStringFW(L"MinimalAPO::~MinimalAPO()");
	InterlockedDecrement(&instCount);
}

HRESULT MinimalAPO::QueryInterface(const IID& iid, void** ppv)
{
	OutputDebugStringFW(L"MinimalAPO::QueryInterface(const IID& iid, void** ppv)");
	return pUnkOuter->QueryInterface(iid, ppv);
}

ULONG MinimalAPO::AddRef()
{
	OutputDebugStringFW(L"MinimalAPO::AddRef()");
	return pUnkOuter->AddRef();
}

ULONG MinimalAPO::Release()
{
	OutputDebugStringFW(L"MinimalAPO::Release()");
	return pUnkOuter->Release();
}

HRESULT MinimalAPO::GetLatency(HNSTIME* pTime)
{
	OutputDebugStringFW(L"MinimalAPO::GetLatency(HNSTIME* pTime)");
	if (!pTime)
		return E_POINTER;

	if (!m_bIsLocked)
		return APOERR_ALREADY_UNLOCKED;

	*pTime = 0;

	return S_OK;
}

HRESULT MinimalAPO::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
	OutputDebugStringFW(L"MinimalAPO::Initialize(UINT32 cbDataSize, BYTE* pbyData)");
	if ((NULL == pbyData) && (0 != cbDataSize))
		return E_INVALIDARG;
	if ((NULL != pbyData) && (0 == cbDataSize))
		return E_POINTER;
	if (cbDataSize != sizeof(APOInitSystemEffects))
		return E_INVALIDARG;

	return S_OK;
}

HRESULT MinimalAPO::IsInputFormatSupported(IAudioMediaType* pOutputFormat,
	IAudioMediaType* pRequestedInputFormat, IAudioMediaType** ppSupportedInputFormat)
{
	OutputDebugStringFW(L"MinimalAPO::IsInputFormatSupported(IAudioMediaType* pOutputFormat,");
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

HRESULT MinimalAPO::LockForProcess(UINT32 u32NumInputConnections,
	APO_CONNECTION_DESCRIPTOR** ppInputConnections, UINT32 u32NumOutputConnections,
	APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
	OutputDebugStringFW(L"MinimalAPO::LockForProcess(UINT32 u32NumInputConnections,");
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

	return hr;
}

HRESULT MinimalAPO::UnlockForProcess()
{
	OutputDebugStringFW(L"MinimalAPO::UnlockForProcess()");
	return CBaseAudioProcessingObject::UnlockForProcess();
}

#pragma AVRT_CODE_BEGIN
void MinimalAPO::APOProcess(UINT32 u32NumInputConnections,
	APO_CONNECTION_PROPERTY** ppInputConnections, UINT32 u32NumOutputConnections,
	APO_CONNECTION_PROPERTY** ppOutputConnections)
{
	switch (ppInputConnections[0]->u32BufferFlags)
	{
	case BUFFER_VALID:
		{
			float* inputFrames = reinterpret_cast<float*>(ppInputConnections[0]->pBuffer);
			float* outputFrames = reinterpret_cast<float*>(ppOutputConnections[0]->pBuffer);

			DlegateWave(inputFrames, ppOutputConnections[0]->u32ValidFrameCount, channelCount);


			for (unsigned i = 0; i < ppOutputConnections[0]->u32ValidFrameCount; i++)
			{
				for (unsigned j = 0; j < channelCount; j++)
				{
					float s = inputFrames[i * channelCount + j];

					// process audio sample
					// s *= 0.01f * abs(((int)i % 200) - 100);

					outputFrames[i * channelCount + j] = s;
				}
			}

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

HRESULT MinimalAPO::NonDelegatingQueryInterface(const IID& iid, void** ppv)
{
	OutputDebugStringFW(L"MinimalAPO::NonDelegatingQueryInterface(const IID& iid, void** ppv)");
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

ULONG MinimalAPO::NonDelegatingAddRef()
{
	OutputDebugStringFW(L"MinimalAPO::NonDelegatingAddRef()");
	return InterlockedIncrement(&refCount);
}

ULONG MinimalAPO::NonDelegatingRelease()
{
	OutputDebugStringFW(L"MinimalAPO::NonDelegatingRelease()");
	if (InterlockedDecrement(&refCount) == 0)
	{
		delete this;
		return 0;
	}

	return refCount;
}