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

	is_done_10milli = vector<bool>(); // debug
	last_return_samples = vector<float>();
	yet_return_samples_vec = vector<vector<float>>();
	process_result = vector<float>();
	processing_vec = vector<float>();
	is_processing = FALSE;

	option _opt;
	_opt.channels = channelCount;
	_opt.m = CreateMutex(
		NULL,
		FALSE,
		NULL);

	opt = _opt;

	bool is_setuup_ok = TRUE;

	hProcessThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)asyncFilterProcess, &opt, 0, NULL);

	HANDLE hInitialize;
	hInitialize = CreateEvent(NULL, FALSE, FALSE, InitializeHandleName);
	if (hInitialize == NULL)
	{
		is_setuup_ok = FALSE;
		OutputDebugStringFW(L"[FiltergAPO] ERROR hInitialize == NULL");
	}

	// たくさんhandleつくる
	hTerminate = CreateEvent(NULL, FALSE, FALSE, TerminateHandleName);
	if (hTerminate == NULL)
	{
		is_setuup_ok = FALSE;
		OutputDebugStringFW(L"[FiltergAPO] ERROR hTerminate == NULL");
	}
	hProcess = CreateEvent(NULL, FALSE, FALSE, ProcessHandleName);
	if (hProcess == NULL)
	{
		is_setuup_ok = FALSE;
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcess == NULL");
	}
	hProcessDone = CreateEvent(NULL, FALSE, FALSE, ProcessDoneHandleName);
	if (hProcessDone == NULL)
	{
		is_setuup_ok = FALSE;
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessDone == NULL");
	}
	hUpdateChannel = CreateEvent(NULL, FALSE, FALSE, UpdateChannelsHandleName);
	if (hUpdateChannel == NULL)
	{
		is_setuup_ok = FALSE;
		OutputDebugStringFW(L"[FiltergAPO] ERROR hUpdateChannel == NULL");
	}

	if (is_setuup_ok) {
		DWORD initializeResult = WaitForSingleObject(hInitialize, 1000);
		if (initializeResult == WAIT_OBJECT_0)
		{
			OutputDebugStringFW(L"[FiltergAPO] initializeResult == WAIT_OBJECT_0");
		}
		if (initializeResult == WAIT_FAILED)
		{
			OutputDebugStringFW(L"[FiltergAPO] initializeResult == WAIT_FAILED");
		}
		CloseHandle(hInitialize);
	}

	InterlockedIncrement(&instCount);
}

FiltergAPO::~FiltergAPO()
{
	OutputDebugStringFW(L"FiltergAPO::~FiltergAPO()");

	if (hTerminate != NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] hTerminate != NULL will Terminate");
		SetEvent(hTerminate);
		CloseHandle(hTerminate);
	}
	if (hProcessThread != NULL)
	{
		CloseHandle(hProcessThread);
	}
	if (hProcess != NULL)
	{
		CloseHandle(hProcess);
	}
	if (hProcessDone != NULL)
	{
		CloseHandle(hProcessDone);
	}
	if (hUpdateChannel != NULL)
	{
		CloseHandle(hUpdateChannel);
	}

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
	opt.setChannels(channelCount);
	if (hUpdateChannel != NULL)
	{
		SetEvent(hUpdateChannel);
	}


	return hr;
}

HRESULT FiltergAPO::UnlockForProcess()
{
	OutputDebugStringFW(L"FiltergAPO::UnlockForProcess()");
	return CBaseAudioProcessingObject::UnlockForProcess();
}

// x = k / N (ハミング窓を逆にしたみたいな形のグラフ、適当)(maxが1でcos^2)
float inv_hamming(unsigned x) {
	return powf(cosf(2.0f * 3.14f * x), 2.0f);
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

			int length = ppOutputConnections[0]->u32ValidFrameCount * channelCount;
			vector<float> v;
			for (int i = 0; i < length; i++)
			{
				v.push_back(inputFrames[i]);
			}
			yet_return_samples_vec.push_back(v);

			vector<float> output = vector<float>();

			if (!is_processing)
			{
				OutputDebugStringFW(L"[FiltergAPO] !is_processing");
				process_result.resize(length);
				processing_vec.insert(processing_vec.end(), yet_return_samples_vec[0].begin(), yet_return_samples_vec[0].end());
				opt.lock();
				opt.processInput = processing_vec.data();
				opt.processOutput = process_result.data();
				opt.processFrames = ppOutputConnections[0]->u32ValidFrameCount;
				opt.unlock();

				SetEvent(hProcess);
				is_processing = TRUE;
			}


			DWORD processResult = WaitForSingleObject(hProcessDone, 8);
			if (processResult == WAIT_OBJECT_0)
			{
				opt.lock();
				is_processing = FALSE;
				yet_return_samples_vec.erase(yet_return_samples_vec.begin(), yet_return_samples_vec.begin() + 1);
				// TODO: この境界条件は怪しい(フレーム数やチャネル数が変わるとバグる)
				for (int i = 0; i < length; i++)
				{
					output.push_back(opt.processOutput[i]);
				}

				processing_vec.erase(processing_vec.begin(), processing_vec.end());
				if (yet_return_samples_vec.size() > 0) {
					processing_vec.insert(processing_vec.end(), yet_return_samples_vec[0].begin(), yet_return_samples_vec[0].end());
				}

				opt.processInput = processing_vec.data();
				opt.processFrames = ppOutputConnections[0]->u32ValidFrameCount;
				opt.unlock();

				if (yet_return_samples_vec.size() > 0) {
					SetEvent(hProcess);
					is_processing = TRUE;
				}

				is_done_10milli.push_back(TRUE);
			}
			if (processResult == WAIT_TIMEOUT)
			{
				//OutputDebugStringFW(L"[FiltergAPO] processResult == WAIT_TIMEOUT");
				is_done_10milli.push_back(FALSE);
			}
			if (processResult == WAIT_FAILED)
			{
				OutputDebugStringFW(L"[FiltergAPO] processResult == WAIT_FAILED");
			}


			if (is_done_10milli.size() > 500)
			{
				int true_count = 0;
				for (int i = 0; i < is_done_10milli.size(); i++)
				{
					if (is_done_10milli[i])
					{
						true_count++;
					}
				}
				OutputDebugStringFW(L"[FiltergAPO] is_done percent: %d percent", true_count * 100 / is_done_10milli.size());
				OutputDebugStringFW(L"[FiltergAPO] yet_return_vec.size(): %d", yet_return_samples_vec.size());
				is_done_10milli.erase(is_done_10milli.begin(), is_done_10milli.end());
			}

			// TIMEOUTしたとき
			if (output.size() == 0)
			{
				// 最初
				if (last_return_samples.size() == 0)
				{
					for (int i = 0; i < length; i++)
					{
						last_return_samples.push_back(0.0f);
					}
				}

				// 違和感ないようにつなげる(前後で断裂がないように中央が0みたいな感じの逆ハミング窓みたいなものをかける)
				for (int i = 0; i < length; i++)
				{
					output.push_back(inv_hamming(i / length) * last_return_samples[i]);
				}
			}

			last_return_samples.erase(last_return_samples.begin(), last_return_samples.end());
			for (int i = 0; i < length; i++)
			{
				outputFrames[i] = output[i];
				last_return_samples.push_back(output[i]);
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