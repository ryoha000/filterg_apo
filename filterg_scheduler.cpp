#include "stdafx.h"

#include "debug.h"
#include "filterg_scheduler.h"

constexpr int SAMPLE_RATE = 48000;
constexpr int KEYWORD_WINDOW_SIZE = SAMPLE_RATE * 0.625;
constexpr int KEYWORD_SHIFT_SIZE = SAMPLE_RATE * 0.1;
constexpr int ERASE_CACHE_THRESHOLD = SAMPLE_RATE * 15;
constexpr int ERASE_CACHE_REMAIN = SAMPLE_RATE * 5;


filterg_scheduler::filterg_scheduler()
	:executor(), cache_frames(), processed_frames(), keyword_infos()
{
	OutputDebugStringFW(L"[FiltergAPO] [INFO] initialize filterg_scheduler");
	keyword_model = std::make_shared<detector>();
}

filterg_scheduler::~filterg_scheduler()
{}

void filterg_scheduler::schedule_process(float* frames, int frame_count, int channel_count) {
	// 初めて処理するとき or チャネル数が変わったとき
	if (cache_frames.size() == 0 || channel_count != cache_frames.size())
	{
		cache_frames = vector<deque<float>>(channel_count, deque<float>(0));
		processed_frames = deque<float>(0);
	}

	for (int i = 0; i < frame_count; i++)
	{
		for (int j = 0; j < channel_count; j++)
		{
			cache_frames[j].push_back(frames[i * channel_count + j]);
		}
	}

	check_feature();
	submit_keyword_predict();
	erase_cache_frames();

	// TODO: cache_frames を破棄するタイミングをきちんと考える
	for (int i = 0; i < frame_count; i++)
	{
		for (int j = 0; j < channel_count; j++)
		{
			processed_frames.push_back(frames[i * channel_count + j]);
		}
	}

	return;
}


void filterg_scheduler::get_processed_frames(float* frames, int frame_count, int channel_count) {
	// TODO: futureとか見てどうするか決める
	for (int i = 0; i < frame_count; i++)
	{
		for (int j = 0; j < channel_count; j++)
		{
			// NOTE: 安全かわからない
			frames[i * channel_count + j] = processed_frames.front();
			processed_frames.pop_front();
		}
	}
	return;
}

void filterg_scheduler::erase_cache_frames()
{
	if (cache_frames.size() == 0)
	{
		return;
	}

	if (cache_frames[0].size() < ERASE_CACHE_THRESHOLD)
	{
		return;
	}

	int erase_count = cache_frames[0].size() - ERASE_CACHE_REMAIN;
	cache_frames[0].erase(cache_frames[0].begin(), cache_frames[0].begin() + erase_count);
	cache_frames[1].erase(cache_frames[1].begin(), cache_frames[1].begin() + erase_count);


	auto updated_keyword_infos = vector<keyword_info>();
	for (int i = 0; i < keyword_infos.size(); i++)
	{
		keyword_infos[i].start -= erase_count;
		keyword_infos[i].end -= erase_count;
		if (keyword_infos[i].end > 0)
		{
			updated_keyword_infos.push_back(keyword_infos[i]);
		}
	}
	keyword_infos = updated_keyword_infos;
}

int detect(detector* model, vector<float>* frames) {
	model->set_frames(frames);
	return model->run();
}

int for_debug(int res) {
	return res;
}

void filterg_scheduler::submit_keyword_predict()
{
	if (cache_frames.size() == 0)
	{
		return;
	}

	// 前回から十分な間隔があいているか
	int last_predict_start = 0;
	if (keyword_infos.size() > 0)
	{
		last_predict_start = keyword_infos.back().start;
	}
	int next_predict_start = last_predict_start + KEYWORD_SHIFT_SIZE;
	if (int(cache_frames[0].size()) - next_predict_start < KEYWORD_WINDOW_SIZE)
	{
		return;
	}

	int next_predict_end = next_predict_start + KEYWORD_WINDOW_SIZE; // end は開区間
	queue_detect(next_predict_start, next_predict_end);
	return;
}

void filterg_scheduler::queue_detect(int start, int end)
{
	auto next_frames = vector<vector<float>>();
	for (int chan = 0; chan < cache_frames.size(); chan++)
	{
		next_frames.push_back(vector<float>());
		for (int cache_index = start; cache_index < end; cache_index++)
		{
			next_frames[chan].push_back(cache_frames[chan][cache_index]);
		}
		// NOTE: 1chan以外でも検出するようにするなら消す
		break;
	}

	auto info = keyword_info(start, end);
	if (!is_detecting()) {
		keyword_future = std::make_shared<std::future<int>>(executor.submit(detect, keyword_model.get(), &next_frames[0]));
	}
	keyword_infos.push_back(info);

	return;
}

bool filterg_scheduler::is_detecting()
{
	bool res = false;
	for (const auto& v : keyword_infos) {
		if (v.label < 0) {
			res = true;
		}
	}

	return res;
}

void filterg_scheduler::check_feature()
{
	if (!is_detecting()) {
		return;
	}
	auto result = keyword_future->wait_for(std::chrono::milliseconds(0));
	if (result == std::future_status::ready)
	{
		int label = keyword_future->get();
		int detecting_index = 0;
		for (int i = 0; i < keyword_infos.size(); i++) {
			if (keyword_infos[i].label < 0) {
				detecting_index = i;
				break;
			}
		}
		keyword_infos[detecting_index].label = label;
		if (label != 0) {
			// TODO: labelもらった時の処理をちゃんと書く
			float before_second = ((float)cache_frames[0].size() - (float)keyword_infos[detecting_index].start) / (float)SAMPLE_RATE;
			OutputDebugStringFW(L"[FiltergAPO] keyword detected! label: %d, time: before %g ms", label, before_second);
		}
	}
}
