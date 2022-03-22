#include "stdafx.h"
#include "debug.h"
#include "HANDLE_NAME.h"

#include "ProcessScheduler.h"

using namespace std;

// x = k / N (ハミング窓を逆にしたみたいな形のグラフ、適当)(maxが1でcos^2)
float inv_hamming(unsigned x) {
	return powf(cosf(2.0f * 3.14f * x), 2.0f);
}

HANDLE prepareFilterProcessThread(option* opt)
{
	HANDLE hInitialize = CreateEvent(NULL, FALSE, FALSE, InitializeHandleName);
	if (hInitialize == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hInitialize == NULL");
		return NULL;
	}
	HANDLE hProcessThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)asyncFilterProcess, opt, 0, NULL);

	DWORD initializeResult = WaitForSingleObject(hInitialize, 1000);
	CloseHandle(hInitialize);
	if (initializeResult == WAIT_OBJECT_0)
	{
		return hProcessThread;
	}
	if (initializeResult == WAIT_FAILED)
	{
		OutputDebugStringFW(L"[FiltergAPO] initializeResult == WAIT_FAILED");
	}
	return NULL;
}

DWORD WINAPI threadFunc(LPVOID* pData)
{
	option* opt = reinterpret_cast<option*>(pData);
	vector<vector<float>> yetProcessInputs;
	vector<vector<float>> alreadyProcessOutputs;
	vector<float> lastReturnOutput;

	vector<float> processingOutput;
	bool isProcessing = FALSE;

	vector<HANDLE> handles;

	HANDLE hTerminate = OpenEvent(EVENT_ALL_ACCESS, FALSE, TerminateSchedulerHandleName);
	if (hTerminate == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hTerminate == NULL");
		ExitThread(1);
	}
	handles.push_back(hTerminate);

	HANDLE hProcessSchedule = OpenEvent(EVENT_ALL_ACCESS, FALSE, ProcessScheduleHandleName);
	if (hProcessSchedule == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessSchedule == NULL");
		ExitThread(1);
	}
	handles.push_back(hProcessSchedule);

	HANDLE hProcess = CreateEvent(NULL, FALSE, FALSE, ProcessHandleName);
	if (hProcess == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcess == NULL");
		ExitThread(1);
	}

	HANDLE hProcessDone = CreateEvent(NULL, FALSE, FALSE, ProcessDoneHandleName);
	if (hProcessDone == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessDone == NULL");
		CloseHandle(hProcess);
		ExitThread(1);
	}
	handles.push_back(hProcessDone);

	HANDLE hProcessRequire = OpenEvent(EVENT_ALL_ACCESS, FALSE, ProcessRequireHandleName);
	if (hProcessRequire == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessRequire == NULL");
		ExitThread(1);
	}
	handles.push_back(hProcessRequire);

	HANDLE hProcessRequireSuccess = OpenEvent(EVENT_ALL_ACCESS, FALSE, ProcessRequireSuccessHandleName);
	if (hProcessRequireSuccess == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessRequireSuccess == NULL");
		ExitThread(1);
	}

	HANDLE hProcessScheduleSuccess = OpenEvent(EVENT_ALL_ACCESS, FALSE, ProcessScheduleSuccessHandleName);
	if (hProcessScheduleSuccess == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessScheduleSuccess == NULL");
		ExitThread(1);
	}

	option processOption;
	processOption.channels = opt->getChannels();
	processOption.m = CreateMutex(
		NULL,
		FALSE,
		NULL);

	HANDLE hProcessThread = prepareFilterProcessThread(&processOption);
	if (hProcessThread == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessThread == NULL");
		ExitThread(1);
	}
	
	while (true)
	{
		auto wait = WaitForMultipleObjects(handles.size(), handles.data(), FALSE, 10 * 1000);
		int length = opt->getLength();
		if (wait == WAIT_OBJECT_0 + 0)
		{
			// hTerminate
			OutputDebugStringFW(L"[FiltergAPO] From ProcessScheduler pull hTerminate.");
			break;
		}
		if (wait == WAIT_OBJECT_0 + 1)
		{
			// hProcessScheduler
			vector<float> queueing_input = opt->getInputFrames();
			yetProcessInputs.push_back(queueing_input);

			// queueing
			if (!isProcessing) {
				processOption.lock();
				processOption.channels = opt->channels;
				processOption.processFrames = opt->processFrames;
				processOption.processInput = yetProcessInputs[0].data();
				processingOutput.resize(length);
				processOption.processOutput = processingOutput.data();
				processOption.unlock();

				SetEvent(hProcess);
				isProcessing = TRUE;
			}
			SetEvent(hProcessScheduleSuccess);
		}
		if (wait == WAIT_OBJECT_0 + 2)
		{
			// hProcessDone
			vector<float> output;
			processOption.lock();

			isProcessing = FALSE;
			yetProcessInputs.erase(yetProcessInputs.begin(), yetProcessInputs.begin() + 1);
			// TODO: この境界条件は怪しい(フレーム数やチャネル数が変わるとバグる)
			for (int i = 0; i < length; i++)
			{
				output.push_back(processOption.processOutput[i]);
			}
			processOption.unlock();

			alreadyProcessOutputs.push_back(output);

			// queueing
			if (yetProcessInputs.size() > 0) {
				processOption.lock();
				processOption.channels = opt->channels;
				processOption.processFrames = opt->processFrames;
				processOption.processInput = yetProcessInputs[0].data();
				processingOutput.resize(length);
				processOption.processOutput = processingOutput.data();
				processOption.unlock();

				SetEvent(hProcess);
				isProcessing = TRUE;
			}
			OutputDebugStringFW(L"[FiltergAPO] fire hProcessDone");

		}
		if (wait == WAIT_OBJECT_0 + 3)
		{
			// hProcessRequire
			//opt->lock();
			if (alreadyProcessOutputs.size() > 0)
			{
				for (int i = 0; i < length; i++)
				{
					opt->processOutput[i] = alreadyProcessOutputs[0][i];
				}
				alreadyProcessOutputs.erase(alreadyProcessOutputs.begin(), alreadyProcessOutputs.begin() + 1);
			}
			else
			{
				if (lastReturnOutput.size() == 0)
				{
					for (int i = 0; i < length; i++)
					{
						opt->processOutput[i] = 0.0f;
					}
				}
				else
				{
					// 違和感ないようにつなげる(前後で断裂がないように中央が0みたいな感じの逆ハミング窓みたいなものをかける)
					for (int i = 0; i < length; i++)
					{
						opt->processOutput[i] = inv_hamming(i / length) * lastReturnOutput[i];
					}
				}
			}
			//opt->unlock();
			SetEvent(hProcessRequireSuccess);
		}
		if (wait == WAIT_TIMEOUT)
		{
			OutputDebugStringFW(L"[FiltergAPO] From ProcessScheduler timeout in waitloop.");
		}
		if (wait == WAIT_FAILED)
		{
			OutputDebugStringFW(L"[FiltergAPO] [ERROR] From ProcessScheduler WAIT_FAILED in wait loop.");
			break;
		}
	}

	CloseHandle(hProcess);
	CloseHandle(hProcessDone);

	ExitThread(0);
}

