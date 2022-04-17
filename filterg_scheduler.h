#pragma once
#include "thread_pool.hpp"
#include "vector"

class filterg_scheduler
{
public:
	filterg_scheduler();
	~filterg_scheduler();

	void schedule_process(float* frames, int frame_count, int channel_count);
	void get_processed_frames(float* frames, int frame_count, int channel_count);

private:
	thread_pool executor;
	std::vector<std::deque<float>> yet_processed_frames;
	std::deque<float> processed_frames;
};
