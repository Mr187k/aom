/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "third_party/googletest/src/include/gtest/gtest.h"

#include "./av1_rtcd.h"
#include "./aom_dsp_rtcd.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/transform_test_base.h"
#include "test/util.h"
#include "aom_ports/mem.h"

using libaom_test::ACMRandom;

namespace {
typedef void (*IhtFunc)(const tran_low_t *in, uint8_t *out, int stride,
                        int tx_type);

using libaom_test::FhtFunc;
using std::tr1::tuple;
typedef tuple<FhtFunc, IhtFunc, int, aom_bit_depth_t, int> Ht8x8Param;

void fht8x8_ref(const int16_t *in, tran_low_t *out, int stride, int tx_type) {
  av1_fht8x8_c(in, out, stride, tx_type);
}

void iht8x8_ref(const tran_low_t *in, uint8_t *out, int stride, int tx_type) {
  av1_iht8x8_64_add_c(in, out, stride, tx_type);
}

#if CONFIG_AOM_HIGHBITDEPTH
typedef void (*IHbdHtFunc)(const tran_low_t *in, uint8_t *out, int stride,
                           int tx_type, int bd);
typedef void (*HbdHtFunc)(const int16_t *input, int32_t *output, int stride,
                          int tx_type, int bd);
// Target optimized function, tx_type, bit depth
typedef tuple<HbdHtFunc, int, int> HighbdHt8x8Param;

void highbd_fht8x8_ref(const int16_t *in, int32_t *out, int stride, int tx_type,
                       int bd) {
  av1_fwd_txfm2d_8x8_c(in, out, stride, tx_type, bd);
}
#endif  // CONFIG_AOM_HIGHBITDEPTH

class AV1Trans8x8HT : public libaom_test::TransformTestBase,
                      public ::testing::TestWithParam<Ht8x8Param> {
 public:
  virtual ~AV1Trans8x8HT() {}

  virtual void SetUp() {
    fwd_txfm_ = GET_PARAM(0);
    inv_txfm_ = GET_PARAM(1);
    tx_type_ = GET_PARAM(2);
    pitch_ = 8;
    height_ = 8;
    fwd_txfm_ref = fht8x8_ref;
    inv_txfm_ref = iht8x8_ref;
    bit_depth_ = GET_PARAM(3);
    mask_ = (1 << bit_depth_) - 1;
    num_coeffs_ = GET_PARAM(4);
  }
  virtual void TearDown() { libaom_test::ClearSystemState(); }

 protected:
  void RunFwdTxfm(const int16_t *in, tran_low_t *out, int stride) {
    fwd_txfm_(in, out, stride, tx_type_);
  }

  void RunInvTxfm(const tran_low_t *out, uint8_t *dst, int stride) {
    inv_txfm_(out, dst, stride, tx_type_);
  }

  FhtFunc fwd_txfm_;
  IhtFunc inv_txfm_;
};

TEST_P(AV1Trans8x8HT, MemCheck) { RunMemCheck(); }
TEST_P(AV1Trans8x8HT, CoeffCheck) { RunCoeffCheck(); }
// Note:
//  TODO(luoyi): Add tx_type, 9-15 for inverse transform.
//  Need cleanup since same tests may be done in fdct8x8_test.cc
// TEST_P(AV1Trans8x8HT, AccuracyCheck) { RunAccuracyCheck(0); }
// TEST_P(AV1Trans8x8HT, InvAccuracyCheck) { RunInvAccuracyCheck(0); }
// TEST_P(AV1Trans8x8HT, InvCoeffCheck) { RunInvCoeffCheck(); }

#if CONFIG_AOM_HIGHBITDEPTH
class AV1HighbdTrans8x8HT : public ::testing::TestWithParam<HighbdHt8x8Param> {
 public:
  virtual ~AV1HighbdTrans8x8HT() {}