ProcessScheduler::ProcessScheduler(int channels)
{
	hTerminateScheduler = CreateEvent(NULL, FALSE, FALSE, TerminateSchedulerHandleName);
	if (hTerminateScheduler == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hTerminateScheduler == NULL");
	}

	hProcessSchedule = CreateEvent(NULL, FALSE, FALSE, ProcessScheduleHandleName);
	if (hProcessSchedule == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR ProcessScheduleHandleName == NULL");
	}

	hProcessScheduleSuccess = CreateEvent(NULL, FALSE, FALSE, ProcessScheduleSuccessHandleName);
	if (hProcessScheduleSuccess == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR ProcessScheduleSuccessHandleName == NULL");
	}

	hProcessRequire = CreateEvent(NULL, FALSE, FALSE, ProcessRequireHandleName);
	if (hProcessRequire == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessRequire == NULL");
	}

	hProcessRequireSuccess = CreateEvent(NULL, FALSE, FALSE, ProcessRequireSuccessHandleName);
	if (hProcessRequireSuccess == NULL)
	{
		OutputDebugStringFW(L"[FiltergAPO] ERROR hProcessRequireSuccess == NULL");
	}

	vector<float> threadOutput;

	option _opt;
	_opt.channels = channels;
	_opt.m = CreateMutex(
		NULL,
		FALSE,
		NULL);

	opt = _opt;

	// スレッド作る
	hScheduleThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)threadFunc, &opt, 0, NULL);
}

ProcessScheduler::~ProcessScheduler()
{
	if (hScheduleThread != NULL)
	{
		CloseHandle(hScheduleThread);
	}
	if (hProcessSchedule != NULL)
	{
		CloseHandle(hProcessSchedule);
	}
	if (hProcessScheduleSuccess != NULL)
	{
		CloseHandle(hProcessScheduleSuccess);
	}
	if (hProcessRequire != NULL)
	{
		CloseHandle(hProcessRequire);
	}
	if (hProcessRequireSuccess != NULL)
	{
		CloseHandle(hProcessRequireSuccess);
	}
}

void ProcessScheduler::UpdateChannels(int newChannels)
{
	opt.setChannels(newChannels);
}

void ProcessScheduler::ProcessFrame(float* input, int frames)
{
	vector<float> v;
	int length = frames * opt.getChannels();
	for (int i = 0; i < length; i++)
	{
		v.push_back(input[i]);
	}
	threadOutput.resize(length);
	opt.lock();
	opt.processInput = v.data();
	opt.processOutput = threadOutput.data();
	opt.processFrames = frames;
	opt.unlock();

	SetEvent(hProcessSchedule);

	auto wait = WaitForSingleObject(hProcessScheduleSuccess, 1000);
	return; // TODO: Error handling
}

void ProcessScheduler::GetProcessedFrames(float* output)
{
	int length = opt.getLength();
	opt.lock();
	for (int i = 0; i < length; i++)
	{
		output[i] = opt.processOutput[i];
	}
	opt.unlock();

	SetEvent(hProcessRequire);

	auto wait = WaitForSingleObject(hProcessRequireSuccess, 1000);
	return; // TODO: Error handling
}
