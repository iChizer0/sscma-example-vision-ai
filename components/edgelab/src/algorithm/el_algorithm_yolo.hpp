/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Seeed Technology Co.,Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _EL_ALGORITHM_YOLO_HPP_
#define _EL_ALGORITHM_YOLO_HPP_

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "el_algorithm_base.hpp"
#include "el_cv.h"
#include "el_debug.h"
#include "el_nms.h"
#include "el_types.h"

namespace edgelab::algorithm {

namespace types {

// we're not using inheritance since it not standard layout
struct el_algorithm_yolo_config_t {
    static constexpr el_algorithm_info_t info{
      .type = EL_ALGO_TYPE_YOLO, .categroy = EL_ALGO_CAT_DET, .input_from = EL_SENSOR_TYPE_CAM};
    uint8_t score_threshold;
    uint8_t nms_threshold;
};

}  // namespace types

class YOLO : public base::Algorithm {
    using ImageType        = el_img_t;
    using BoxType          = el_box_t;
    using ConfigType       = types::el_algorithm_yolo_config_t;
    using ScoreType        = decltype(types::el_algorithm_yolo_config_t::score_threshold);
    using NMSThresholdType = decltype(types::el_algorithm_yolo_config_t::nms_threshold);

   public:
    static InfoType algorithm_info;

    YOLO(EngineType* engine, ScoreType score_threshold = 50, NMSThresholdType nms_threshold = 45);
    YOLO(EngineType* engine, const ConfigType& config);
    ~YOLO();

    static bool is_model_valid(const EngineType* engine);

    el_err_code_t                     run(ImageType* input);
    const std::forward_list<BoxType>& get_results() const;

    void      set_score_threshold(ScoreType threshold);
    ScoreType get_score_threshold() const;

    void             set_nms_threshold(NMSThresholdType threshold);
    NMSThresholdType get_nms_threshold() const;

    void       set_algorithm_config(const ConfigType& config);
    ConfigType get_algorithm_config() const;

   protected:
    inline void init();

    el_err_code_t preprocess() override;
    el_err_code_t postprocess() override;

   private:
    enum {
        INDEX_X = 0,
        INDEX_Y = 1,
        INDEX_W = 2,
        INDEX_H = 3,
        INDEX_S = 4,
        INDEX_T = 5,
    };

    ImageType _input_img;
    float     _w_scale;
    float     _h_scale;

    std::atomic<ScoreType>        _score_threshold;
    std::atomic<NMSThresholdType> _nms_threshold;

    std::forward_list<BoxType> _results;
};

}  // namespace edgelab::algorithm

#endif
