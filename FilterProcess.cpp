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
#define SAMPLE_RATE (48000.0f)

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

float norm_cpx(kiss_fft_cpx v) {
	return sqrtf(v.r * v.r + v.i * v.i);
}

float log_power_spectrum(kiss_fft_cpx v, int N)
{
	return log10f(v.r * v.r + v.i * v.i) / powf((float)N, 2);
}

pair<int, int> get_peak_and_foot_index(vector<float> v)
{
	int window_size = 5;
	for (int i = 0; i < v.size(); i++)
	{

	}
}

// x = k / (N - 1)
float hamming(unsigned x) {
	return 0.54f - 0.46f * cosf(2.0f * 3.14f * x);
}

FilterProcess::FilterProcess()
{
	this->loop = 0;
	this->channels = 0;

	this->spleeter_raw = vector<float>();
	this->spleeter_filtered_remain = vector<float>();
	this->process_times = vector<double>(); // debug
}

void FilterProcess::SetBandStopFilters(float freq, int target_chan)
{
	const float cutoff_frequency = 1000.0f;
	const float Q_factor = 0.539f;
	// const float Q_factor = 30.0f;
	
	Iir::RBJ::BandStop f;
	f.setup(SAMPLE_RATE, freq, Q_factor);

	filters[target_chan] = f;
	filter_freq[target_chan] = freq;
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
	filters = vector<Iir::RBJ::BandStop>(channels);
	filter_freq = vector<float>(channels);


	if (!InitializeSpleeterFilters())
	{
		OutputDebugStringFW(L"[FiltergAPO] [ERROR] From asyncFilterProcess InitializeSpleeterFilters is failed.");
		return FALSE;
	}

	return TRUE;
}

vector<float> FilterProcess::SpleeterProces()
{
	vector<float> inputFrames = opt->getInputFrames();
	vector<float> resultFrames = vector<float>(inputFrames.size());

	int frames = inputFrames.size() / channels;

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
		return resultFrames;
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

	for (int32_t sample_idx = 0;
		sample_idx < (int)transformed_frames.size() - multichannel_buffer_size;
		sample_idx += multichannel_buffer_size) {

		float* sample_ptr = transformed_frames.data() + (uint32_t)sample_idx;
		audio_buffer->fromInterleaved(sample_ptr);
		spleeter_filter->ProcessBlock(audio_buffer.get());
		audio_buffer->toInterleaved(sample_ptr);
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
	src_data_restore.data_out = resultFrames.data();
	src_data_restore.output_frames = frames;
	src_data_restore.src_ratio = 48000.0 / 44100.0;
	src_data_restore.end_of_input = 0;

	err = src_simple(&src_data_restore, SRC_SINC_FASTEST, 2);
	if (err != 0) {
		if (loop % 100 == 0) {
			OutputDebugStringFW(L"[FiltergAPO] src_process2 ERROR: %d, input_frames_used: %d.", err, src_data_restore.input_frames_used);
		}
		opt->unlock();
		return resultFrames;
	}
	opt->unlock();
	return resultFrames;
}

vector<float> FilterProcess::FilterPNoise(float* input)
{
	int frames = opt->getProcessFrames();
	int channels = opt->getChannels();
	vector<float> result(frames * channels);

	// まずFFTする
	// TODO: メンバ変数にする
	kiss_fft_cfg cfg_fft = kiss_fft_alloc(frames, FALSE, NULL, NULL);

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

				float re = input[i * channels + j];

				// 窓関数の係数(ハミング窓)
				float coefficient = hamming(i / (frames - 1));

				buffer_in[i].r = re * coefficient;
				buffer_in[i].i = 0.0f;
			}
		}

		kiss_fft(cfg_fft, &buffer_in[0], &buffer_out[0]);

		vector<float> log_power_in_freq = vector<float>();
		// 2で割ってるのはナイキスト周波数に対して対象だから
		for (unsigned i = 0; i < frames / 2; i++) {
			// TODO: 正規化？(する必要ないかも、しないならifftのとき要素数で割る)

			// 窓関数の補正値
			float window_correction = hamming(i / (frames - 1)) / frames;

			// 対数パワースペクトルを出す
			float power = norm_cpx(buffer_out[i]) * powf(window_correction, 1.0f);
			//float power = log_power_spectrum(buffer_out[i], frames / 2) * powf(window_correction, 2.0f);
			log_power_in_freq.push_back(power);
		}

		float log_power_sum = 0.0f;
		int log_power_max_index = 0;
		for (int i = 0; i < log_power_in_freq.size(); i++)
		{
			if (log_power_in_freq[i] > log_power_in_freq[log_power_max_index])
			{
				log_power_max_index = i;
			}
			log_power_sum += log_power_in_freq[i];
		}

		float target_freq = (float)log_power_max_index * SAMPLE_RATE / (float)frames;
		// 平均より10倍maxがこのときiir filterかける
		if (log_power_in_freq[log_power_max_index] > log_power_sum / log_power_in_freq.size() * 80)
		{
			// すでにこのチャネルでフィルタしてるとき
			if (filter_freq.size() > 0 && filter_freq[target_channel] > 1.0f)
			{
				// 100Hzの範囲内の時はフィルタそのまま
				if (target_freq < filter_freq[target_channel] + 100.0f && target_freq > filter_freq[target_channel] - 100.0f)
				{
					// そのまま
				}
				// 前後100Hzから外れた時はフィルタ作り替え
				else
				{
					SetBandStopFilters(target_freq, target_channel);
				}

			}
			// まだこのチャネルでフィルタしてないとき
			if (filter_freq.size() > 0 && filter_freq[target_channel] < 1.0f)
			{
				SetBandStopFilters(target_freq, target_channel);
			}

			OutputDebugStringFW(L"[FiltergAPO] filter freq: %g.", filter_freq[target_channel]);

		}
		// フィルタor素通り
		bool is_filter = filter_freq[target_channel] > 1.0f;
		for (unsigned i = 0; i < frames; i++)
		{
			for (unsigned j = 0; j < channels; j++)
			{
				if (j != target_channel) {
					continue;
				}

				float v = input[i * channels + j];
				if (is_filter)
				{
					result[i * channels + j] = filters[target_channel].filter(v);
				}
				else
				{
					result[i * channels + j] = v;
				}
			}
		}
	}
	kiss_fft_free(cfg_fft);

	return result;
}

void FilterProcess::Processing()
{
	loop++;

	UpdateChannel();

	vector<float> inputFrames = opt->getInputFrames();
	int frames = inputFrames.size() / channels;

	/*vector<float> pNoiseFilterd =  FilterPNoise(inputFrames.data());
	opt->lock();
	for (int i = 0; i < pNoiseFilterd.size(); i++) {
		opt->processOutput[i] = pNoiseFilterd[i];
	}
	opt->unlock();

	return;*/

	std::vector<float> content_copy;
	for (int i = 0; i < frames * channels; i++)
	{
		content_copy.push_back(inputFrames[i]);
	}

	opt->lock();
	for (int i = 0; i < content_copy.size(); i++) {
		opt->processOutput[i] = content_copy[i];
	}
	opt->unlock();
	return;
}

void FilterProcess::UpdateChannel()
{
	int new_chan = opt->getChannels();
	if (new_chan != channels)
	{
		filters = vector<Iir::RBJ::BandStop>(new_chan);
		filter_freq = vector<float>(new_chan);
	}
	channels = new_chan;
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
