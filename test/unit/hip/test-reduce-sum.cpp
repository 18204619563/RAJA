//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-20, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

///
/// Source file containing tests for RAJA GPU reductions.
///

#include <math.h>
#include <cfloat>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <random>

#include "RAJA/RAJA.hpp"
#include "RAJA_gtest.hpp"

using UnitIndexSet = RAJA::TypedIndexSet<RAJA::RangeSegment,
                                         RAJA::ListSegment,
                                         RAJA::RangeStrideSegment>;

constexpr const int TEST_VEC_LEN = 1024 * 1024 * 5;

using namespace RAJA;

static const double dinit_val = 0.1;
static const int iinit_val = 1;

class ReduceSumHIP : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {

    dvalue = (double *) malloc(sizeof(double) * TEST_VEC_LEN);
    hipMalloc((void **)&d_dvalue, sizeof(double) * TEST_VEC_LEN);

    for (int i = 0; i < TEST_VEC_LEN; ++i) {
      dvalue[i] = dinit_val;
    }
    hipMemcpy(d_dvalue, dvalue,
              sizeof(double) * TEST_VEC_LEN,
              hipMemcpyHostToDevice);

    ivalue = (int *) malloc(sizeof(int) * TEST_VEC_LEN);
    hipMalloc((void **)&d_ivalue, sizeof(int) * TEST_VEC_LEN);

    for (int i = 0; i < TEST_VEC_LEN; ++i) {
      ivalue[i] = iinit_val;
    }
    hipMemcpy(d_ivalue, ivalue,
              sizeof(int) * TEST_VEC_LEN,
              hipMemcpyHostToDevice);

    rand_dvalue = (double *) malloc(sizeof(double) * TEST_VEC_LEN);
    hipMalloc((void **)&d_rand_dvalue, sizeof(double) * TEST_VEC_LEN);
  }

  static void TearDownTestCase()
  {
    free(dvalue);
    free(rand_dvalue);
    free(ivalue);
    hipFree(d_dvalue);
    hipFree(d_rand_dvalue);
    hipFree(d_ivalue);
  }

  static double *dvalue;
  static double *d_dvalue;
  static double *rand_dvalue;
  static double *d_rand_dvalue;
  static int *ivalue;
  static int *d_ivalue;
};

double* ReduceSumHIP::dvalue = nullptr;
double* ReduceSumHIP::rand_dvalue = nullptr;
int* ReduceSumHIP::ivalue = nullptr;
double* ReduceSumHIP::d_dvalue = nullptr;
double* ReduceSumHIP::d_rand_dvalue = nullptr;
int* ReduceSumHIP::d_ivalue = nullptr;

const size_t block_size = 256;

GPU_TEST_F(ReduceSumHIP, staggered_sum)
{
  double* dvalue = ReduceSumHIP::d_dvalue;

  double dtinit = 5.0;

  ReduceSum<hip_reduce, double> dsum0(0.0);
  ReduceSum<hip_reduce, double> dsum1(dtinit * 1.0);
  ReduceSum<hip_reduce, double> dsum2(0.0);
  ReduceSum<hip_reduce, double> dsum3(dtinit * 3.0);
  ReduceSum<hip_reduce, double> dsum4(0.0);
  ReduceSum<hip_reduce, double> dsum5(dtinit * 5.0);
  ReduceSum<hip_reduce, double> dsum6(0.0);
  ReduceSum<hip_reduce, double> dsum7(dtinit * 7.0);

  int loops = 2;
  for (int k = 0; k < loops; k++) {

    forall<hip_exec<block_size> >(RangeSegment(0, TEST_VEC_LEN),
                                   [=] RAJA_HOST_DEVICE(int i) {
                                     dsum0 += dvalue[i];
                                     dsum1 += dvalue[i] * 2.0;
                                     dsum2 += dvalue[i] * 3.0;
                                     dsum3 += dvalue[i] * 4.0;
                                     dsum4 += dvalue[i] * 5.0;
                                     dsum5 += dvalue[i] * 6.0;
                                     dsum6 += dvalue[i] * 7.0;
                                     dsum7 += dvalue[i] * 8.0;
                                   });

    double base_chk_val = dinit_val * double(TEST_VEC_LEN) * (k + 1);

    ASSERT_FLOAT_EQ(1 * base_chk_val, dsum0.get());
    ASSERT_FLOAT_EQ(2 * base_chk_val + (dtinit * 1.0), dsum1.get());
    ASSERT_FLOAT_EQ(3 * base_chk_val, dsum2.get());
    ASSERT_FLOAT_EQ(4 * base_chk_val + (dtinit * 3.0), dsum3.get());
    ASSERT_FLOAT_EQ(5 * base_chk_val, dsum4.get());
    ASSERT_FLOAT_EQ(6 * base_chk_val + (dtinit * 5.0), dsum5.get());
    ASSERT_FLOAT_EQ(7 * base_chk_val, dsum6.get());
    ASSERT_FLOAT_EQ(8 * base_chk_val + (dtinit * 7.0), dsum7.get());
  }
}

