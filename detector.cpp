#include "stdafx.h"
#include "detector.h"
#include "torch/script.h"
#include "librosa/librosa.h"
#include "debug.h"
#include "samplerate.h"


constexpr int SAMPLE_RATE = 48000;
constexpr int RESAMPLE_RATE = 16000;
constexpr int UNKNOWN_LABEL_INDEX = 1;
constexpr double THRESHOLD_DIFF_TO_UNKNOWN = 10.0;


torch::jit::Module initialize_detector()
{
	return torch::jit::load("C:\Program Files\FiltergDebug\models\wewks_model.zip");
}

SRC_DATA get_resample_src(vector<float> frames, vector<float> resample_frames, int from, int to) {
	SRC_DATA src_data;
	src_data.data_in = frames.data();
	src_data.input_frames = frames.size();
	src_data.data_out = resample_frames.data();
	src_data.output_frames = resample_frames.size();
	src_data.src_ratio = to / from;
	src_data.end_of_input = 0;

	return src_data;
}

int keyword_detect(torch::jit::Module model, vector<vector<float>> frames) {
	//if (frames.size() == 0) {
	//	OutputDebugStringFW(L"[FiltergAPO] [ERROR] frames.size() == 0 in keyword_detect");
	//	return 0;
	//}

	//for (int i = 0; i < frames[0].size(); i++)
	//{
	//	frames[0][i] = frames[0][i] * (1 << 15);
	//}

	//auto downsampled_frames = vector<float>(frames[0].size() * RESAMPLE_RATE / SAMPLE_RATE);
	//auto downsample_src = get_resample_src(frames[0], downsampled_frames, SAMPLE_RATE, RESAMPLE_RATE);

	//// TODO: resample
	//int err = src_simple(&downsample_src, SRC_SINC_FASTEST, 2);
	//if (err != 0) {
	//	OutputDebugStringFW(L"[FiltergAPO] [ERROR] src_process ERROR: %d, input_frames_used: %d.", err, downsample_src.input_frames_used);
	//	return 0;
	//}

	//vector<vector<float>> mfcc = librosa::Feature::mfcc(
	//	downsampled_frames,
	//	16000,
	//	400,
	//	160,
	//	"hann",
	//	TRUE,
	//	"reflect",
	//	2.0,
	//	80,
	//	20,
	//	8000,
	//	80,
	//	TRUE,
	//	2);

	//vector<torch::jit::IValue> inputs;
	//auto input_tensor = torch::zeros({ 1, (int)mfcc.size(), (int)mfcc[0].size() });
	//for (int i = 0; i < mfcc.size(); i++)
	//{
	//	for (int j = 0; j < mfcc[0].size(); j++)
	//	{
	//		input_tensor[0][i][j] = mfcc[i][j];
	//	}
	//}
	//inputs.push_back(input_tensor);

	//auto outputs = model(inputs).toTensor();
	//auto eval_label = torch::argmax(outputs[0]).item().to<int>();
	//auto unknown_value = outputs[0][UNKNOWN_LABEL_INDEX].item().to<double>();

	//if (outputs[0][eval_label].item().to<double>() > unknown_value + THRESHOLD_DIFF_TO_UNKNOWN) {
	//	return eval_label;
	//}

	return UNKNOWN_LABEL_INDEX;
}
