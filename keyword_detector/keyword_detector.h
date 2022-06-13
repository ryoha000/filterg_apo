#pragma once

#include <onnxruntime/onnxruntime_cxx_api.h>
#include <iterator>
#include <algorithm>
#include <memory>

using std::vector;

namespace keyword_detector {
	class detector
	{
	public:
		detector();
		~detector();
		int run();
		int set_frames(vector<float> frames);

	private:
		static constexpr const int hop_count_ = 101;
		static constexpr const int mel_bin_count_ = 60;

		static constexpr const char* input_names[] = { "input" };
		static constexpr const char* output_names[] = { "output" };

		//vector<float> input_spec_;
		std::array<float, hop_count_* mel_bin_count_> input_spec_{};
		//vector<float> results_;
		std::array<float, 7> results_{};
		int64_t result_{ 0 };

		Ort::Env env;
		Ort::Session session_{ env, L"C:\\Program Files\\FiltergDebug\\wewks_melspec_12800.onnx", Ort::SessionOptions{nullptr} };

		std::shared_ptr< Ort::Value > input_tensor_{ nullptr };
		//Ort::Value input_tensor_{ nullptr };
		//vector<int64_t> input_shape_;
		static constexpr const std::array<int64_t, 3> input_shape_{ 1, hop_count_, mel_bin_count_ };

		std::shared_ptr< Ort::Value > output_tensor_{ nullptr };
		//Ort::Value output_tensor_{ nullptr };
		//vector<int64_t> output_shape_;
		static constexpr const std::array<int64_t, 2> output_shape_{ 1, 7 };

		vector<float> resampled_frames;
	};
}
