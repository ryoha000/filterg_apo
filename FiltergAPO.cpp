#include "stdafx.h"
#include "debug.h"
#include "FftBox.h"

#include <kiss_fft.h>

#include "FiltergAPO.h"

using namespace std;
#define N 16

void FiltergAPO::DlegateWave(float* content, unsigned frames, unsigned channels)
{
	loop++;

	if (loop % 100 == 0)
	{
		OutputDebugStringFW(L"[FiltergAPO] frames: %d, channels: %d..", frames, channels);
	}
	return;
}

void FiltergAPO::TestFft()
{
	/*typedef std::complex<float> Complex;
	std::vector<Complex> v;

	float t = 0.0;
	float omega = 1000.0;

	for (unsigned i = 0; i < std::pow(2, 9); i++) {
		float re = std::sinf(omega * t);
		v.push_back(Complex(re, 0));
		t += 0.001;
	}

	auto plan = fft::FftArray(v.begin(), v.end());

	OutputDebugStringFW(L"[FiltergAPO] TestFft input length: %d", plan.size());

	plan.fft();*/

	float freq = 1.0;

	kiss_fft_cfg cfg_fft = kiss_fft_alloc(N, FALSE, NULL, NULL);
	kiss_fft_cfg cfg_ifft = kiss_fft_alloc(N, TRUE, NULL, NULL);

	kiss_fft_cpx buffer_in[N];
	kiss_fft_cpx buffer_out[N];
	for (unsigned i = 0; i < N; i++) {
		float re = std::sinf(2 * 3.14 * freq * i / N);
		buffer_in[i].r = re;
		buffer_in[i].i = 0.0f;
	}

	// debug
	for (unsigned i = 0; i < 16; i++) {
		OutputDebugStringFW(L"[FiltergAPO] TestFft set %g", buffer_in[i].r);
	}

	kiss_fft(cfg_fft, buffer_in, buffer_out);

	//debug
	for (unsigned i = 0; i < 16; i++) {
		OutputDebugStringFW(L"[FiltergAPO] TestFft fft abs: %g, omega/pi: %g", sqrtf(buffer_out[i].r * buffer_out[i].r + buffer_out[i].i * buffer_out[i].i), atan2(buffer_out[i].i, buffer_out[i].r) / 3.14);
	}

	kiss_fft(cfg_ifft, buffer_out, buffer_in);

	//debug
	for (unsigned i = 0; i < 16; i++) {
		OutputDebugStringFW(L"[FiltergAPO] TestFft ifft real: %g, im: %g", buffer_in[i].r, buffer_in[i].i);
	}

	//fft::FftArray z(16);
	//for (int i = 0; i < z.size(); ++i) {
	//	OutputDebugStringFW(L"[FiltergAPO] TestFft set %g", sinf(2. * M_PI * i / z.size()));
	//	z[i] = sinf(2. * M_PI * i / z.size());
	//}
	//z.fft();	// フーリエ変換
	//for (fft::FftArray::iterator it = z.begin(); it != z.end(); ++it) {
	//	OutputDebugStringFW(L"[FiltergAPO] TestFft fft abs: %g, omega/pi: %g", sqrtf(it->real() * it->real() + it->imag() * it->imag()), atan2(it->imag(), it->real()) / 3.14);
	//}
	//z.ifft();	// フーリエ逆変換
	//for (int i = 0; i < z.size(); ++i) {
	//	OutputDebugStringFW(L"[FiltergAPO] TestFft ifft real: %g, im: %g", z[i].real(), z[i].imag());
	//}

	kiss_fft_free(cfg_fft);
	kiss_fft_free(cfg_ifft);
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

	TestFft();

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