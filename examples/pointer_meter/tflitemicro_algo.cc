/*
 * tflitemicro_algo.cc
 *
 *  Created on: 20220922
 *      Author: 902453
 */
#include <math.h>
#include <stdint.h>
#include <forward_list>
#include "grove_ai_config.h"
#include "isp.h"
#include "hx_drv_webusb.h"
#include "embARC_debug.h"

#include "tflitemicro_algo.h"

#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"


// TFu debug log, make it hardware platform print
extern "C" void DebugLog(const char *s) { xprintf("%s", s); } //{ fprintf(stderr, "%s", s); }

#define PI acos(-1)
#define METER_RESULT_MAX_SIZE 4
#define MODEL_INDEX 1
#define ALGORITHM_INDEX 3
#define IMG_PREVIEW_MAX_SIZE 16
#define IMAGE_PREIVEW_ELEMENT_NUM 2
#define IMAGE_PREIVEW_ELEMENT_SIZE 4
#define IMAGE_PREVIEW_FORMATE "{\"type\":\"preview\", \"algorithm\":%d, \"model\":%d,\"count\":%d, \"object\":{\"x\": [%s],\"y\": [%s]}, \"value:\"%d}"

// Globals, used for compatibility with Arduino-style sketches.
namespace
{
    tflite::ErrorReporter *error_reporter = nullptr;
    const tflite::Model *model = nullptr;
    tflite::MicroInterpreter *interpreter = nullptr;
    TfLiteTensor *input = nullptr;
    TfLiteTensor *output = nullptr;
    static point_t points[METER_RESULT_MAX_SIZE];
    static uint32_t value = 0;

    // In order to use optimized tensorflow lite kernels, a signed int8_t quantized
    // model is preferred over the legacy unsigned model format. This means that
    // throughout this project, input images must be converted from unisgned to
    // signed format. The easiest and quickest way to convert from unsigned to
    // signed 8-bit integers is to subtract 128 from the unsigned value to get a
    // signed value.

    // An area of memory to use for input, output, and intermediate arrays.
    constexpr int kTensorArenaSize = 500 * 1024;
#if (defined(__GNUC__) || defined(__GNUG__)) && !defined(__CCAC__)
    static uint8_t tensor_arena[kTensorArenaSize] __attribute__((aligned(16)));
#else
#pragma Bss(".tensor_arena")
    static uint8_t tensor_arena[kTensorArenaSize];
#pragma Bss()
#endif // if defined (_GNUC_) && !defined (_CCAC_)
} // namespace

extern "C" int tflitemicro_algo_init()
{
    int ercode = 0;
    uint32_t *xip_flash_addr;

    static tflite::MicroErrorReporter micro_error_reporter;
    error_reporter = &micro_error_reporter;
    xip_flash_addr = (uint32_t *)0x30000000;

    // get model (.tflite) from flash
    model = ::tflite::GetModel(xip_flash_addr);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        TF_LITE_REPORT_ERROR(error_reporter,
                             "Model provided is schema version %d not equal "
                             "to supported version %d.",
                             model->version(), TFLITE_SCHEMA_VERSION);
        return -1;
    }

    static tflite::MicroMutableOpResolver<13> micro_op_resolver;
    micro_op_resolver.AddPad();
    micro_op_resolver.AddPadV2();
    micro_op_resolver.AddAdd();
    micro_op_resolver.AddMul();
    micro_op_resolver.AddMean();
    micro_op_resolver.AddReshape();
    micro_op_resolver.AddLogistic();
    micro_op_resolver.AddConv2D();
    micro_op_resolver.AddMaxPool2D();
    micro_op_resolver.AddConcatenation();
    micro_op_resolver.AddAveragePool2D();
    micro_op_resolver.AddFullyConnected();
    micro_op_resolver.AddResizeNearestNeighbor();

    // Build an interpreter to run the model with.
    // NOLINTNEXTLINE(runtime-global-variables)
    static tflite::MicroInterpreter static_interpreter(
        model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
    interpreter = &static_interpreter;
    // Allocate memory from the tensor_arena for the model's tensors.
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk)
    {
        TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
        return -1;
    }
    // Get information about the memory area to use for the model's input.
    input = interpreter->input(0);
    output = interpreter->output(0);

    return ercode;
}

