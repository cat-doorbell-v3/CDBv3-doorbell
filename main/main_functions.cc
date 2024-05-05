/* Copyright 2020-2023 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <algorithm>
#include <cstdint>
#include <iterator>
#include "esp_log.h"

#include "main_functions.h"

#include "audio_provider.h"
#include "feature_provider.h"
#include "micro_model_settings.h"
#include "model.h"
#include "recognize_commands.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/core/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

#define TAG "main_functions"

// Globals, used for compatibility with Arduino-style sketches.
namespace
{
  const tflite::Model *model = nullptr;
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *model_input = nullptr;
  FeatureProvider *feature_provider = nullptr;
  RecognizeCommands *recognizer = nullptr;
  int32_t previous_time = 0;

  // Create an area of memory to use for input, output, and intermediate arrays.
  // The size of this will depend on the model you're using, and may need to be
  // determined by experimentation.
  constexpr int kTensorArenaSize = 30 * 1024;
  uint8_t tensor_arena[kTensorArenaSize];
  int8_t feature_buffer[kFeatureElementCount];
  int8_t *model_input_buffer = nullptr;
} // namespace

// Compute the mean of the data
float ComputeMean(const int8_t *data, int length)
{
  float sum = 0;
  for (int i = 0; i < length; i++)
  {
    sum += data[i];
  }
  return sum / length;
}

// Compute the standard deviation of the data, requires the mean
float ComputeStdDev(const int8_t *data, int length, float mean)
{
  float variance = 0;
  for (int i = 0; i < length; i++)
  {
    variance += (data[i] - mean) * (data[i] - mean);
  }
  variance /= length;
  return sqrt(variance);
}

// The name of this function is important for Arduino compatibility.
void setup()
{
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION)
  {
    MicroPrintf("Model provided is schema version %d not equal to supported "
                "version %d.",
                model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  //
  // tflite::AllOpsResolver resolver;
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroMutableOpResolver<5> micro_op_resolver;
  if (micro_op_resolver.AddDepthwiseConv2D() != kTfLiteOk)
  {
    return;
  }
  if (micro_op_resolver.AddFullyConnected() != kTfLiteOk)
  {
    return;
  }
  if (micro_op_resolver.AddSoftmax() != kTfLiteOk)
  {
    return;
  }
  if (micro_op_resolver.AddReshape() != kTfLiteOk)
  {
    return;
  }
  if (micro_op_resolver.AddConv2D() != kTfLiteOk)
  {
    return;
  }

  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk)
  {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  // Get information about the memory area to use for the model's input.
  model_input = interpreter->input(0);

  ESP_LOGI(TAG, "Dims size: %d", model_input->dims->size);

  ESP_LOGI(TAG, "Dims data[0]: %d", model_input->dims->data[0]);

  ESP_LOGI(TAG, "Dims data[1]: %d", model_input->dims->data[1]);

  ESP_LOGI(TAG, "Expecting Dims data[1] to be (kFeatureCount * kFeatureSize): %d", kFeatureCount * kFeatureSize);

  ESP_LOGI(TAG, "Data type: %d", model_input->type);

  ESP_LOGI(TAG, "Expecting Data type (kTfLiteInt8): %d", kTfLiteInt8);

  if ((model_input->dims->size != 2) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] !=
       (kFeatureCount * kFeatureSize)) ||
      (model_input->type != kTfLiteInt8))
  {
    MicroPrintf("Bad input tensor parameters in model");
    return;
  }
  model_input_buffer = tflite::GetTensorData<int8_t>(model_input);

  // Prepare to access the audio spectrograms from a microphone or other source
  // that will provide the inputs to the neural network.
  // NOLINTNEXTLINE(runtime-global-variables)
  static FeatureProvider static_feature_provider(kFeatureElementCount,
                                                 feature_buffer);
  feature_provider = &static_feature_provider;

  static RecognizeCommands static_recognizer;
  recognizer = &static_recognizer;

  previous_time = 0;
}

// The name of this function is important for Arduino compatibility.
void loop()
{
  // Fetch the spectrogram for the current time.
  const int32_t current_time = LatestAudioTimestamp();
  int how_many_new_slices = 0;
  TfLiteStatus feature_status = feature_provider->PopulateFeatureData(previous_time, current_time, &how_many_new_slices);
  if (feature_status != kTfLiteOk)
  {
    MicroPrintf("Feature generation failed");
    return;
  }
  previous_time = current_time;
  // If no new audio samples have been received since last time, don't bother
  // running the network model.
  if (how_many_new_slices == 0)
  {
    return;
  }

  // Copy feature buffer to input tensor
  for (int i = 0; i < kFeatureElementCount; i++)
  {
    model_input_buffer[i] = feature_buffer[i];
  }

  // Run the model on the spectrogram input and make sure it succeeds.
  TfLiteStatus invoke_status = interpreter->Invoke();
  if (invoke_status != kTfLiteOk)
  {
    MicroPrintf("Invoke failed");
    return;
  }

  // Obtain a pointer to the output tensor
  TfLiteTensor *output = interpreter->output(0);
  float output_scale = output->params.scale;
  int output_zero_point = output->params.zero_point;
  int max_idx = 0;
  float max_result = -FLT_MAX; // Start with the smallest possible float
  
  // Dequantize output values and find the max
  for (int i = 0; i < kCategoryCount; i++)
  {
      float current_result = static_cast<float>(tflite::GetTensorData<int8_t>(output)[i] - output_zero_point) * output_scale;
      if (current_result > max_result)
      {
          max_result = current_result; // update max result
          max_idx = i;                 // update index of max result
      }
  }
  
  // Print the results with the maximum result in red
  for (int i = 0; i < kCategoryCount; i++)
  {
      float current_result = static_cast<float>(tflite::GetTensorData<int8_t>(output)[i] - output_zero_point) * output_scale;
      if (i == max_idx)
      {
          // Print the maximum result in red
          printf(" \033[31m%s: %.2f\033[0m ", kCategoryLabels[i], current_result);
      }
      else
      {
          // Print other results in default color
          printf(" %s: %.2f ", kCategoryLabels[i], current_result);
      }
  }
  
  // Send to the queue only if the maximum result is meow and greater than 0.85
  if (max_result > 0.85f && strcmp(kCategoryLabels[max_idx], "meow") == 0)
  {
    bool detect = true;
    if (xQueueSend(catHeardQueue, &detect, portMAX_DELAY) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send meow detection to the queue");
    }
  }
  
  printf("\n");
}
