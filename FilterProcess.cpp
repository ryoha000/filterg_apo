#include "stdafx.h"
#include "debug.h"
#include "FilterProcess.h"
#include "HANDLE_NAME.h"

#include <windows.h>

#include <kiss_fft.h>

// spleeter
#include <tensorflow/c/c_api.h>
#include "spleeter_common/spleeter_common.h"
#include "spleeter/spleeter.h"
#include "samplerate.h"

// debug
#include <numeric>
#include <algorithm>

#define SPLEETER_BLOCK_SIZE (256);

using namespace std;

DWORD WINAPI asyncFilterProcess(LPVOID* pData)
{
	bool result;
	vector<HANDLE> handles;

	option* opt = reinterpret_cast<option*>(pData);

	FilterProcess filterProcess;
	result = filterProcess.Initialize(opt);
	if (!result)
	{
		ExitThread(1);
	}

	HANDLE hInitialize = OpenEvent(EVENT_ALL_ACCESS, FALSE, InitializeHandleName);
	if (hInitialize == NULL)
	{
		ExitThread(1);
	}

	result = SetEvent(hInitialize);
	if (!result) {
		ExitThread(1);
	}

	HANDLE hTerminate = OpenEvent(EVENT_ALL_ACCESS, FALSE, TerminateHandleName);
	if (hTerminate == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] From asyncFilterProcess hTerminate == NULL.");
		ExitThread(1);
	}
	handles.push_back(hTerminate);

	HANDLE hProcess = OpenEvent(EVENT_ALL_ACCESS, FALSE, ProcessHandleName);
	if (hProcess == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] From asyncFilterProcess hProcess == NULL.");
		ExitThread(1);
	}
	handles.push_back(hProcess);

	HANDLE hUpdateChannel = OpenEvent(EVENT_ALL_ACCESS, FALSE, UpdateChannelsHandleName);
	if (hUpdateChannel == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] From asyncFilterProcess hUpdateChannel == NULL.");
		ExitThread(1);
	}
	handles.push_back(hProcess);

	HANDLE hProcessDone = OpenEvent(EVENT_ALL_ACCESS, FALSE, ProcessDoneHandleName);
	if (hProcessDone == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] From asyncFilterProcess hProcessDone == NULL.");
		ExitThread(1);
	}

	while (true)
	{
		DWORD wait = WaitForMultipleObjects(handles.size(), handles.data(), FALSE, 10000);

		if (wait == WAIT_OBJECT_0 + 0)
		{
			OutputDebugStringFW(L"[FiltergAPO] From asyncFilterProcess pull hTerminate.");
			break;
		}
		if (wait == WAIT_OBJECT_0 + 1)
		{
			filterProcess.Processing();
			SetEvent(hProcessDone);
		}
		if (wait == WAIT_OBJECT_0 + 2)
		{
			OutputDebugStringFW(L"[FiltergAPO] From asyncFilterProcess pull hUpdateChannel.");
			filterProcess.UpdateChannel();
		}
		if (wait == WAIT_TIMEOUT)
		{
			OutputDebugStringFW(L"[FiltergAPO] From asyncFilterProcess timeout in waitloop.");
		}
		if (wait == WAIT_FAILED)
		{
			OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess WAIT_FAILED in wait loop.");
			break;
		}
	}

	ExitThread(0);
}

FilterProcess::FilterProcess()
{
	this->loop = 0;
	this->channels = 0;

	this->spleeter_raw = vector<float>();
	this->spleeter_filtered_remain = vector<float>();
	this->process_times = vector<double>(); // debug
}

void FilterProcess::InitializeBandStopFilters()
{
	const float cutoff_frequency = 1000.0f;
	const float samplerate = 48000.0f;
	//const float Q_factor = 0.539f;
	const float Q_factor = 30.0f;

	vector<Iir::RBJ::BandStop> _filters;

	for (int i = 0; i < channels; i++)
	{
		Iir::RBJ::BandStop f;
		f.setup(48000.0, cutoff_frequency, Q_factor);
		_filters.push_back(f);
	}

	filters = _filters;
	return;
}

