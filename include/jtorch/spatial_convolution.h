//
//  spatial_convolution.h
//
//  Created by Jonathan Tompson on 5/15/13.
//

#pragma once

#include <mutex>
#include <condition_variable>
#include "jcl/math/int_types.h"
#include "jcl/math/math_types.h"
#include "jtorch/torch_stage.h"
#include "jcl/jcl.h"  // For jcl::JCLBuffer

namespace jcl { namespace data_str { template <typename T> class VectorManaged; } }

namespace jtorch {

  template <typename T> class Tensor;
  
  class SpatialConvolution : public TorchStage {
  public:
    // Constructor / Destructor
    SpatialConvolution(const uint32_t feats_in, const uint32_t feats_out,
      const uint32_t filt_height, const uint32_t filt_width, 
      const uint32_t padding = 0);
    virtual ~SpatialConvolution();

    virtual TorchStageType type() const { return SPATIAL_CONVOLUTION_STAGE; }
    virtual std::string name() const { return "SpatialConvolution"; }
    virtual void forwardProp(TorchData& input);

    void setWeights(const float* weights);
    void setBiases(const float* biases);
    Tensor<float>* weights() { return weights_; }
    Tensor<float>* biases() { return biases_; }

    static TorchStage* loadFromFile(std::ifstream& file);

  protected:
    uint32_t filt_width_;
    uint32_t filt_height_;
    uint32_t feats_in_;
    uint32_t feats_out_;
    uint32_t padding_;

    Tensor<float>* weights_;
    Tensor<float>* biases_;

    void init(TorchData& input);

    // Non-copyable, non-assignable.
    SpatialConvolution(SpatialConvolution&);
    SpatialConvolution& operator=(const SpatialConvolution&);
  };
  
};  // namespace jtorch
