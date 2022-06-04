#include "pch.h"
#include "framework.h"

#include "keyword_detector.h"
#include <samplerate.h>
#include <librosapp.hpp>

keyword_detector::detector::detector() {
	auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
	input_tensor_ = Ort::Value::CreateTensor(memory_info, input_spec_.data(), input_spec_.size(), input_shape_.data(), input_shape_.size());
	output_tensor_ = Ort::Value::CreateTensor(memory_info, results_.data(), results_.size(), output_shape_.data(), output_shape_.size());
}

keyword_detector::detector::~detector() {}

int keyword_detector::detector::run() {
	session_.Run(Ort::RunOptions{ nullptr }, input_names, &input_tensor_, 1, output_names, &output_tensor_, 1);

	result_ = std::distance(results_.begin(), std::max_element(results_.begin(), results_.end()));
	return result_;
}

vector<vector<float>> transpose(vector<vector<float>> v) {
	if (v.size() == 0) {
		vector<vector<float>> res(0, vector<float>());
		return res;
	}

	vector<vector<float>> res(v[0].size(), vector<float>());
	for (int i = 0; i < v.size(); i++) {
		for (int j = 0; j < v[i].size(); j++) {
			res[j].push_back(v[i][j]);
		}
	}

	return res;
}

int keyword_detector::detector::set_frames(vector<float> frames) {
	// http://libsndfile.github.io/libsamplerate/api_full.html
	std::vector<float> transformed_frames(10000);
	SRC_DATA src_data;
	src_data.data_in = frames.data();
	src_data.input_frames = frames.size();
	src_data.data_out = transformed_frames.data();
	src_data.output_frames = 4096 / 2;
	src_data.src_ratio = 16000.0 / 48000.0;
	src_data.end_of_input = 0;

	int err = src_simple(&src_data, SRC_SINC_FASTEST, 2);
	if (err != 0) {
		// TODO: エラー処理
		return 0;
	}

	librosa::feature::melspectrogram_arg arg;
	arg.y = transformed_frames;
	arg.n_fft = 512;
	arg.n_mels = 60;
	arg.hop_length = 128;
	arg.sr = 16000;
	auto melspec = librosa::feature::melspectrogram(&arg);

	auto transposed = transpose(melspec);

	for (int i = 0; i < transposed.size(); i++)
	{
		for (int j = 0; j < transposed[i].size(); j++)
		{
			input_spec_[i * transposed[i].size() + j] = transposed[i][j];
		}
	}
	return 1;
}

