// THE CPP FUNCTIONALITY HERE IS TO BE TESTED AGAINST "jtorch_test.lua" SCRIPT

#include <stdlib.h>
#include <cmath>
#include <thread>
#include <iostream>
#include <limits>
#include <assert.h>
#include "jtorch/torch_stage.h"
#include "jtorch/jtorch.h"
#include "jtorch/tensor.h"
#include "jtorch/spatial_convolution.h"
#include "jtorch/spatial_convolution_map.h"
#include "jtorch/spatial_convolution_mm.h"
#include "jtorch/spatial_lp_pooling.h"
#include "jtorch/spatial_max_pooling.h"
#include "jtorch/spatial_subtractive_normalization.h"
#include "jtorch/spatial_divisive_normalization.h"
#include "jtorch/spatial_contrastive_normalization.h"
#include "jtorch/spatial_up_sampling_nearest.h"
#include "jtorch/identity.h"
#include "jtorch/linear.h"
#include "jtorch/reshape.h"
#include "jtorch/tanh.h"
#include "jtorch/threshold.h"
#include "jtorch/sequential.h"
#include "jtorch/parallel_table.h"
#include "jtorch/table.h"
#include "jtorch/join_table.h"
#include "jtorch/select_table.h"
#include "jtorch/c_add_table.h"
#include "jcl/threading/thread_pool.h"
#include "jcl/data_str/vector_managed.h"
#include "debug_util.h"
#include "file_io.h"
#include "clk/clk.h"

#if defined(WIN32) || defined(_WIN32)
  #define snprintf _snprintf_s
#endif

#define JTORCH_FLOAT_PRECISION 1e-6f

using namespace std;
using namespace jtorch;
using namespace jcl::threading;
using namespace jcl::math;
using namespace jcl::data_str;
using namespace jcl::file_io;
using namespace clk;

const uint32_t num_feats_in = 5;
const uint32_t num_feats_out = 10;
const uint32_t fan_in = 3;  // For SpatialConvolutionMap
const uint32_t width = 10;
const uint32_t height = 10;
const uint32_t filt_height = 5;
const uint32_t filt_width = 5;
float din[width * height * num_feats_in];
float dout[width * height * num_feats_out];

// CPU weights and biases for SpatialConvolution stage
float cweights[num_feats_out * num_feats_in * filt_height * filt_width];
float cbiases[num_feats_out];

// CPU weights and biases for Linear stage
const uint32_t lin_size_in = num_feats_in * width * height;
const uint32_t lin_size_out = 20;
float lweights[lin_size_in * lin_size_out];
float lbiases[lin_size_out];

void testJTorchValue(jtorch::Tensor<float>* data, const std::string& filename,
  float precision = JTORCH_FLOAT_PRECISION) {
  float* correct_data = new float[data->nelems()];
  float* model_data = new float[data->nelems()];
  memset(model_data, 0, sizeof(model_data[0]) * data->nelems());
  memset(correct_data, 0, sizeof(correct_data[0]) * data->nelems());

  jtorch::Tensor<float>* correct_data_tensor = Tensor<float>::loadFromFile(filename);

  if (!correct_data_tensor->isSameSizeAs(*data)) {
    std::cout << "Test FAILED (size mismatch)!: " << filename << std::endl;
  } else {
    correct_data_tensor->getData(correct_data);
    data->getData(model_data);
    bool data_correct = true;
    for (uint32_t i = 0; i < data->nelems() && data_correct; i++) {
      float delta = fabsf(model_data[i] - correct_data[i]) ;
      if (delta > precision && (delta /
        std::max<float>(fabsf(correct_data[i]), LOOSE_EPSILON)) > precision) {
        data_correct = false;
        for (uint32_t repeat = 0; repeat < 5; repeat++) {
          for (uint32_t cnt = 0; cnt < 60; cnt++) {
            std::cout << "*";
          }
          std::cout << std::endl;
        }
        std::cout << "index " << i << " incorrect!: " << std::endl;
        std::cout << std::fixed << std::setprecision(15); 
        std::cout << "model_data[" << i << "] = " << model_data[i] << std::endl;
        std::cout << "correct_data[" << i << "] = " << correct_data[i] << std::endl;
        for (uint32_t repeat = 0; repeat < 5; repeat++) {
          for (uint32_t cnt = 0; cnt < 60; cnt++) {
            std::cout << "*";
          }
          std::cout << std::endl;
        }
      }
    }
    if (data_correct) {
      std::cout << "Test PASSED: " << filename << std::endl;
    } else {
      std::cout << "Test FAILED!: " << filename << std::endl;
    }
  }
  delete correct_data_tensor;
  delete[] model_data;
  delete[] correct_data;
}

