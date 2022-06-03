#include "stdafx.h"

#include "debug.h"
#include "filterg_scheduler.h"

constexpr int SAMPLE_RATE = 48000;
constexpr int KEYWORD_WINDOW_SIZE = SAMPLE_RATE * 0.75;
constexpr int KEYWORD_SHIFT_SIZE = SAMPLE_RATE * 0.1;
constexpr int ERASE_CACHE_THRESHOLD = SAMPLE_RATE * 15;
constexpr int ERASE_CACHE_REMAIN = SAMPLE_RATE * 5;


filterg_scheduler::filterg_scheduler()
	:executor(), cache_frames(), processed_frames(), keyword_futures(), keyword_models(), keyword_infos()
{
	OutputDebugStringFW(L"[FiltergAPO] [INFO] initialize filterg_scheduler");
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

	int erase_keyword = keyword_futures.size() - keyword_infos.size();
	for (int i = 0; i < erase_keyword; i++)
	{
		keyword_futures.pop_front();
	}
}

int detect(Wekws* model, vector<float> frames) {
	// TODO: resample

	// TODO: melspec
	model->set_melspec(melspec);
	return model->run();
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

	int next_predict_end = next_predict_start + KEYWORD_WINDOW_SIZE;
	auto info = keyword_info(next_predict_start, next_predict_end, get_available_model_index()); // end は開区間

	auto next_frames = vector<vector<float>>();
	for (int chan = 0; chan < cache_frames.size(); chan++)
	{
		next_frames.push_back(vector<float>());
		for (int cache_index = next_predict_start; cache_index < next_predict_end; cache_index++)
		{
			next_frames[chan].push_back(cache_frames[chan][cache_index]);
		}
		// NOTE: 1chan以外でも検出するようにするなら消す
		break;
	}

	Wekws* model = keyword_models[info.detector_index];

	keyword_futures.push_back(executor.submit(detect, model, next_frames[0]));
	keyword_infos.push_back(info);

	return;
}

// 使えるモデルの選択
int filterg_scheduler::get_available_model_index()
{
	for (int model_index = 0; model_index < keyword_models.size(); model_index++)
	{
		bool is_used = false;
		for (int info_index = 0; info_index < keyword_infos.size(); info_index++)
		{
			if (model_index == keyword_infos[info_index].detector_index)
			{
				is_used = true;
				break;
			}
		}

		if (!is_used)
		{
			return model_index;
		}
	}

	OutputDebugStringFW(L"[FiltergAPO] [INFO] call create_detector_fn. size: %d", keyword_models.size());
	Wekws new_model;
	keyword_models.push_back(&new_model);
	OutputDebugStringFW(L"[FiltergAPO] [INFO] called create_detector_fn. size: %d", keyword_models.size());
	return keyword_models.size() - 1;
}

void filterg_scheduler::check_feature()
{
	for (int i = 0; i < keyword_futures.size(); i++)
	{
		if (keyword_infos[i].detector_index == -1)
		{
			continue;
		}
		auto result = keyword_futures[i].wait_for(std::chrono::milliseconds(0));
		if (result == std::future_status::ready)
		{
			int label = keyword_futures[i].get();
			if (label != 0)
			{
				if (cache_frames.size() == 0)
				{
					continue;
				}
				// TODO: labelもらった時の処理をちゃんと書く
				float before_second = ((float)cache_frames[0].size() - (float)keyword_infos[i].start) / (float)SAMPLE_RATE;
				OutputDebugStringFW(L"[FiltergAPO] keyword detected! label: %d, time: before %g ms", label, before_second);
			}
			else {
				OutputDebugStringFW(L"[FiltergAPO] keyword not detected! label: %d", label);
			}
			keyword_infos[i].detector_index = -1;
			keyword_infos[i].label = label;
		}
	}
}
