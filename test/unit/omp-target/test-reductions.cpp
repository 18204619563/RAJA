//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-20, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

///
/// Source file containing tests for RAJA CPU reduction operations.
///

#include "gtest/gtest.h"

#include <iostream>
#include "RAJA/RAJA.hpp"

#include <tuple>

template <typename T>
class ReductionConstructorTestTargetOMP : public ::testing::Test
{
};

TYPED_TEST_SUITE_P(ReductionConstructorTestTargetOMP);

TYPED_TEST_P(ReductionConstructorTestTargetOMP, ReductionConstructor)
{
  using ReducePolicy = typename std::tuple_element<0, TypeParam>::type;
  using NumericType = typename std::tuple_element<1, TypeParam>::type;

  NumericType initVal = 5;

  RAJA::ReduceSum<ReducePolicy, NumericType> reduce_sum(initVal);
  RAJA::ReduceMin<ReducePolicy, NumericType> reduce_min(initVal);
  RAJA::ReduceMax<ReducePolicy, NumericType> reduce_max(initVal);
  RAJA::ReduceMinLoc<ReducePolicy, NumericType> reduce_minloc(initVal, 1);
  RAJA::ReduceMaxLoc<ReducePolicy, NumericType> reduce_maxloc(initVal, 1);

  RAJA::tuple<RAJA::Index_type, RAJA::Index_type> LocTup(1, 1);
  RAJA::ReduceMinLoc<ReducePolicy, NumericType, RAJA::tuple<RAJA::Index_type, RAJA::Index_type>> reduce_minloctup(initVal, LocTup);
  RAJA::ReduceMaxLoc<ReducePolicy, NumericType, RAJA::tuple<RAJA::Index_type, RAJA::Index_type>> reduce_maxloctup(initVal, LocTup);

  ASSERT_EQ((NumericType)reduce_sum.get(), (NumericType)(initVal));
  ASSERT_EQ((NumericType)reduce_min.get(), (NumericType)(initVal));
  ASSERT_EQ((NumericType)reduce_max.get(), (NumericType)(initVal));
  ASSERT_EQ((NumericType)reduce_minloc.get(), (NumericType)(initVal));
  ASSERT_EQ((RAJA::Index_type)reduce_minloc.getLoc(), (RAJA::Index_type)1);
  ASSERT_EQ((NumericType)reduce_maxloc.get(), (NumericType)(initVal));
  ASSERT_EQ((RAJA::Index_type)reduce_maxloc.getLoc(), (RAJA::Index_type)1);

  ASSERT_EQ((NumericType)reduce_minloctup.get(), (NumericType)(initVal));
  ASSERT_EQ((NumericType)reduce_maxloctup.get(), (NumericType)(initVal));
  ASSERT_EQ((RAJA::Index_type)(RAJA::get<0>(reduce_minloctup.getLoc())), (RAJA::Index_type)1);
  ASSERT_EQ((RAJA::Index_type)(RAJA::get<1>(reduce_minloctup.getLoc())), (RAJA::Index_type)1);
  ASSERT_EQ((RAJA::Index_type)(RAJA::get<0>(reduce_maxloctup.getLoc())), (RAJA::Index_type)1);
  ASSERT_EQ((RAJA::Index_type)(RAJA::get<1>(reduce_maxloctup.getLoc())), (RAJA::Index_type)1);
}

REGISTER_TYPED_TEST_SUITE_P(ReductionConstructorTestTargetOMP,
                            ReductionConstructor);

using constructor_types =
    ::testing::Types<std::tuple<RAJA::omp_target_reduce, int>,
                     std::tuple<RAJA::omp_target_reduce, float>,
                     std::tuple<RAJA::omp_target_reduce, double>>;


INSTANTIATE_TYPED_TEST_SUITE_P(ReduceBasicTestsTargetOMP,
                              ReductionConstructorTestTargetOMP,
                              constructor_types);

template <typename TUPLE>
class ReductionCorrectnessTestTargetOMP : public ::testing::Test
{
protected:
  virtual void SetUp()
  {
    array_length = 102;

    array = RAJA::allocate_aligned_type<double>(RAJA::DATA_ALIGN,
                                                array_length * sizeof(double));

    for (int i = 1; i < array_length - 1; ++i) {
      array[i] = (RAJA::Real_type)i;
    }
    array[0] = 0.0;
    array[array_length - 1] = -1.0;
#pragma omp target enter data map(to : array[:array_length])

    sum = 0.0;
    min = array_length * 2;
    max = 0.0;
    minloc = -1;
    maxloc = -1;

    for (int i = 0; i < array_length; ++i) {
      RAJA::Real_type val = array[i];

      sum += val;

      if (val > max) {
        max = val;
        maxloc = i;
      }

      if (val < min) {
        min = val;
        minloc = i;
      }
    }
  }