bool FilterProcess::InitializeSpleeterFilters()
{
	std::error_code err;

	const auto separation_type = spleeter::TwoStems;

	// TODO: パスをちゃんと指定する
	spleeter::Initialize(std::string("C:\\Program Files\\FiltergDebug\\models\\filter"), { separation_type }, err);

	if (err.value() != 0)
	{
		OutputDebugStringFW(L"[FiltergAPO] spleeter Initialize error value: %d, error message: %s.", err.value(), err.message());
		return FALSE;
	}

	spleeter_filter = std::make_shared<spleeter::Filter>(separation_type);
	spleeter_filter->Init(err);

	if (err.value() != 0)
	{
		OutputDebugStringFW(L"[FiltergAPO] spleeter Initialize error value: %d, error message: %s.", err.value(), err.message());
		return FALSE;
	}

	// voice only にしてる
	spleeter_filter->set_volume(0, 1.0);
	spleeter_filter->set_volume(1, 0.0);

	const auto block_size = SPLEETER_BLOCK_SIZE;
	spleeter_filter->set_block_size(block_size);

	audio_buffer = std::make_shared<rtff::AudioBuffer>(block_size, spleeter_filter->channel_count());

	return TRUE;
}

bool FilterProcess::Initialize(option* _opt)
{
	opt = _opt;

	OutputDebugStringFW(L"[FiltergAPO] From asyncFilterProcess FilterProcess::Initialize().");

	channels = opt->getChannels();
	if (channels != 0)
	{
		InitializeBandStopFilters();
		return FALSE;
	}


	if (!InitializeSpleeterFilters())
	{
		OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess InitializeSpleeterFilters is failed.");
		return FALSE;
	}

	return TRUE;
}

float norm_cpx(kiss_fft_cpx v) {
	return sqrtf(v.r * v.r + v.i * v.i);
}

// x = k / (N - 1)
float hamming(unsigned x) {
	return 0.54f - 0.46f * cosf(2.0f * 3.14f * x);
}

