#pragma once
#include "thread_pool.hpp"

#include <keyword_detector.h>

#include <vector>
#include <future>
#include <memory>

using std::vector;
using std::deque;

using keyword_detector::detector;

struct keyword_info
{
	int label;
	int start; // ‚±‚Ì keyword_info ‚ªŠJn‚·‚é cache_frames ‚É‘Î‚·‚é index
	int end; // ‚±‚Ì keyword_info ‚ªI—¹‚·‚é cache_frames ‚É‘Î‚·‚é index + 1

	keyword_info(int start_, int end_) :label(-1), start(0), end(0)
	{
		start = start_;
		end = end_;
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

	std::shared_ptr<detector> keyword_model;
	std::shared_ptr<std::future<int>> keyword_future;
	vector<keyword_info> keyword_infos;

	void submit_keyword_predict();
	void erase_cache_frames();
	void queue_detect(int start, int end);
	bool is_detecting();
	void check_feature();
};