GPU_TEST_F(ReduceSumHIP, staggered_sum2)
{
  double* dvalue = ReduceSumHIP::d_dvalue;

  double dtinit = 5.0;

  ReduceSum<hip_reduce, double> dsum0(5.0);
  ReduceSum<hip_reduce, double> dsum1;
  ReduceSum<hip_reduce, double> dsum2(5.0);
  ReduceSum<hip_reduce, double> dsum3;
  ReduceSum<hip_reduce, double> dsum4(5.0);
  ReduceSum<hip_reduce, double> dsum5;
  ReduceSum<hip_reduce, double> dsum6(5.0);
  ReduceSum<hip_reduce, double> dsum7;

  dsum0.reset(0.0);
  dsum1.reset(dtinit * 1.0);
  dsum2.reset(0.0);
  dsum3.reset(dtinit * 3.0);
  dsum4.reset(0.0);
  dsum5.reset(dtinit * 5.0);
  dsum6.reset(0.0);
  dsum7.reset(dtinit * 7.0);

  int loops = 2;
  for (int k = 0; k < loops; k++) {

    forall<hip_exec<block_size> >(RangeSegment(0, TEST_VEC_LEN),
                                   [=] RAJA_DEVICE(int i) {
                                     dsum0 += dvalue[i];
                                     dsum1 += dvalue[i] * 2.0;
                                     dsum2 += dvalue[i] * 3.0;
                                     dsum3 += dvalue[i] * 4.0;
                                     dsum4 += dvalue[i] * 5.0;
                                     dsum5 += dvalue[i] * 6.0;
                                     dsum6 += dvalue[i] * 7.0;
                                     dsum7 += dvalue[i] * 8.0;
                                   });

    double base_chk_val = dinit_val * double(TEST_VEC_LEN) * (k + 1);

    ASSERT_FLOAT_EQ(1 * base_chk_val, dsum0.get());
    ASSERT_FLOAT_EQ(2 * base_chk_val + (dtinit * 1.0), dsum1.get());
    ASSERT_FLOAT_EQ(3 * base_chk_val, dsum2.get());
    ASSERT_FLOAT_EQ(4 * base_chk_val + (dtinit * 3.0), dsum3.get());
    ASSERT_FLOAT_EQ(5 * base_chk_val, dsum4.get());
    ASSERT_FLOAT_EQ(6 * base_chk_val + (dtinit * 5.0), dsum5.get());
    ASSERT_FLOAT_EQ(7 * base_chk_val, dsum6.get());
    ASSERT_FLOAT_EQ(8 * base_chk_val + (dtinit * 7.0), dsum7.get());
  }
}


