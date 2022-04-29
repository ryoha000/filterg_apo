#pragma once

#include "torch/torch.h"
#include <vector>

using std::vector;

torch::jit::Module initialize_detector();

int keyword_detect(torch::jit::Module model, vector<vector<float>> frames);