void FilterProcess::Processing()
{
	loop++;

	channels = opt->getChannels();
	if (channels == 0)
	{
		if (loop % 100 == 0) {
			OutputDebugStringFW(L"[FiltergAPO] channels is zero.");
		}
		return;
	}

	vector<float> inputFrames = opt->getInputFrames();
	int frames = inputFrames.size() / channels;


	//return;

	// TODO: samplerateが44100ならlibsamplerate入らないように
	if (true) {
		std::vector<float> content_copy;
		for (int i = 0; i < frames * channels; i++)
		{
			content_copy.push_back(inputFrames[i]);
		}

		// http://libsndfile.github.io/libsamplerate/api_full.html
		std::vector<float> transformed_frames(4096);
		SRC_DATA src_data;
		src_data.data_in = inputFrames.data();
		src_data.input_frames = frames;
		src_data.data_out = transformed_frames.data();
		src_data.output_frames = 4096 / 2;
		src_data.src_ratio = 44100.0 / 48000.0;
		src_data.end_of_input = 0;

		int err = src_simple(&src_data, SRC_SINC_FASTEST, 2);
		if (err != 0) {
			if (loop % 100 == 0) {
				OutputDebugStringFW(L"[FiltergAPO] src_process ERROR: %d, input_frames_used: %d.", err, src_data.input_frames_used);
			}
			return;
		}

		transformed_frames.erase(transformed_frames.begin() + (long)channels * src_data.output_frames_gen, transformed_frames.end());

		// 未処理のフレームを頭に挿入する
		transformed_frames.insert(transformed_frames.begin(), spleeter_raw.begin(), spleeter_raw.end());

		int multichannel_buffer_size = audio_buffer->channel_count() * SPLEETER_BLOCK_SIZE;

		// 今回処理できないフレーム
		// TODO: どうせspleeter_filter->ProcessBlockで触られないしコピーしないでよさそう
		std::vector<float> remain_raw_44100_frames;
		unsigned remain_length = transformed_frames.size() % multichannel_buffer_size;
		std::copy(transformed_frames.begin() + transformed_frames.size() - remain_length, transformed_frames.end(), std::back_inserter(remain_raw_44100_frames));

		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);

		LARGE_INTEGER start;
		QueryPerformanceCounter(&start);

		for (int32_t sample_idx = 0;
			sample_idx < (int)transformed_frames.size() - multichannel_buffer_size;
			sample_idx += multichannel_buffer_size) {

			float* sample_ptr = transformed_frames.data() + (uint32_t)sample_idx;
			audio_buffer->fromInterleaved(sample_ptr);
			spleeter_filter->ProcessBlock(audio_buffer.get());
			audio_buffer->toInterleaved(sample_ptr);
		}

		LARGE_INTEGER end;
		QueryPerformanceCounter(&end);

		process_times.push_back((double)(end.QuadPart - start.QuadPart) / frequency.QuadPart);
		if (process_times.size() > 2000)
		{
			double sum = std::accumulate(process_times.begin(), process_times.end(), 0);
			double max = *max_element(process_times.begin(), process_times.end());
			for (int i = 0; i < 5; i++)
			{
				OutputDebugStringFW(L"[FiltergAPO] -----------------------------------------------------------.");
			}
			OutputDebugStringFW(L"[FiltergAPO] PROCESS TIME avg: %g, max: %g.", sum / (double)(process_times.size()), max);
			for (int i = 0; i < 5; i++)
			{
				OutputDebugStringFW(L"[FiltergAPO] -----------------------------------------------------------.");
			}
			process_times.erase(process_times.begin(), process_times.end());
		}

		// 多分最初だけ
		auto return_length = src_data.output_frames_gen * audio_buffer->channel_count();
		if (transformed_frames.size() - return_length - remain_length < 0)
		{
			// 処理できなかった分だけ頭を0でうめる
			transformed_frames.insert(transformed_frames.begin(), remain_length, 0.0f);
			// OutputDebugStringFW(L"[FiltergAPO] fill 0.0f in head of %d frames.", remain_length);
		}

		spleeter_raw.clear();
		// 今回処理できなかったフレームを保持
		spleeter_raw.insert(spleeter_raw.end(), remain_raw_44100_frames.begin(), remain_raw_44100_frames.end());

		// 過去に処理できた分を頭につける
		transformed_frames.insert(transformed_frames.begin(), spleeter_filtered_remain.begin(), spleeter_filtered_remain.end());
		spleeter_filtered_remain.clear();

		// 今回のAPOProcessで返せない分のデータを保持
		if (return_length + remain_length < transformed_frames.size()) {
			spleeter_filtered_remain.insert(spleeter_filtered_remain.end(), transformed_frames.begin() + return_length, transformed_frames.end() - remain_length);
			if (loop % 1000 == 0) {
				OutputDebugStringFW(L"[FiltergAPO] transformed_frames: %d, return_length: %d.", transformed_frames.size(), return_length);
			}
		}

		opt->lock();
		SRC_DATA src_data_restore;
		src_data_restore.data_in = &transformed_frames[0];
		src_data_restore.input_frames = src_data.output_frames_gen;
		src_data_restore.data_out = opt->processOutput;
		src_data_restore.output_frames = frames;
		src_data_restore.src_ratio = 48000.0 / 44100.0;
		src_data_restore.end_of_input = 0;

		err = src_simple(&src_data_restore, SRC_SINC_FASTEST, 2);
		if (err != 0) {
			if (loop % 100 == 0) {
				OutputDebugStringFW(L"[FiltergAPO] src_process2 ERROR: %d, input_frames_used: %d.", err, src_data_restore.input_frames_used);
			}
			return;
		}

		for (int i = 0; i < content_copy.size(); i++) {
			opt->processOutput[i] = content_copy[i];
		}
		opt->unlock();

		return;
	}

	// iir(biquad)
	if (true) {
		for (unsigned target_channel = 0; target_channel < channels; target_channel++)
		{
			for (unsigned i = 0; i < frames; i++)
			{
				for (unsigned j = 0; j < channels; j++)
				{
					if (j != target_channel) {
						continue;
					}

					// float v = content[i * channels + j];
					// result[i * channels + j] = filters[target_channel].filter(v);
				}
			}
		}
	}

	// fft
	//if (loop % 100 == 0)
	if (false)
	{
		OutputDebugStringFW(L"[FiltergAPO] frames: %d, channels: %d..", frames, channels);


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

					float re = inputFrames[i * channels + j];

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
			float norm_top[3] = { 0.0f, 0.0f, 0.0f };
			unsigned norm_top_index[3] = { 0, 0, 0 };
			// 2で割ってるのはナイキスト周波数に対して対象だから
			for (unsigned i = 0; i < frames / 2; i++) {
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
				}
				else if (norm_top[1] < norm) {
					norm_top[2] = norm_top[1];
					norm_top[1] = norm;

					norm_top_index[2] = norm_top_index[1];
					norm_top_index[1] = i;
				}
				else if (norm_top[2] < norm) {
					norm_top[2] = norm;

					norm_top_index[2] = i;
				}
			}

			for (unsigned i = 0; i < 3; i++) {
				//auto target = norm_top_index[i];
				unsigned target = 9 + i;
				if (target < 1) {
					continue;
				}

				buffer_out[target].r = 0.0f;
				buffer_out[target].i = 0.0f;

				// なんか波数0はナイキスト周波数に対称にならない？
				if (frames % 2 != 0) {
					target = 2 * ((frames - 1) / 2) - target + 1;
				}
				else {
					target = 2 * ((frames - 1) / 2 + 1) - target;
				}

				buffer_out[target].r = 0.0f;
				buffer_out[target].i = 0.0f;
			}

			/*auto target = 10;

			buffer_out[target].r = 0.5;
			buffer_out[target].i = 0.0;

			target = 470;

			buffer_out[target].r = 0.5;
			buffer_out[target].i = 0.0;*/

			kiss_fft(cfg_ifft, &buffer_out[0], &buffer_in[0]);

			// 結果を返す
			for (unsigned i = 0; i < frames; i++)
			{
				for (unsigned j = 0; j < channels; j++)
				{
					if (j != target_channel) {
						continue;
					}

					// 窓関数の係数(ハミング窓)(最初はこれをかけたから今回はこれで割る)
					float coefficient = hamming(i / (frames - 1));

					// result[i * channels + j] = buffer_in[i].r / coefficient/ frames;
				}
			}
		}

		kiss_fft_free(cfg_fft);
		kiss_fft_free(cfg_ifft);
	}
}