  virtual void TearDown()
  {
#pragma omp target exit data map(release : array[:array_length])
    free(array);
  }

  RAJA::Real_ptr array;

  RAJA::Real_type max;
  RAJA::Real_type min;
  RAJA::Real_type sum;
  RAJA::Real_type maxloc;
  RAJA::Real_type minloc;

  RAJA::Index_type array_length;
};
TYPED_TEST_SUITE_P(ReductionCorrectnessTestTargetOMP);

TYPED_TEST_P(ReductionCorrectnessTestTargetOMP, ReduceSum)
{
  using ExecPolicy = typename std::tuple_element<0, TypeParam>::type;
  using ReducePolicy = typename std::tuple_element<1, TypeParam>::type;

  RAJA::ReduceSum<ReducePolicy, double> sum_reducer(0.0);

  auto array = this->array;
  RAJA::forall<ExecPolicy>(RAJA::RangeSegment(0, this->array_length),
                           [=](int i) { sum_reducer += array[i]; });

  double raja_sum = (double)sum_reducer.get();

  ASSERT_FLOAT_EQ(this->sum, raja_sum);
}

TYPED_TEST_P(ReductionCorrectnessTestTargetOMP, ReduceMin)
{
  using ExecPolicy = typename std::tuple_element<0, TypeParam>::type;
  using ReducePolicy = typename std::tuple_element<1, TypeParam>::type;

  RAJA::ReduceMin<ReducePolicy, double> min_reducer(1024.0);

  auto array = this->array;
  RAJA::forall<ExecPolicy>(RAJA::RangeSegment(0, this->array_length),
                           [=](int i) { min_reducer.min(array[i]); });

  double raja_min = (double)min_reducer.get();

  ASSERT_FLOAT_EQ(this->min, raja_min);
}

TYPED_TEST_P(ReductionCorrectnessTestTargetOMP, ReduceMax)
{
  using ExecPolicy = typename std::tuple_element<0, TypeParam>::type;
  using ReducePolicy = typename std::tuple_element<1, TypeParam>::type;

  RAJA::ReduceMax<ReducePolicy, double> max_reducer(0.0);

  auto array = this->array;
  RAJA::forall<ExecPolicy>(RAJA::RangeSegment(0, this->array_length),
                           [=](int i) { max_reducer.max(array[i]); });

  double raja_max = (double)max_reducer.get();

  ASSERT_FLOAT_EQ(this->max, raja_max);
}

TYPED_TEST_P(ReductionCorrectnessTestTargetOMP, ReduceMinLoc)
{
  using ExecPolicy = typename std::tuple_element<0, TypeParam>::type;
  using ReducePolicy = typename std::tuple_element<1, TypeParam>::type;

  RAJA::ReduceMinLoc<ReducePolicy, double> minloc_reducer(1024.0, 0);

  auto array = this->array;
  RAJA::forall<ExecPolicy>(RAJA::RangeSegment(0, this->array_length),
                           [=](int i) { minloc_reducer.minloc(array[i], i); });

  double raja_min = (double)minloc_reducer.get();
  RAJA::Index_type raja_loc = minloc_reducer.getLoc();

  ASSERT_FLOAT_EQ(this->min, raja_min);
  ASSERT_EQ(this->minloc, raja_loc);
}

TYPED_TEST_P(ReductionCorrectnessTestTargetOMP, ReduceMinLocGenericIndex)
{
  using ExecPolicy = typename std::tuple_element<0, TypeParam>::type;
  using ReducePolicy = typename std::tuple_element<1, TypeParam>::type;

  struct Index {
     RAJA::Index_type idx;
     Index() : idx(-1) {}
     Index(RAJA::Index_type idx) : idx(idx) {}
  };

  RAJA::ReduceMinLoc<ReducePolicy, double, Index> minloc_reducer(1024.0, Index(0));

  auto array = this->array;
  // TODO: remove this when compilers (clang-coral and IBM XLC) are no longer
  // broken for lambda capture
#pragma omp target data use_device_ptr(array)
  RAJA::forall<ExecPolicy>(RAJA::RangeSegment(0, this->array_length),
                           [=](int i) { minloc_reducer.minloc(array[i], Index(i)); });

  double raja_min = (double)minloc_reducer.get();
  Index raja_loc = minloc_reducer.getLoc();

  ASSERT_FLOAT_EQ(this->min, raja_min);
  ASSERT_EQ(this->minloc, raja_loc.idx);
}

