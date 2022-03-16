#include "stdafx.h"
#include "debug.h"

#include <vector>
#include <cmath> // debug
#include <kiss_fft.h>

#include "FiltergAPO.h"

using namespace std;

float norm_cpx(kiss_fft_cpx v) {
	return sqrtf(v.r * v.r + v.i * v.i);
}

// x = k / (N - 1)
float hamming(unsigned x) {
	return 0.54 - 0.46 * cos(2 * 3.14 * x);
}

void FiltergAPO::DlegateWave(float* content, unsigned frames, unsigned channels)
{
	loop++;

	if (loop % 100 == 0)
	{
		OutputDebugStringFW(L"[FiltergAPO] frames: %d, channels: %d..", frames, channels);

		// fft してみる

		float freq = 1.0;

		kiss_fft_cfg cfg_fft = kiss_fft_alloc(frames, FALSE, NULL, NULL);
		kiss_fft_cfg cfg_ifft = kiss_fft_alloc(frames, TRUE, NULL, NULL);

		vector<kiss_fft_cpx> buffer_in(frames);
		vector<kiss_fft_cpx> buffer_out(frames);
		for (unsigned target_channel = 0; target_channel < channels; target_channel++)
		{
			for (unsigned i = 0; i < frames; i++)
			{
				for (unsigned j = 0; j < channels; j++)
				{
					if (j != target_channel) {
						continue;
					}

					float re = content[i * channels + j];

					// 窓関数の係数(ハミング窓)
					float coefficient = hamming(i / (frames - 1));

					buffer_in[i].r = re * coefficient;
					buffer_in[i].i = 0.0f;

					// process audio sample
					// s *= 0.01f * abs(((int)i % 200) - 100);

					// outputFrames[i * channels + j] = s;
				}
			}

			kiss_fft(cfg_fft, &buffer_in[0], &buffer_out[0]);

			// normの平均とTOP3を出す
			float norm_sum = 0.0f;
			float norm_top[3] = {0.0f, 0.0f, 0.0f};
			unsigned norm_top_index[3] = {0, 0, 0};
			for (unsigned i = 0; i < frames; i++) {
				// TODO: 正規化？(する必要ないかも、しないならifftのとき要素数で割る)

				// 窓関数の補正値
				float window_correction = hamming(i / (frames - 1)) / frames;

				float norm = norm_cpx(buffer_out[i]) * window_correction;

				norm_sum += norm;

				if (norm_top[0] < norm) {
					norm_top[2] = norm_top[1];
					norm_top[1] = norm_top[0];
					norm_top[0] = norm;

					norm_top_index[2] = norm_top_index[1];
					norm_top_index[1] = norm_top_index[0];
					norm_top_index[0] = i;
				} else if (norm_top[1] < norm) {
					norm_top[2] = norm_top[1];
					norm_top[1] = norm;

					norm_top_index[2] = norm_top_index[1];
					norm_top_index[1] = i;
				} else if (norm_top[2] < norm) {
					norm_top[2] = norm;

					norm_top_index[2] = i;
				}
			}

			OutputDebugStringFW(L"[FiltergAPO] [channel%d] frames   : %d", target_channel, frames);
			OutputDebugStringFW(L"[FiltergAPO] [channel%d] norm avg : %g", target_channel, norm_sum / frames);
			for (unsigned i = 0; i < 3; i++) {
				OutputDebugStringFW(L"[FiltergAPO] [channel%d] top%d norm: %g, k: %d", target_channel, i + 1, norm_top[i], norm_top_index[i]);
			}
		}

		kiss_fft_free(cfg_fft);
		kiss_fft_free(cfg_ifft);
	}
	return;
}

long FiltergAPO::instCount = 0;
const CRegAPOProperties<1> FiltergAPO::regProperties(
	__uuidof(FiltergAPO), L"FiltergAPO", L"" /* no copyright specified, see License.txt */, 1, 0, __uuidof(IAudioProcessingObject),
	(APO_FLAG)(/* APO_FLAG_SAMPLESPERFRAME_MUST_MATCH | */APO_FLAG_FRAMESPERSECOND_MUST_MATCH | APO_FLAG_BITSPERSAMPLE_MUST_MATCH | APO_FLAG_INPLACE));

FiltergAPO::FiltergAPO(IUnknown* pUnkOuter)
	: CBaseAudioProcessingObject(regProperties)
{
	OutputDebugStringFW(L"FiltergAPO::FiltergAPO(IUnknown* pUnkOuter): CBaseAudioProcessingObject(regProperties)");
	refCount = 1;
	if (pUnkOuter != NULL)
	{
		this->pUnkOuter = pUnkOuter;
		OutputDebugStringFW(L"FiltergAPO::FiltergAPO(IUnknown* pUnkOuter): CBaseAudioProcessingObject(regProperties) pUnkOuter != NULL");
	}
	else
	{
		this->pUnkOuter = reinterpret_cast<IUnknown*>(static_cast<INonDelegatingUnknown*>(this));
		OutputDebugStringFW(L"FiltergAPO::FiltergAPO(IUnknown* pUnkOuter): CBaseAudioProcessingObject(regProperties) pUnkOuter == NULL");
	}

	loop = 1;

	InterlockedIncrement(&instCount);
}

FiltergAPO::~FiltergAPO()
{
	OutputDebugStringFW(L"FiltergAPO::~FiltergAPO()");
	InterlockedDecrement(&instCount);
}

HRESULT FiltergAPO::QueryInterface(const IID& iid, void** ppv)
{
	OutputDebugStringFW(L"FiltergAPO::QueryInterface(const IID& iid, void** ppv)");
	return pUnkOuter->QueryInterface(iid, ppv);
}

ULONG FiltergAPO::AddRef()
{
	OutputDebugStringFW(L"FiltergAPO::AddRef()");
	return pUnkOuter->AddRef();
}

ULONG FiltergAPO::Release()
{
	OutputDebugStringFW(L"FiltergAPO::Release()");
	return pUnkOuter->Release();
}

HRESULT FiltergAPO::GetLatency(HNSTIME* pTime)
{
	OutputDebugStringFW(L"FiltergAPO::GetLatency(HNSTIME* pTime)");
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
	OutputDebugStringFW(L"FiltergAPO::IsInputFormatSupported(IAudioMediaType* pOutputFormat,");
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
	OutputDebugStringFW(L"FiltergAPO::LockForProcess(UINT32 u32NumInputConnections,");
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

HRESULT FiltergAPO::UnlockForProcess()
{
	OutputDebugStringFW(L"FiltergAPO::UnlockForProcess()");
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

HRESULT FiltergAPO::NonDelegatingQueryInterface(const IID& iid, void** ppv)
{
	OutputDebugStringFW(L"FiltergAPO::NonDelegatingQueryInterface(const IID& iid, void** ppv)");
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
	OutputDebugStringFW(L"FiltergAPO::NonDelegatingAddRef()");
	return InterlockedIncrement(&refCount);
}

ULONG FiltergAPO::NonDelegatingRelease()
{
	OutputDebugStringFW(L"FiltergAPO::NonDelegatingRelease()");
	if (InterlockedDecrement(&refCount) == 0)
	{
		delete this;
		return 0;
	}

	return refCount;
}