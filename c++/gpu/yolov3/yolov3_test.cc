// Copyright (c) 2023 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <iostream>
#include <memory>
#include <numeric>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "paddle_inference_api.h"

using paddle_infer::Config;
using paddle_infer::CreatePredictor;
using paddle_infer::PrecisionType;
using paddle_infer::Predictor;

DEFINE_string(model_file, "", "Directory of the inference model.");
DEFINE_string(params_file, "", "Directory of the inference model.");
DEFINE_string(model_dir, "", "Directory of the inference model.");
DEFINE_int32(batch_size, 1, "Directory of the inference model.");
DEFINE_int32(warmup, 0, "warmup.");
DEFINE_int32(repeats, 1, "repeats.");
DEFINE_string(
    run_mode, "paddle_gpu",
    "run_mode which can be: trt_fp32, trt_fp16, trt_int8 and paddle_gpu");
DEFINE_bool(use_dynamic_shape, false, "use trt dynaminc shape.");

using Time = decltype(std::chrono::high_resolution_clock::now());
Time time() { return std::chrono::high_resolution_clock::now(); };
double time_diff(Time t1, Time t2) {
  typedef std::chrono::microseconds ms;
  auto diff = t2 - t1;
  ms counter = std::chrono::duration_cast<ms>(diff);
  return counter.count() / 1000.0;
}

std::shared_ptr<Predictor> InitPredictor() {
  Config config;
  if (FLAGS_model_dir != "") {
    config.SetModel(FLAGS_model_dir);
  }
  config.SetModel(FLAGS_model_file, FLAGS_params_file);
  config.EnableUseGpu(500, 0);

  if (FLAGS_run_mode == "trt_fp32") {
    config.EnableTensorRtEngine(1 << 30, FLAGS_batch_size, 5,
                                PrecisionType::kFloat32, false, false);
  } else if (FLAGS_run_mode == "trt_fp16") {
    config.EnableTensorRtEngine(1 << 30, FLAGS_batch_size, 5,
                                PrecisionType::kHalf, false, false);
  } else if (FLAGS_run_mode == "trt_int8") {
    config.EnableTensorRtEngine(1 << 30, FLAGS_batch_size, 5,
                                PrecisionType::kInt8, false, true);
  }

  if (FLAGS_use_dynamic_shape) {
    std::map<std::string, std::vector<int>> min_input_shape = {
        {"image", {FLAGS_batch_size, 3, 608, 608}},
        {"batch_norm_45.tmp_2", {FLAGS_batch_size, 2048, 19, 19}},
        {"deformable_conv_0.tmp_0", {FLAGS_batch_size, 512, 19, 19}},
        {"relu_44.tmp_0", {FLAGS_batch_size, 2048, 19, 19}},
        {"relu_41.tmp_0", {FLAGS_batch_size, 1024, 38, 38}},
        {"deformable_conv_1.tmp_0", {FLAGS_batch_size, 512, 19, 19}},
        {"relu_23.tmp_0", {FLAGS_batch_size, 512, 76, 76}},
        {"relu_47.tmp_0", {FLAGS_batch_size, 2048, 19, 19}},
        {"deformable_conv_2.tmp_0", {FLAGS_batch_size, 512, 19, 19}}};
    std::map<std::string, std::vector<int>> max_input_shape = {
        {"image", {FLAGS_batch_size, 3, 608, 608}},
        {"batch_norm_45.tmp_2", {FLAGS_batch_size, 2048, 19, 19}},
        {"deformable_conv_0.tmp_0", {FLAGS_batch_size, 512, 19, 19}},
        {"relu_44.tmp_0", {FLAGS_batch_size, 2048, 19, 19}},
        {"relu_41.tmp_0", {FLAGS_batch_size, 1024, 38, 38}},
        {"deformable_conv_1.tmp_0", {FLAGS_batch_size, 512, 19, 19}},
        {"relu_23.tmp_0", {FLAGS_batch_size, 512, 76, 76}},
        {"relu_47.tmp_0", {FLAGS_batch_size, 2048, 19, 19}},
        {"deformable_conv_2.tmp_0", {FLAGS_batch_size, 512, 19, 19}}};
    std::map<std::string, std::vector<int>> opt_input_shape = {
        {"image", {FLAGS_batch_size, 3, 608, 608}},
        {"batch_norm_45.tmp_2", {FLAGS_batch_size, 2048, 19, 19}},
        {"deformable_conv_0.tmp_0", {FLAGS_batch_size, 512, 19, 19}},
        {"relu_44.tmp_0", {FLAGS_batch_size, 2048, 19, 19}},
        {"relu_41.tmp_0", {FLAGS_batch_size, 1024, 38, 38}},
        {"deformable_conv_1.tmp_0", {FLAGS_batch_size, 512, 19, 19}},
        {"relu_23.tmp_0", {FLAGS_batch_size, 512, 76, 76}},
        {"relu_47.tmp_0", {FLAGS_batch_size, 2048, 19, 19}},
        {"deformable_conv_2.tmp_0", {FLAGS_batch_size, 512, 19, 19}}};
    config.SetTRTDynamicShapeInfo(min_input_shape, max_input_shape,
                                  opt_input_shape);
  }

  // Open the memory optim.
  config.EnableMemoryOptim();
  return CreatePredictor(config);
}

void run(Predictor *predictor, const std::vector<float> &input,
         const std::vector<int> &input_shape,
         const std::vector<float> &input_im,
         const std::vector<int> &input_im_shape, std::vector<float> *out_data) {
  auto input_names = predictor->GetInputNames();
  auto im_shape_handle = predictor->GetInputHandle(input_names[0]);
  im_shape_handle->Reshape(input_im_shape);
  im_shape_handle->CopyFromCpu(input_im.data());

  auto image_handle = predictor->GetInputHandle(input_names[1]);
  image_handle->Reshape(input_shape);
  image_handle->CopyFromCpu(input.data());

  auto scale_factor_handle = predictor->GetInputHandle(input_names[2]);
  scale_factor_handle->Reshape(input_im_shape);
  scale_factor_handle->CopyFromCpu(input_im.data());

  CHECK(predictor->Run());

  auto output_names = predictor->GetOutputNames();
  auto output_t = predictor->GetOutputHandle(output_names[0]);
  std::vector<int> output_shape = output_t->shape();
  int out_num = std::accumulate(output_shape.begin(), output_shape.end(), 1,
                                std::multiplies<int>());

  out_data->resize(out_num);
  output_t->CopyToCpu(out_data->data());
}

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  auto predictor = InitPredictor();

  const int height = 608;
  const int width = 608;
  const int channels = 3;
  std::vector<int> input_shape = {FLAGS_batch_size, channels, height, width};
  std::vector<float> input_data(FLAGS_batch_size * channels * height * width);
  for (size_t i = 0; i < input_data.size(); ++i) {
    input_data[i] = i % 255 * 0.13f;
  }
  std::vector<int> input_im_shape = {FLAGS_batch_size, 2};
  std::vector<float> input_im_data(FLAGS_batch_size * 2, 608);

  std::vector<float> out_data;
  run(predictor.get(), input_data, input_shape, input_im_data, input_im_shape,
      &out_data);
  LOG(INFO) << "output num is " << out_data.size();
  return 0;
}
