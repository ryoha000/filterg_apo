#pragma once
#include <vector>
#include <Windows.h>
#include "FilterProcess.h"

DWORD WINAPI threadFunc(LPVOID* pData);

class ProcessScheduler
{
public:
	ProcessScheduler(int channels);
	~ProcessScheduler();
	void UpdateChannels(int newChannels);
	void GetProcessedFrames(float* output);
	void ProcessFrame(float* input, int frames);

private:

	option opt;
	vector<float> threadOutput;

	HANDLE hScheduleThread;
	HANDLE hProcessSchedule;
	HANDLE hProcessScheduleSuccess;
	HANDLE hProcessRequire;
	HANDLE hProcessRequireSuccess;

	HANDLE hTerminateScheduler;
};