void assertTrue(bool value, const std::string& module_name) {
  if (value) {
    std::cout << "Test PASSED: " << module_name << std::endl;
  } else {
    std::cout << "Test FAILED!: " << module_name << std::endl;
  }
}

int main(int argc, char *argv[]) {  
#if defined(_DEBUG) || defined(DEBUG)
  jcl::debug::EnableMemoryLeakChecks();
  // jcl::debug::EnableAggressiveMemoryLeakChecks();
  // jcl::debug::SetBreakPointOnAlocation(1898);
#endif

  try {
    std::cout << "Beginning jtorch tests..." << std::endl;

    const bool use_cpu = false;
    jtorch::InitJTorch("../", use_cpu);

    const uint32_t isize[3] = {width, height, num_feats_in};
    Tensor<float> data_in(3, isize);
    const uint32_t osize[3] = {width, height, num_feats_in};
    Tensor<float> data_out(3, osize);

    for (uint32_t f = 0; f < num_feats_in; f++) {
      float val = (f+1) - (float)(width * height) / 16.0f;
      for (uint32_t v = 0; v < height; v++) {
        for (uint32_t u = 0; u < width; u++) {
          din[f * width * height + v * width + u] = val;
          val += 1.0f / 8.0f;
        }
      }
    }
    data_in.setData(din);
    testJTorchValue(&data_in, "./test_data/data_in.bin");

    // Test Tanh, Threshold and SpatialConvolutionMap in a Sequential container
    // (this means we can also test Sequential at the same time)
    Sequential stages;
    {
      // ***********************************************
      // Test Tanh
      stages.add(new Tanh());
      stages.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(stages.output), 
        "./test_data/tanh_result.bin");
    
      // ***********************************************
      // Test Threshold
      const float threshold = 0.5f;
      const float val = 0.1f;
      stages.add(new jtorch::Threshold());
      ((jtorch::Threshold*)stages.get(1))->threshold = threshold;
      ((jtorch::Threshold*)stages.get(1))->val = val;
      stages.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(stages.output), 
        "./test_data/threshold.bin");
    
      // ***********************************************
      // Test SpatialConvolutionMap --> THIS STAGE IS STILL ON THE CPU!!
      stages.add(new SpatialConvolutionMap(num_feats_in, num_feats_out, fan_in,
        filt_height, filt_width));
      for (int32_t i = 0; i < static_cast<int32_t>(num_feats_out); i++) {
        ((SpatialConvolutionMap*)stages.get(2))->biases[i] = (float)(i+1) / 
          (float)num_feats_out - 0.5f;
      }
      const float sigma_x_sq = 1.0f;
      const float sigma_y_sq = 1.0f;
      for (int32_t i = 0; i < static_cast<int32_t>(num_feats_out * fan_in); i++) {
        float scale = ((float)(i + 1) / (float)(num_feats_out * fan_in));
        for (int32_t v = 0; v < static_cast<int32_t>(filt_height); v++) {
          for (int32_t u = 0; u < static_cast<int32_t>(filt_width); u++) {
            float x = (float)u - (float)(filt_width-1) / 2.0f;
            float y = (float)v - (float)(filt_height-1) / 2.0f;
            ((SpatialConvolutionMap*)stages.get(2))->weights[i][v * filt_width + u] = 
              scale * expf(-((x*x)/(2.0f*sigma_x_sq) + (y*y)/(2.0f*sigma_y_sq)));
          }
        }
      }
      int32_t cur_filt = 0;
      for (int32_t f_out = 0; f_out < static_cast<int32_t>(num_feats_out); f_out++) {
        for (int32_t f_in = 0; f_in < static_cast<int32_t>(fan_in); f_in++) {
          ((SpatialConvolutionMap*)stages.get(2))->conn_table[f_out][f_in * 2 + 1] = cur_filt;
          int32_t cur_f_in = (f_out + f_in) % num_feats_in;
          ((SpatialConvolutionMap*)stages.get(2))->conn_table[f_out][f_in * 2] = cur_f_in;
          cur_filt++;
        }
      }
      stages.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(stages.output), 
        "./test_data/spatial_convolution_map.bin");
    }

    // ***********************************************
    // Test SpatialConvolution
    {
      SpatialConvolution conv(num_feats_in, num_feats_out, filt_height, filt_width);
      for (int32_t i = 0; i < static_cast<int32_t>(num_feats_out); i++) {
        cbiases[i] = (float)(i+1) / (float)num_feats_out - 0.5f;
      }
      const float sigma_x_sq = 1.0f;
      const float sigma_y_sq = 1.0f;
      const uint32_t filt_dim = filt_width * filt_height;
      for (int32_t fout = 0; fout < static_cast<int32_t>(num_feats_out); fout++) {
        for (int32_t fin = 0; fin < static_cast<int32_t>(num_feats_in); fin++) {
          int32_t i = fout * num_feats_out + fin;
          float scale = ((float)(i + 1) / (float)(num_feats_out * num_feats_in));
          for (int32_t v = 0; v < static_cast<int32_t>(filt_height); v++) {
            for (int32_t u = 0; u < static_cast<int32_t>(filt_width); u++) {
              float x = (float)u - (float)(filt_width-1) / 2.0f;
              float y = (float)v - (float)(filt_height-1) / 2.0f;
              cweights[fout * filt_dim * num_feats_in + fin * filt_dim + v * filt_width + u] =
                scale * expf(-((x*x)/(2.0f*sigma_x_sq) + (y*y)/(2.0f*sigma_y_sq)));
            }
          }
        }
      }
      conv.setWeights(cweights);
      conv.setBiases(cbiases);
      // TODO: This shouldn't use the output from the previous test
      conv.forwardProp(*stages.get(1)->output);
      testJTorchValue(TO_TENSOR_PTR(conv.output), 
        "./test_data/spatial_convolution.bin");

      const uint32_t padding = 6;
      SpatialConvolutionMM convmm(num_feats_in, num_feats_out, filt_height, 
        filt_width, padding);
      Tensor<float>::copy(*convmm.weights(), *conv.weights());
      Tensor<float>::copy(*convmm.biases(), *conv.biases());
      convmm.forwardProp(*stages.get(1)->output);
      testJTorchValue(TO_TENSOR_PTR(convmm.output), 
        "./test_data/spatial_convolution_mm_padding.bin");
    }
    
    // ***********************************************
    // Test SpatialLPPooling
    {
      const float pnorm = 2;
      const int32_t pool_u = 2;
      const int32_t pool_v = 2;
      stages.add(new SpatialLPPooling(pnorm, pool_v, pool_u));
      stages.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(stages.output), 
        "./test_data/spatial_lp_pooling.bin");
    }

    // ***********************************************
    // Test SpatialMaxPooling
    {
      const uint32_t pool_u = 2;
      const uint32_t pool_v = 2;
      SpatialMaxPooling max_pool_stage(pool_v, pool_u);
      max_pool_stage.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(max_pool_stage.output), 
        "./test_data/spatial_max_pooling.bin");
    }
  
    // ***********************************************
    // Test SpatialSubtractiveNormalization
    {
      uint32_t gauss_size = 7;
      Tensor<float>* kernel_1d = Tensor<float>::gaussian1D(gauss_size);
      Tensor<float>* kernel_2d = Tensor<float>::gaussian(gauss_size);
      const float precision = JTORCH_FLOAT_PRECISION * 10;

      SpatialSubtractiveNormalization sub_norm_stage(*kernel_1d);
      sub_norm_stage.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(sub_norm_stage.output), 
        "./test_data/spatial_subtractive_normalization.bin", precision);

      SpatialSubtractiveNormalization sub_norm_stage_2d(*kernel_2d);
      sub_norm_stage_2d.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(sub_norm_stage_2d.output), 
        "./test_data/spatial_subtractive_normalization_2d.bin", precision);

      delete kernel_1d;
      delete kernel_2d;
    }

    // ***********************************************
    // Test SpatialDivisiveNormalization
    {
      uint32_t gauss_size = 7;
      Tensor<float>* kernel_1d = Tensor<float>::gaussian1D(gauss_size);
      Tensor<float>* kernel_2d = Tensor<float>::gaussian(gauss_size);
      const float precision = JTORCH_FLOAT_PRECISION * 10;

      SpatialDivisiveNormalization div_norm_stage(*kernel_1d);
      div_norm_stage.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(div_norm_stage.output), 
        "./test_data/spatial_divisive_normalization.bin", precision);

      SpatialDivisiveNormalization div_norm_stage_2d(*kernel_2d);
      div_norm_stage_2d.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(div_norm_stage_2d.output), 
        "./test_data/spatial_divisive_normalization_2d.bin", precision);

      delete kernel_1d;
      delete kernel_2d;
    }

    // ***********************************************
    // Test SpatialContrastiveNormalization
    {
      Tensor<float>* lena = Tensor<float>::loadFromFile("./test_data/lena_image.bin");

      const uint32_t kernel_size = 7;
      Tensor<float>* kernel2 = new Tensor<float>(1, &kernel_size);
      Tensor<float>::fill(*kernel2, 1);
      SpatialContrastiveNormalization cont_norm_stage(kernel2);
      cont_norm_stage.forwardProp(*lena);
      const float precision = JTORCH_FLOAT_PRECISION * 10;
      testJTorchValue(TO_TENSOR_PTR(cont_norm_stage.output), 
        "./test_data/spatial_contrastive_normalization.bin", precision);

      delete lena;
      delete kernel2;
    }

    // ***********************************************
    // Test Linear
    {
      Sequential lin_stage;
      lin_stage.add(new Reshape(1, &lin_size_in));

      Linear* lin = new Linear(lin_size_in, lin_size_out);
      lin_stage.add(lin);
      // Weight matrix is M (rows = outputs) x N (columns = inputs)
      // It is stored column major with the M dimension stored contiguously
      for (int32_t n = 0; n < lin_size_in; n++) {
        for (int32_t m = 0; m < lin_size_out; m++) {
          int32_t out_i = n * lin_size_out + m;
          int32_t k = m * lin_size_in + n + 1;
          lweights[out_i] = (float)k / (float)(lin_size_in * lin_size_out);
        }
      }

      for (int32_t i = 0; i < lin_size_out; i++) {
        lbiases[i] = (float)(i+1) / (float)(lin_size_out);
      }
      lin->setBiases(lbiases);
      lin->setWeights(lweights);
      lin_stage.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(lin_stage.output), 
        "./test_data/linear.bin");
    }

    // ***********************************************
    // Test Identity
    {
      Identity eye;
      const int32_t rand_size = 5;
      Tensor<float>* rand = Tensor<float>::gaussian(rand_size);
      eye.forwardProp(*rand);
      assertTrue(eye.output == rand, "Identity");
      delete rand;
    }

    // ***********************************************
    // Test SelectTable
    {
      const int32_t table_size = 5;
      Table input;
      for (int32_t i = 0; i < table_size; i++) {
        input.add(Tensor<float>::gaussian1D(i+1));  // Transfers ownership
      }

      bool test_passed = true;
      for (int32_t i = 0; i < table_size; i++) {
        SelectTable* module = new SelectTable(i);
        module->forwardProp(input);
        test_passed = test_passed && module->output == input(i);
        test_passed = test_passed && TO_TENSOR_PTR(module->output)->nelems() == i+1;
        delete module;
      }
      assertTrue(test_passed, "SelectTable");
    }

    // ***********************************************
    // Test CAddTable
    {
      const int32_t table_size = 5;
      const int32_t tensor_size = 5;
      Table input;
      for (int32_t i = 0; i < table_size; i++) {
        input.add(Tensor<float>::gaussian(tensor_size));  // Transfers ownership
        Tensor<float>::mul(*(Tensor<float>*)input(i), (float)(i+1));
      }

      // Add the tensors to get the ground truth
      float* gt = new float[tensor_size * tensor_size];
      float* temp = new float[tensor_size * tensor_size];
      memset(gt, 0, sizeof(gt[0]) * tensor_size * tensor_size);
      for (int32_t i = 0; i < table_size; i++) {
        assert(input(i)->dataSize() == tensor_size * tensor_size);
        TO_TENSOR_PTR(input(i))->getData(temp);
        for (uint32_t i = 0; i < tensor_size * tensor_size; i++) {
          gt[i] += temp[i];
        }
      }

      CAddTable* module = new CAddTable();
      module->forwardProp(input);
      TO_TENSOR_PTR(module->output)->getData(temp);

      bool test_passed = true;
      for (int32_t i = 0; i < tensor_size * tensor_size; i++) {
        test_passed = test_passed && 
          (fabsf(temp[i] - gt[i]) < JTORCH_FLOAT_PRECISION);
      }
      assertTrue(test_passed, "CAddTable");

      delete module;
      delete[] gt;
      delete[] temp;
    }

    // ***********************************************
    // Test SpatialUpSamplingNearest
    {
      int32_t scale = 4;
      SpatialUpSamplingNearest module(scale);
      module.forwardProp(data_in);
      testJTorchValue(TO_TENSOR_PTR(module.output), 
        "./test_data/spatial_up_sampling_nearest.bin");
    }

    // ***********************************************
    // Test Loading and running a model
    {
      TorchStage* model = TorchStage::loadFromFile("./test_data/testmodel.bin");

      model->forwardProp(data_in);

      // Some debugging if things go wrong:
      if (model->type() != SEQUENTIAL_STAGE) {
        throw std::runtime_error("main() - ERROR: Expecting Sequential!");
      }
      const TorchStageType stages[] = {SPATIAL_CONVOLUTION_STAGE, TANH_STAGE,
        THRESHOLD_STAGE, SPATIAL_MAX_POOLING_STAGE, SPATIAL_CONVOLUTION_STAGE,
        RESHAPE_STAGE, LINEAR_STAGE};

      if (((Sequential*)model)->size() != sizeof(stages) / sizeof(stages[0])) {
        throw std::runtime_error("main() - ERROR: Badly formatted model!");
      }

      for (uint32_t i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
        TorchStage* stage = ((Sequential*)model)->get(i);
        if (stage->type() != stages[i]) {
          throw std::runtime_error("main() - ERROR: Badly formatted model!");
        }
      }

      testJTorchValue(TO_TENSOR_PTR(model->output),
        "./test_data/test_model_result.bin");

      delete model;
    }

    // ***********************************************
    // Test Loading and running a big model (ie our body tracking model)
    if (jcl::file_io::fileExists("./test_data/big_model.bin")) {
      TorchStage* model = TorchStage::loadFromFile("./test_data/big_model.bin");

      // The input is a nested table of tensors (one for each resolution bank)
      Table* input = new Table();
      const uint32_t num_banks = 3;
      for (uint32_t i = 1; i <= num_banks; i++) {
        std::stringstream ss;
        ss << "./test_data/big_model_input_bank" << i << ".bin";
        Table* cur_table = new Table();
        cur_table->add(Tensor<float>::loadFromFile(ss.str()));
        cur_table->add(NULL);
        input->add(cur_table);
      }

      model->forwardProp(*input);

      const float precision = JTORCH_FLOAT_PRECISION * 100;
      testJTorchValue(TO_TENSOR_PTR(model->output),
        "./test_data/big_model_output.bin", precision);

      delete model;
      delete input;
    }

    // ***********************************************
    // Profile convolution
    {
      const uint32_t fin = 128, fout = 512, kw = 11, kh = 11, pad = 5,
        imw = 90, imh = 60;
      const double t_test = 5.0;
      double t_start, t_end;
      uint64_t niters;
      SpatialConvolution conv(fin, fout, kh, kw, pad);
      SpatialConvolutionMM conv_mm(fin, fout, kh, kw, pad);
      uint32_t size[3] = {imw, imh, fin};
      Tensor<float>* input = new Tensor<float>(3, size);
      clk::Clk clk;

      Tensor<float>::fill(*conv.weights(), 1);
      Tensor<float>::fill(*conv.biases(), 1);
      Tensor<float>::fill(*input, 1);

      std::cout << "\tProfiling SpatialConvolutionMM for " << t_test << 
        " seconds" << std::endl;
      t_start = clk.getTime();
      t_end = t_start;
      niters = 0;
      while (t_end - t_start < t_test) {
        conv_mm.forwardProp(*input);
        niters++;
        jtorch::Sync();
        t_end = clk.getTime();
      }
      std::cout << "\t\tExecution time: " << (t_end - t_start) / (double)niters
         << " seconds per FPROP" << std::endl;

      std::cout << "\tProfiling SpatialConvolution for " << t_test << 
        " seconds" << std::endl;
      t_start = clk.getTime();
      t_end = t_start;
      niters = 0;
      while (t_end - t_start < t_test) {
        conv.forwardProp(*input);
        niters++;
        jtorch::Sync();
        t_end = clk.getTime();
      }
      std::cout << "\t\tExecution time: " << (t_end - t_start) / (double)niters
         << " seconds per FPROP" << std::endl;

      delete input;
    }

  } catch (std::runtime_error e) {
    std::cout << "Exception caught!" << std::endl;
    std::cout << e.what() << std::endl;
  };

  jtorch::ShutdownJTorch();

#if defined(WIN32) || defined(_WIN32)
  cout << endl;
  system("PAUSE");
#endif

}
