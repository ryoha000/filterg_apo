#include "stdafx.h"
#include "detector.h"
#include "torch/script.h"

torch::jit::Module initialize_detector()
{
	return torch::jit::load("C:\Program Files\FiltergDebug\models\wewks_model.zip");
}

int keyword_detect(torch::jit::Module model, vector<vector<float>> frames) {
	// Ç±Ç±Ç≈êÑò_Ç∑ÇÈ
	return 0;
}
