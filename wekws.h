#pragma once
#include <onnxruntime_cxx_api.h>
#include <iterator>
#include <algorithm>

using std::vector;

class Wekws
{
public:
	Wekws();
	~Wekws();
	int64_t run();
	void set_melspec(vector<vector<float>> melspec);

private:
	static constexpr const char* input_names[] = { "input" };
	static constexpr const char* output_names[] = { "output" };
	static constexpr const int width_ = 79;
	static constexpr const int height_ = 60;

	std::array<float, width_ * height_> input_spec_{};
	std::array<float, 7> results_{};
	int64_t result_{ 0 };

	Ort::Env env;
	Ort::Session session_{ env, L"wewks_short_time_melspec.onnx", Ort::SessionOptions{nullptr} };

	Ort::Value input_tensor_{ nullptr };
	std::array<int64_t, 3> input_shape_{ 1, width_, height_ };

	Ort::Value output_tensor_{ nullptr };
	std::array<int64_t, 2> output_shape_{ 1, 7 };
};