//
// test 3 runs 4 reductions (2 int, 2 double) over disjoint chunks
//        of the array using an indexset with four range segments
//        not aligned with warp boundaries to check that reduction
//        mechanics don't depend on any sort of special indexing.
//
GPU_TEST_F(ReduceSumHIP, indexset_noalign)
{
  double* dvalue = ReduceSumHIP::d_dvalue;
  int* ivalue = ReduceSumHIP::d_ivalue;


  RangeSegment seg0(1, 1230);
  RangeSegment seg1(1237, 3385);
  RangeSegment seg2(4860, 10110);
  RangeSegment seg3(20490, 32003);

  UnitIndexSet iset;
  iset.push_back(seg0);
  iset.push_back(seg1);
  iset.push_back(seg2);
  iset.push_back(seg3);

  double dtinit = 5.0;
  int itinit = 4;

  ReduceSum<hip_reduce, double> dsum0(dtinit * 1.0);
  ReduceSum<hip_reduce, int> isum1(itinit * 2);
  ReduceSum<hip_reduce, double> dsum2(dtinit * 3.0);
  ReduceSum<hip_reduce, int> isum3(itinit * 4);

  forall<ExecPolicy<seq_segit, hip_exec<block_size> > >(
      iset, [=] RAJA_DEVICE(int i) {
        dsum0 += dvalue[i];
        isum1 += 2 * ivalue[i];
        dsum2 += 3 * dvalue[i];
        isum3 += 4 * ivalue[i];
      });

  double dbase_chk_val = dinit_val * double(iset.getLength());
  int ibase_chk_val = iinit_val * double(iset.getLength());

  ASSERT_FLOAT_EQ(double(dsum0), dbase_chk_val + (dtinit * 1.0));
  ASSERT_EQ(int(isum1), 2 * ibase_chk_val + (itinit * 2));
  ASSERT_FLOAT_EQ(double(dsum2), 3 * dbase_chk_val + (dtinit * 3.0));
  ASSERT_EQ(int(isum3), 4 * ibase_chk_val + (itinit * 4));
}

GPU_TEST_F(ReduceSumHIP, atomic_reduce)
{
  double* rand_dvalue = ReduceSumHIP::rand_dvalue;
  double* d_rand_dvalue = ReduceSumHIP::d_rand_dvalue;

  ReduceSum<hip_reduce_atomic, double> dsumN(0.0);
  ReduceSum<hip_reduce_atomic, double> dsumP(0.0);

  double neg_chk_val = 0.0;
  double pos_chk_val = 0.0;

  int loops = 3;

  for (int k = 0; k < loops; k++) {

    for (int i = 0; i < TEST_VEC_LEN; ++i) {
      rand_dvalue[i] = drand48() - 0.5;
      if (rand_dvalue[i] < 0.0) {
        neg_chk_val += rand_dvalue[i];
      } else {
        pos_chk_val += rand_dvalue[i];
      }
    }
    hipMemcpy(d_rand_dvalue, rand_dvalue,
              sizeof(double) * TEST_VEC_LEN,
              hipMemcpyHostToDevice);

    forall<hip_exec<block_size> >(RangeSegment(0, TEST_VEC_LEN),
                                   [=] RAJA_HOST_DEVICE(int i) {
                                     if (d_rand_dvalue[i] < 0.0) {
                                       dsumN += d_rand_dvalue[i];
                                     } else {
                                       dsumP += d_rand_dvalue[i];
                                     }
                                   });

    ASSERT_FLOAT_EQ(dsumN.get(), neg_chk_val);
    ASSERT_FLOAT_EQ(dsumP.get(), pos_chk_val);
  }
}

GPU_TEST_F(ReduceSumHIP, increasing_size)
{
  double* dvalue = ReduceSumHIP::d_dvalue;

  double dtinit = 5.0;

  for (int size = block_size; size <= TEST_VEC_LEN; size += block_size) {

    ReduceSum<hip_reduce, double> dsum0(dtinit);

    forall<hip_exec<block_size, true> >(RangeSegment(0, size),
                                         [=] RAJA_DEVICE(int i) {
                                           dsum0 += dvalue[i];
                                         });

    double base_chk_val = dinit_val * double(size);

    ASSERT_FLOAT_EQ(base_chk_val + dtinit, dsum0.get());
  }
}