void FilterProcess::UpdateChannel()
{
	channels = opt->getChannels();
}

void option::setChannels(int c)
{
	DWORD dwWaitResult;

	dwWaitResult = WaitForSingleObject(m, INFINITE);

	switch (dwWaitResult)
	{
	case WAIT_OBJECT_0:
		__try {
			this->channels = c;
		}

		__finally {
			if (!ReleaseMutex(m))
			{
				OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::setChannels() !ReleaseMutex(m).");
			}
		}
		break;

	case WAIT_ABANDONED:
		OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::setChannels() WAIT_ABANDONED.");
	}

	return;
}

int option::getChannels()
{
	DWORD dwWaitResult;
	int c = 0;

	dwWaitResult = WaitForSingleObject(m, INFINITE);

	switch (dwWaitResult)
	{
	case WAIT_OBJECT_0:
		__try {
			c = this->channels;
		}

		__finally {
			if (!ReleaseMutex(m))
			{
				OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::getChannels() !ReleaseMutex(m).");
			}
		}
		break;

	case WAIT_ABANDONED:
		OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::getChannels() WAIT_ABANDONED.");
	}

	return c;
}

int option::getProcessFrames()
{
	DWORD dwWaitResult;
	int c = 0;

	dwWaitResult = WaitForSingleObject(m, INFINITE);

	switch (dwWaitResult)
	{
	case WAIT_OBJECT_0:
		__try {
			c = processFrames;
		}

		__finally {
			if (!ReleaseMutex(m))
			{
				OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::getChannels() !ReleaseMutex(m).");
			}
		}
		break;

	case WAIT_ABANDONED:
		OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::getChannels() WAIT_ABANDONED.");
	}

	return c;
}

vector<float> option::getInputFrames()
{
	DWORD dwWaitResult;
	vector<float> res;
	int length = 0;
	dwWaitResult = WaitForSingleObject(m, INFINITE);

	switch (dwWaitResult)
	{
	case WAIT_OBJECT_0:
		length = this->processFrames * this->channels;
		// TODO: どうしても気になるならここのcopyを消してmutexを長くとる
		for (int i = 0; i < length; i++)
		{
			res.push_back(processInput[i]);
		}

		if (!ReleaseMutex(m))
		{
			OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::getInputFrames() !ReleaseMutex(m).");
		}
		break;

	case WAIT_ABANDONED:
		OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::getInputFrames() WAIT_ABANDONED.");
	}

	return res;
}

void option::lock()
{
	DWORD dwWaitResult;

	dwWaitResult = WaitForSingleObject(m, INFINITE);

	switch (dwWaitResult)
	{
	case WAIT_OBJECT_0:
		return;

	case WAIT_ABANDONED:
		OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::lock() WAIT_ABANDONED.");
	}

	return;
}

void option::unlock()
{
	if (!ReleaseMutex(m))
	{
		OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess option::unlock() !ReleaseMutex(m).");
	}

	return;
}