TYPED_TEST_P(ReductionCorrectnessTestTargetOMP, ReduceMaxLoc)
{
  using ExecPolicy = typename std::tuple_element<0, TypeParam>::type;
  using ReducePolicy = typename std::tuple_element<1, TypeParam>::type;

  RAJA::ReduceMaxLoc<ReducePolicy, double> maxloc_reducer(0.0, -1);

  auto array = this->array;
  RAJA::forall<ExecPolicy>(RAJA::RangeSegment(0, this->array_length),
                           [=](int i) { maxloc_reducer.maxloc(array[i], i); });

  double raja_max = (double)maxloc_reducer.get();
  RAJA::Index_type raja_loc = maxloc_reducer.getLoc();

  ASSERT_FLOAT_EQ(this->max, raja_max);
  ASSERT_EQ(this->maxloc, raja_loc);
}

TYPED_TEST_P(ReductionCorrectnessTestTargetOMP, ReduceMaxLocGenericIndex)
{
  using ExecPolicy = typename std::tuple_element<0, TypeParam>::type;
  using ReducePolicy = typename std::tuple_element<1, TypeParam>::type;

  struct Index {
     RAJA::Index_type idx;
     Index() : idx(-1) {}
     Index(RAJA::Index_type idx) : idx(idx) {}
  };

  RAJA::ReduceMaxLoc<ReducePolicy, double, Index> maxloc_reducer(0.0, Index());

  auto array = this->array;
  // TODO: remove this when compilers (clang-coral and IBM XLC) are no longer
  // broken for lambda capture
#pragma omp target data use_device_ptr(array)
  RAJA::forall<ExecPolicy>(RAJA::RangeSegment(0, this->array_length),
                           [=](int i) { maxloc_reducer.maxloc(array[i], Index(i)); });

  double raja_max = (double)maxloc_reducer.get();
  Index raja_loc = maxloc_reducer.getLoc();

  ASSERT_FLOAT_EQ(this->max, raja_max);
  ASSERT_EQ(this->maxloc, raja_loc.idx);
}

REGISTER_TYPED_TEST_SUITE_P(ReductionCorrectnessTestTargetOMP,
                            ReduceSum,
                            ReduceMin,
                            ReduceMax,
                            ReduceMinLoc,
                            ReduceMinLocGenericIndex,
                            ReduceMaxLoc,
                            ReduceMaxLocGenericIndex);
using types =
    ::testing::Types<std::tuple<RAJA::omp_target_parallel_for_exec<16>,
                                RAJA::omp_target_reduce>,
                     std::tuple<RAJA::omp_target_parallel_for_exec<64>,
                                RAJA::omp_target_reduce>,
                     std::tuple<RAJA::omp_target_parallel_for_exec<256>,
                                RAJA::omp_target_reduce>>;

INSTANTIATE_TYPED_TEST_SUITE_P(Reduce, ReductionCorrectnessTestTargetOMP, types);

template <typename TUPLE>
class NestedReductionCorrectnessTestTargetOMP : public ::testing::Test
{
protected:
  virtual void SetUp()
  {
    x_size = 256;
    y_size = 256;
    z_size = 256;

    array = RAJA::allocate_aligned_type<double>(RAJA::DATA_ALIGN,
                                                x_size * y_size * z_size *
                                                    sizeof(double));

    const double val = 4.0 / (x_size * y_size * z_size);

    for (int i = 0; i < (x_size * y_size * z_size); ++i) {
      array[i] = (RAJA::Real_type)val;
    }

#pragma omp target enter data map(to : array[:x_size * y_size * z_size])

    sum = 4.0;
  }

  virtual void TearDown()
  {
#pragma omp target exit data map(release : array[:x_size * y_size * z_size])
    free(array);
  }

  RAJA::Real_ptr array;

  RAJA::Real_type sum;

  RAJA::Index_type x_size;
  RAJA::Index_type y_size;
  RAJA::Index_type z_size;
};