  virtual void SetUp() {
    fwd_txfm_ = GET_PARAM(0);
    fwd_txfm_ref_ = highbd_fht8x8_ref;
    tx_type_ = GET_PARAM(1);
    bit_depth_ = GET_PARAM(2);
    mask_ = (1 << bit_depth_) - 1;
    num_coeffs_ = 64;

    input_ = reinterpret_cast<int16_t *>(
        aom_memalign(16, sizeof(int16_t) * num_coeffs_));
    output_ = reinterpret_cast<int32_t *>(
        aom_memalign(16, sizeof(int32_t) * num_coeffs_));
    output_ref_ = reinterpret_cast<int32_t *>(
        aom_memalign(16, sizeof(int32_t) * num_coeffs_));
  }

  virtual void TearDown() {
    aom_free(input_);
    aom_free(output_);
    aom_free(output_ref_);
    libaom_test::ClearSystemState();
  }

 protected:
  void RunBitexactCheck();

 private:
  HbdHtFunc fwd_txfm_;
  HbdHtFunc fwd_txfm_ref_;
  int tx_type_;
  int bit_depth_;
  int mask_;
  int num_coeffs_;
  int16_t *input_;
  int32_t *output_;
  int32_t *output_ref_;
};

void AV1HighbdTrans8x8HT::RunBitexactCheck() {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  int i, j;
  const int stride = 8;
  const int num_tests = 1000;
  const int num_coeffs = 64;

  for (i = 0; i < num_tests; ++i) {
    for (j = 0; j < num_coeffs; ++j) {
      input_[j] = (rnd.Rand16() & mask_) - (rnd.Rand16() & mask_);
    }

    fwd_txfm_ref_(input_, output_ref_, stride, tx_type_, bit_depth_);
    ASM_REGISTER_STATE_CHECK(
        fwd_txfm_(input_, output_, stride, tx_type_, bit_depth_));

    for (j = 0; j < num_coeffs; ++j) {
      EXPECT_EQ(output_ref_[j], output_[j])
          << "Not bit-exact result at index: " << j << " at test block: " << i;
    }
  }
}

TEST_P(AV1HighbdTrans8x8HT, HighbdCoeffCheck) { RunBitexactCheck(); }
#endif  // CONFIG_AOM_HIGHBITDEPTH

using std::tr1::make_tuple;

#if HAVE_SSE2
const Ht8x8Param kArrayHt8x8Param_sse2[] = {
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 0, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 1, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 2, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 3, AOM_BITS_8, 64),
#if CONFIG_EXT_TX
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 4, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 5, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 6, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 7, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 8, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 10, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 11, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 12, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 13, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 14, AOM_BITS_8, 64),
  make_tuple(&av1_fht8x8_sse2, &av1_iht8x8_64_add_sse2, 15, AOM_BITS_8, 64)
#endif  // CONFIG_EXT_TX
};
INSTANTIATE_TEST_CASE_P(SSE2, AV1Trans8x8HT,
                        ::testing::ValuesIn(kArrayHt8x8Param_sse2));
#endif  // HAVE_SSE2

#if HAVE_SSE4_1 && CONFIG_AOM_HIGHBITDEPTH
const HighbdHt8x8Param kArrayHBDHt8x8Param_sse4_1[] = {
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 0, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 0, 12),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 1, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 1, 12),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 2, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 2, 12),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 3, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 3, 12),
#if CONFIG_EXT_TX
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 4, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 4, 12),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 5, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 5, 12),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 6, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 6, 12),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 7, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 7, 12),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 8, 10),
  make_tuple(&av1_fwd_txfm2d_8x8_sse4_1, 8, 12),
#endif  // CONFIG_EXT_TX
};
INSTANTIATE_TEST_CASE_P(SSE4_1, AV1HighbdTrans8x8HT,
                        ::testing::ValuesIn(kArrayHBDHt8x8Param_sse4_1));
#endif  // HAVE_SSE4_1 && CONFIG_AOM_HIGHBITDEPTH

}  // namespace
