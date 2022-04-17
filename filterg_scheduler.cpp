#include "stdafx.h"

#include "filterg_scheduler.h"

using std::vector;
using std::deque;

filterg_scheduler::filterg_scheduler()
	:executor(), yet_processed_frames(), processed_frames()
{}

filterg_scheduler::~filterg_scheduler()
{}

void filterg_scheduler::schedule_process(float* frames, int frame_count, int channel_count) {
	// 初めて処理するとき or チャネル数が変わったとき
	if (yet_processed_frames.size() == 0)
	{
		yet_processed_frames = vector<deque<float>>(channel_count, deque<float>(0));
		processed_frames = deque<float>(0);
	}

	for (int i = 0; i < frame_count; i++)
	{
		for (int j = 0; j < channel_count; j++)
		{
			yet_processed_frames[j].push_back(frames[i * channel_count + j]);
		}
	}

	// TODO: executorにタスクを投げる

	// TODO: frames_cache を破棄するタイミングをきちんと考える
	for (int i = 0; i < frame_count; i++)
	{
		for (int j = 0; j < channel_count; j++)
		{
			processed_frames.push_back(yet_processed_frames[j].front());
			yet_processed_frames[j].pop_front();
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
			frames[i * channel_count + j] = processed_frames.front();
			processed_frames.pop_front();
		}
	}

	return;
}