float get_value(float x, float y, float start_x, float start_y,
                float end_x, float end_y, float center_x, float center_y, float start_value, float end_value)
{
    /*
    pfld model post-precessiong, paras:
        (x, y): coordinate of pointer.
        (start_x, start_y): coordinate of starting point.
        (end_x, end_y): coordinate of ending point.
        (center_x, center_y): coordinate of center of circle.
        range: range from start point to end point.
    */
    float center_to_start, center_to_end, start_to_end; // The three sides of a triangle from center to start, then to end.
    float theta, theta1;
    float A, B, C, D;
    float start_to_pointer, center_to_pointer; // The two sides of start to pointer and center to pointer.
    float out;

    center_to_start = sqrt(pow(fabs(center_x - start_x), 2) + pow(fabs(center_y - start_y), 2));
    center_to_end = sqrt(pow(fabs(center_x - end_x), 2) + pow(fabs(center_y - end_y), 2));
    start_to_end = sqrt(pow(fabs(end_x - start_x), 2) + pow(fabs(end_y - start_y), 2));
    theta = acos((pow(center_to_start, 2) + pow(center_to_end, 2) - pow(start_to_end, 2)) / (2 * center_to_start * center_to_end)); // cosθ=(a^2 + b^2 - c^2) / 2ab
    theta = 2 * PI - theta;

    // determine center in which side of line from start to pointer.
    A = center_y - start_y;
    B = start_x - center_x;
    C = (center_x * start_y) - (start_x * center_y);
    D = A * x + B * y + C; // linear function: Ax+By+C=0.

    start_to_pointer = sqrt(pow(fabs(x - start_x), 2) + pow(fabs(y - start_y), 2));
    center_to_pointer = sqrt(pow(fabs(x - center_x), 2) + pow(fabs(y - center_y), 2));
    theta1 = acos((pow(center_to_start, 2) + pow(center_to_pointer, 2) - pow(start_to_pointer, 2)) / (2 * center_to_start * center_to_pointer));

    if (D < 0)
    {
        theta1 = 2 * PI - theta1;
    }

    if (theta1 > theta)
    {
        return -1;
    }
    else
    {
        out = (end_value - start_value) * (theta1 / theta) + start_value;
        if (out < start_value)
            return start_value;
        else if (out > end_value)
            return end_value;
        else
            return out;
    }
}

extern "C" int tflitemicro_algo_run(uint32_t img, uint32_t ow, uint32_t oh)
{

    uint16_t h = input->dims->data[1];
    uint16_t w = input->dims->data[2];
    uint16_t c = input->dims->data[3];

    yuv422p2rgb(input->data.uint8, (const uint8_t *)img, oh, ow, c, h, w, VISION_ROTATION);

    for (int i = 0; i < input->bytes; i++)
    {
        input->data.int8[i] -= 128;
    }

    // Run inference, and report any error
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk)
    {
        TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed.");
        return -1;
    }

    // Get the results of the inference attempt
    float scale = ((TfLiteAffineQuantization *)(output->quantization.params))->scale->data[0];
    int zero_point = ((TfLiteAffineQuantization *)(output->quantization.params))->zero_point->data[0];


    for (int i = 0; i < output->bytes / 2; i++)
    {
        points[i].x = uint16_t(float(float(output->data.int8[i * 2] - zero_point) * scale) * ow);
        points[i].y = uint16_t(float(float(output->data.int8[i * 2 + 1] - zero_point) * scale) * oh);
    }

    value = (uint32_t)get_value(points[0].x, points[0].y, points[1].x, points[1].y, points[2].x, points[2].y, points[3].x, points[3].y, 0, 1000);

    return 0;
}

int tflitemicro_algo_get_preview(char *preview, uint16_t max_length)
{

    uint16_t index = 0;

    // 输入preview最多能有多少element
    uint16_t available_size = (max_length - sizeof(IMAGE_PREVIEW_FORMATE)) / (IMAGE_PREIVEW_ELEMENT_SIZE * IMAGE_PREIVEW_ELEMENT_NUM);

    if (available_size < 1)
    {
        return -1;
    }

    // element数组
    char element[IMAGE_PREIVEW_ELEMENT_NUM][IMG_PREVIEW_MAX_SIZE * IMAGE_PREIVEW_ELEMENT_SIZE] = {0};

     // 生成element
    for (uint8_t i = 0; i < METER_RESULT_MAX_SIZE; i++)
    {
        if (index == 0)
        {
            snprintf(element[0], sizeof(element[0]), "%d", points[i].x);
            snprintf(element[1], sizeof(element[1]), "%d", points[i].x);
        }
        else
        {
            snprintf(element[0], sizeof(element[0]), "%s,%d", element[0], points[i].x);
            snprintf(element[1], sizeof(element[1]), "%s,%d", element[1], points[i].x);
        }
        index++;
        // 如果超过最大的可预览长度 则退出
        if (index > IMG_PREVIEW_MAX_SIZE || index > available_size)
        {
            break;
        }
    }

    // 规格化preview
    snprintf(preview, max_length, IMAGE_PREVIEW_FORMATE, ALGORITHM_INDEX, MODEL_INDEX, index, element[0], element[1], value);

    return 0;
}

void tflitemicro_algo_exit()
{
    hx_lib_pm_cplus_deinit();
}
