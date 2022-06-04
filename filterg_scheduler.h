#pragma once
#include "thread_pool.hpp"

#include <keyword_detector.h>

#include <vector>
#include <future>

using std::vector;
using std::deque;

using keyword_detector::detector;

struct keyword_info
{
	int label;
	int start; // Ç±ÇÃ keyword_info Ç™äJénÇ∑ÇÈ cache_frames Ç…ëŒÇ∑ÇÈ index
	int end; // Ç±ÇÃ keyword_info Ç™èIóπÇ∑ÇÈ cache_frames Ç…ëŒÇ∑ÇÈ index + 1
	int detector_index;

	keyword_info(int start_, int end_, int model_index_) :label(-1), start(0), end(0), detector_index(-1)
	{
		start = start_;
		end = end_;
		detector_index = model_index_;
	}
};

class filterg_scheduler
{
public:
	filterg_scheduler();
	~filterg_scheduler();

	void schedule_process(float* frames, int frame_count, int channel_count);
	void get_processed_frames(float* frames, int frame_count, int channel_count);

private:
	thread_pool executor;

	vector<deque<float>> cache_frames;
	deque<float> processed_frames;

	vector<detector*> keyword_models;
	deque<std::future<int>> keyword_futures;
	vector<keyword_info> keyword_infos;

	void submit_keyword_predict();
	void erase_cache_frames();
	int get_available_model_index();
	void check_feature();
};
