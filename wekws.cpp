#include "wekws.h"

Wekws::Wekws() {
	auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
	input_tensor_ = Ort::Value::CreateTensor<float>(memory_info, input_spec_.data(), input_spec_.size(), input_shape_.data(), input_shape_.size());
	output_tensor_ = Ort::Value::CreateTensor<float>(memory_info, results_.data(), results_.size(), output_shape_.data(), output_shape_.size());
}

Wekws::~Wekws() {}

int64_t Wekws::run() {
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

void Wekws::set_melspec(vector<vector<float>> melspec) {
	auto transposed = transpose(melspec);

	for (int i = 0; i < transposed.size(); i++)
	{
		for (int j = 0; j < transposed[i].size(); j++)
		{
			input_spec_[i * transposed[i].size() + j] = transposed[i][j];
		}
	}
	return;
}
