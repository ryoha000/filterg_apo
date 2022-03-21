#pragma once
#include <vector>
#include <stdio.h>
#include <Windows.h>

#include "Iir.h"
#include "spleeter_filter/filter.h"
#include "rtff/buffer/audio_buffer.h"

using namespace std;

DWORD WINAPI asyncFilterProcess(LPVOID* data);

struct option {
	HANDLE m;
	int channels;
	float* processInput;
	float* processOutput;
	int processFrames;

public:
	int getChannels();
	void setChannels(int c);
	int getProcessFrames();
	void getInputFrames(vector<float>* v);
	void lock();
	void unlock();
};

class FilterProcess
{
public:
	FilterProcess();
	bool Initialize(option* opt);
	void Processing();
	void InitializeBandStopFilters();
	bool InitializeSpleeterFilters();
	void UpdateChannel();

private:
	vector<Iir::RBJ::BandStop> filters;

	std::shared_ptr<spleeter::Filter> spleeter_filter;
	std::shared_ptr<rtff::AudioBuffer> audio_buffer;

	vector<float> spleeter_filtered_remain;
	vector<float> spleeter_raw;

	vector<double> process_times; // debug
	int loop; // debug
	int channels;

	option* opt;
};
