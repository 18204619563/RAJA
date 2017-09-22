/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   Header file containing RAJA reduction templates for CUDA execution.
 *
 *          These methods should work on any platform that supports
 *          CUDA devices.
 *
 ******************************************************************************
 */

#ifndef RAJA_reduce_cuda_HPP
#define RAJA_reduce_cuda_HPP

#include "RAJA/config.hpp"

#if defined(RAJA_ENABLE_CUDA)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016, Lawrence Livermore National Security, LLC.
//
// Produced at the Lawrence Livermore National Laboratory
//
// LLNL-CODE-689114
//
// All rights reserved.
//
// This file is part of RAJA.
//
// For additional details, please also read RAJA/LICENSE.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the disclaimer below.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the disclaimer (as noted below) in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of the LLNS/LLNL nor the names of its contributors may
//   be used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL LAWRENCE LIVERMORE NATIONAL SECURITY,
// LLC, THE U.S. DEPARTMENT OF ENERGY OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#include "RAJA/util/types.hpp"

#include "RAJA/util/basic_mempool.hpp"

#include "RAJA/util/SoAArray.hpp"

#include "RAJA/util/SoAPtr.hpp"

#include "RAJA/pattern/detail/reduce.hpp"

#include "RAJA/pattern/reduce.hpp"

#include "RAJA/policy/cuda/MemUtils_CUDA.hpp"

#include "RAJA/policy/cuda/policy.hpp"

#include "RAJA/policy/cuda/atomic.hpp"

#include "RAJA/policy/cuda/raja_cudaerrchk.hpp"

#include <cuda.h>

#include "RAJA/util/mutex.hpp"

namespace RAJA
{

namespace reduce
{

namespace cuda
{
//! atomic operator version of Combiner object
template <typename Combiner>
struct atomic;

template <typename T>
struct atomic<sum<T>>
{
  RAJA_DEVICE RAJA_INLINE
  void operator()(T &val, const T v)
  {
    RAJA::cuda::atomicAdd(&val, v);
  }
};

template <typename T>
struct atomic<min<T>>
{
  RAJA_DEVICE RAJA_INLINE
  void operator()(T &val, const T v)
  {
    RAJA::cuda::atomicMin(&val, v);
  }
};

template <typename T>
struct atomic<max<T>>
{
  RAJA_DEVICE RAJA_INLINE
  void operator()(T &val, const T v)
  {
    RAJA::cuda::atomicMax(&val, v);
  }
};

} // namespace cuda

} // namespace reduce

namespace cuda
{

namespace impl
{
/*!
 ******************************************************************************
 *
 * \brief Method to shuffle 32b registers in sum reduction for arbitrary type.
 *
 * \Note Returns an undefined value if src lane is inactive (divergence).
 *       Returns this lane's value if src lane is out of bounds or has exited.
 *
 ******************************************************************************
 */
template <typename T>
RAJA_DEVICE RAJA_INLINE
T shfl_xor_sync(T var, int laneMask)
{
  const int int_sizeof_T = (sizeof(T) + sizeof(int) - 1) / sizeof(int);
  union Tint_u {
    T var;
    int arr[int_sizeof_T];
    RAJA_DEVICE constexpr Tint_u(T var_) : var(var_) {};
  };
  Tint_u Tunion(var);

  for (int i = 0; i < int_sizeof_T; ++i) {
#if (__CUDACC_VER_MAJOR__ >= 9)
    Tunion.arr[i] = ::__shfl_xor_sync(0xffffffffu, Tunion.arr[i], laneMask);
#else
    Tunion.arr[i] = ::__shfl_xor(Tunion.arr[i], laneMask);
#endif
  }
  return Tunion.var;
}

template <typename T>
RAJA_DEVICE RAJA_INLINE
T shfl_sync(T var, int srcLane)
{
  const int int_sizeof_T = (sizeof(T) + sizeof(int) - 1) / sizeof(int);
  union Tint_u {
    T var;
    int arr[int_sizeof_T];
    RAJA_DEVICE constexpr Tint_u(T var_) : var(var_) {};
  };
  Tint_u Tunion(var);

  for (int i = 0; i < int_sizeof_T; ++i) {
#if (__CUDACC_VER_MAJOR__ >= 9)
    Tunion.arr[i] = ::__shfl_sync(0xffffffffu, Tunion.arr[i], srcLane);
#else
    Tunion.arr[i] = ::__shfl(Tunion.arr[i], srcLane);
#endif
  }
  return Tunion.var;
}

//! reduce values in block into thread 0
template <typename Combiner, typename T>
RAJA_DEVICE RAJA_INLINE
T block_reduce(T val)
{
  int numThreads = blockDim.x * blockDim.y * blockDim.z;

  int threadId = threadIdx.x + blockDim.x * threadIdx.y
                 + (blockDim.x * blockDim.y) * threadIdx.z;

  int warpId = threadId % WARP_SIZE;
  int warpNum = threadId / WARP_SIZE;

  T temp = val;

  //printf("block_reduce  id %d : temp %f\n",threadId,temp);

  if (numThreads % WARP_SIZE == 0) {

    // reduce each warp
    for (int i = 1; i < WARP_SIZE ; i *= 2) {
      T rhs = shfl_xor_sync(temp, i);
      Combiner{}(temp, rhs);
    }

  } else {

    // reduce each warp
    for (int i = 1; i < WARP_SIZE ; i *= 2) {
      int srcLane = threadId ^ i;
      T rhs = shfl_sync(temp, srcLane);
      // only add from threads that exist (don't double count own value)
      if (srcLane < numThreads) {
        Combiner{}(temp, rhs);
      }
    }

  }

  // reduce per warp values
  if (numThreads > WARP_SIZE) {

    __shared__ RAJA::detail::SoAArray<T, MAX_WARPS> sd;

    // write per warp values to shared memory
    if (warpId == 0) {
      sd.set(warpNum, temp);
    }

    __syncthreads();

    if (warpNum == 0) {

      // read per warp values
      if (warpId*WARP_SIZE < numThreads) {
        temp = sd.get(warpId);
      } else {
        temp = Combiner::identity();
      }

      for (int i = 1; i < WARP_SIZE ; i *= 2) {
        T rhs = shfl_xor_sync(temp, i);
        Combiner{}(temp, rhs);
      }
    }

    __syncthreads();

  }

  return temp;
}


//! reduce values in grid into thread 0 of last running block
//  returns true if put reduced value in val
template <typename Combiner, typename T, typename TempIterator>
RAJA_DEVICE RAJA_INLINE
bool grid_reduce(T& val,
                 TempIterator device_mem,
                 unsigned int* device_count)
{
  int numBlocks = gridDim.x * gridDim.y * gridDim.z;
  int numThreads = blockDim.x * blockDim.y * blockDim.z;
  unsigned int wrap_around = numBlocks - 1;

  int blockId = blockIdx.x + gridDim.x * blockIdx.y
                + (gridDim.x * gridDim.y) * blockIdx.z;

  int threadId = threadIdx.x + blockDim.x * threadIdx.y
                 + (blockDim.x * blockDim.y) * threadIdx.z;

  T temp = block_reduce<Combiner>(val);
  printf("thread %d in grid reduce is calling block reduce for %f\n",threadId,temp);

  // one thread per block writes to device_mem
  bool lastBlock = false;
  if (threadId == 0) {
    device_mem.set(blockId, temp);
    // ensure write visible to all threadblocks
    __threadfence();
    // increment counter, (wraps back to zero if old count == wrap_around)
    unsigned int old_count = ::atomicInc(device_count, wrap_around);
    lastBlock = (old_count == wrap_around);
  }

  // returns non-zero value if any thread passes in a non-zero value
  lastBlock = __syncthreads_or(lastBlock);

  // last block accumulates values from device_mem
  if (lastBlock) {
    temp = Combiner::identity();

    for (int i = threadId; i < numBlocks; i += numThreads) {
      printf("lastBlock accum at %d block : val %f\n",i,device_mem.get(i));
      Combiner{}(temp, device_mem.get(i));
    }

    temp = block_reduce<Combiner>(temp);

    // one thread returns value
    if (threadId == 0) {
      val = temp;
      printf("grid_reduce sets val to %f\n",val);
    }
  }

  return lastBlock && threadId == 0;
}


//! reduce values in grid into thread 0 of last running block
//  returns true if put reduced value in val
template <typename Combiner, typename T>
RAJA_DEVICE RAJA_INLINE
bool grid_reduce_atomic(T& val,
                        T* device_mem,
                        unsigned int* device_count)
{
  int numBlocks = gridDim.x * gridDim.y * gridDim.z;
  unsigned int wrap_around = numBlocks + 1;

  int threadId = threadIdx.x + blockDim.x * threadIdx.y
                 + (blockDim.x * blockDim.y) * threadIdx.z;

  // one thread in first block initializes device_mem
  if (threadId == 0) {
    unsigned int old_val = ::atomicCAS(device_count, 0u, 1u);
    if (old_val == 0u) {
      device_mem[0] = Combiner::identity();
      __threadfence();
      ::atomicAdd(device_count, 1u);
    }
  }

  T temp = block_reduce<Combiner>(val);
  //printf("thread %d in grid reduce is calling block reduce for %f %f\n",threadId,temp,val);

  // one thread per block performs atomic on device_mem
  bool lastBlock = false;
  if (threadId == 0) {
    // thread waits for device_mem to be initialized
    while(static_cast<volatile unsigned int*>(device_count)[0] < 2u);
    __threadfence();
    printf("combining temp = %f from numblocks %d\n",temp,numBlocks);
    RAJA::reduce::cuda::atomic<Combiner>{}(device_mem[0], temp);
    __threadfence();
    // increment counter, (wraps back to zero if old count == wrap_around)
    unsigned int old_count = ::atomicInc(device_count, wrap_around);
    lastBlock = (old_count == wrap_around);

    // last block gets value from device_mem
    if (lastBlock) {
      val = device_mem[0];
      printf("grid_reduce_atomic sets val to %f\n",val);
    }
  }

  return lastBlock;
}

}  // namespace impl

//! Object that manages pinned memory buffers for reduction results
//  use one per reducer object
template <typename T>
class PinnedTally
{
public:
  //! Object put in Pinned memory with value and pointer to next Node
  struct Node {
    Node* next;
    T value;
  };
  //! Object per stream to keep track of pinned memory nodes
  struct StreamNode {
    StreamNode* next;
    cudaStream_t stream;
    Node* node_list;
  };

  //! Iterator over streams used by reducer
  class StreamIterator {
  public:
    StreamIterator() = delete;

    StreamIterator(StreamNode* sn)
      : m_sn(sn)
    {
    }

    const StreamIterator& operator++()
    {
      m_sn = m_sn->next;
      return *this;
    }

    StreamIterator operator++(int)
    {
      StreamIterator ret = *this;
      this->operator++();
      return ret;
    }

    cudaStream_t& operator*()
    {
      return m_sn->stream;
    }

    bool operator==(const StreamIterator& rhs) const
    {
      return m_sn == rhs.m_sn;
    }

    bool operator!=(const StreamIterator& rhs) const
    {
      return !this->operator==(rhs);
    }

  private:
    StreamNode* m_sn;
  };

  //! Iterator over all values generated by reducer
  class StreamNodeIterator {
  public:
    StreamNodeIterator() = delete;

    StreamNodeIterator(StreamNode* sn, Node* n)
      : m_sn(sn), m_n(n)
    {
    }

    const StreamNodeIterator& operator++()
    {
      if (m_n->next) {
        m_n = m_n->next;
      } else if (m_sn->next) {
        m_sn = m_sn->next;
        m_n = m_sn->node_list;
      } else {
        m_sn = nullptr;
        m_n = nullptr;
      }
      return *this;
    }

    StreamNodeIterator operator++(int)
    {
      StreamNodeIterator ret = *this;
      this->operator++();
      return ret;
    }

    T& operator*()
    {
      return m_n->value;
    }

    bool operator==(const StreamNodeIterator& rhs) const
    {
      return m_n == rhs.m_n;
    }

    bool operator!=(const StreamNodeIterator& rhs) const
    {
      return !this->operator==(rhs);
    }

  private:
    StreamNode* m_sn;
    Node* m_n;
  };

  PinnedTally()
    : stream_list(nullptr)
  {

  }

  PinnedTally(const PinnedTally&) = delete;

  //! get begin iterator over streams
  StreamIterator streamBegin()
  {
    return{stream_list};
  }

  //! get end iterator over streams
  StreamIterator streamEnd()
  {
    return{nullptr};
  }

  //! get begin iterator over values
  StreamNodeIterator begin()
  {
    return{stream_list, stream_list ? stream_list->node_list : nullptr};
  }

  //! get end iterator over values
  StreamNodeIterator end()
  {
    return{nullptr, nullptr};
  }

  //! get new value for use in stream
  T* new_value(cudaStream_t stream)
  {
#if defined(RAJA_ENABLE_OPENMP) && defined(_OPENMP)
    lock_guard<omp::mutex> lock(m_mutex);
#endif
    StreamNode* sn = stream_list;
    while(sn) {
      if (sn->stream == stream) break;
      sn = sn->next;
    }
    if (!sn) {
      sn = (StreamNode*)malloc(sizeof(StreamNode));
      sn->next = stream_list;
      sn->stream = stream;
      sn->node_list = nullptr;
      stream_list = sn;
    }
    Node* n = cuda::pinned_mempool_type::getInstance().template malloc<Node>(1);
    n->next = sn->node_list;
    sn->node_list = n;
    printf("new_value begin %p : end %p\n",begin(),end());
    return &n->value;
  }

  //! all values used in all streams
  void free_list()
  {
    while (stream_list) {
      StreamNode* s = stream_list;
      while (s->node_list) {
        Node* n = s->node_list;
        s->node_list = n->next;
        cuda::pinned_mempool_type::getInstance().free(n);
      }
      stream_list = s->next;
      free(s);
    }
  }

  ~PinnedTally()
  {
    free_list();
  }

#if defined(RAJA_ENABLE_OPENMP) && defined(_OPENMP)
  omp::mutex m_mutex;
#endif

private:
  StreamNode* stream_list;
};

//
//////////////////////////////////////////////////////////////////////
//
// Reduction classes.
//
//////////////////////////////////////////////////////////////////////
//

//! Reduction data for Cuda Offload -- stores value, host pointer, and device pointer
template <bool Async, typename Combiner, typename T>
struct Reduce_Data {
  //! union to hold either pointer to PinnedTally or poiter to value
  //  only use list before setup for device and only use val_ptr after
  union tally_u {
    PinnedTally<T>* list;
    T *val_ptr;
    constexpr tally_u(PinnedTally<T>* l) : list(l) {};
    constexpr tally_u(T *v_ptr) : val_ptr(v_ptr) {};
  };

  mutable T value;
  tally_u tally_or_val_ptr;
  unsigned int *device_count;
  RAJA::detail::SoAPtr<T, device_mempool_type> device;
  bool own_device_ptr;

  //! disallow default constructor
  Reduce_Data() = delete;

  /*! \brief create from a default value and offload information
   *
   *  allocates PinnedTally to hold device values
   */
  explicit Reduce_Data(T initValue)
      : value{initValue},
        tally_or_val_ptr{new PinnedTally<T>},
        device_count{nullptr},
        device{},
        own_device_ptr{false}
  {
  }

  RAJA_HOST_DEVICE
  Reduce_Data(const Reduce_Data &other)
      : value{Combiner::identity()},
        tally_or_val_ptr{other.tally_or_val_ptr},
        device_count{other.device_count},
        device{other.device},
        own_device_ptr{false}
  {
  }

  //! delete pinned tally
  RAJA_INLINE
  void destroy()
  {
    delete tally_or_val_ptr.list; tally_or_val_ptr.list = nullptr;
  }

  //! check and setup for device
  //  allocate device pointers and get a new result buffer from the pinned tally
  RAJA_INLINE
  bool setupForDevice()
  {
    bool act = !device.allocated() && setupReducers();
    printf("called setupForDevice with act = %d\n",act);
    if (act) {
      dim3 gridDim = currentGridDim();
      size_t numBlocks = gridDim.x * gridDim.y * gridDim.z;
      device.allocate(numBlocks);
      device_count = device_zeroed_mempool_type::getInstance().template malloc<unsigned int>(1);
      tally_or_val_ptr.val_ptr = tally_or_val_ptr.list->new_value(currentStream());
      own_device_ptr = true;
    }
    printf("returning from setupForDevice with act = %d\n",act);
    return act;
  }

  //! if own resources teardown device setup
  //  free device pointers
  RAJA_INLINE
  void teardownForDevice()
  {
    if(own_device_ptr) {
      device.deallocate();
      device_zeroed_mempool_type::getInstance().free(device_count);  device_count = nullptr;
      tally_or_val_ptr.val_ptr = nullptr;
      own_device_ptr = false;
    }
  }

  //! transfers from the host to the device
  RAJA_INLINE
  void hostToDevice()
  {
  }

  //! transfers from the device to the host
  RAJA_INLINE
  void deviceToHost()
  {
    auto end = tally_or_val_ptr.list->streamEnd();
    for(auto s = tally_or_val_ptr.list->streamBegin(); s != end; ++s) {
      synchronize(*s);
    }
  }

  //! frees all data used
  //  frees all values in the pinned tally
  RAJA_INLINE
  void cleanup()
  {
    tally_or_val_ptr.list->free_list();
  }
};


//! Reduction data for Cuda Offload -- stores value, host pointer
template <bool Async, typename Combiner, typename T>
struct ReduceAtomic_Data {
  //! union to hold either pointer to PinnedTally or poiter to value
  //  only use list before setup for device and only use val_ptr after
  union tally_u {
    PinnedTally<T>* list;
    T *val_ptr;
    constexpr tally_u(PinnedTally<T>* l) : list(l) {};
    constexpr tally_u(T *v_ptr) : val_ptr(v_ptr) {};
  };

  mutable T value;
  tally_u tally_or_val_ptr;
  unsigned int* device_count;
  T* device;
  T* tidVal;
  bool own_device_ptr;

  //! disallow default constructor
  ReduceAtomic_Data() = delete;

  /*! \brief create from a default value and offload information
   *
   *  allocates PinnedTally to hold device values
   */
  explicit ReduceAtomic_Data(T initValue)
      : value{initValue},
        tally_or_val_ptr{new PinnedTally<T>},
        device_count{nullptr},
        device{nullptr},
        tidVal(nullptr),
        own_device_ptr{false}
  {
  }

  RAJA_HOST_DEVICE
  ReduceAtomic_Data(const ReduceAtomic_Data &other)
      : value{Combiner::identity()},
        tally_or_val_ptr{other.tally_or_val_ptr},
        device_count{other.device_count},
        device{other.device},
        tidVal{other.tidVal},
        own_device_ptr{false}
  {
  }

  //! delete pinned tally
  RAJA_INLINE
  void destroy()
  {
    delete tally_or_val_ptr.list; tally_or_val_ptr.list = nullptr;
  }

  //! check and setup for device
  //  allocate device pointers and get a new result buffer from the pinned tally
  RAJA_INLINE
  bool setupForDevice()
  {
    bool act = !device && setupReducers();
    //printf("called ReduceAtomic_Data setupForDevice with act = %d\n",act);
    if (act) {
      device = device_mempool_type::getInstance().template malloc<T>(1);
      device_count = device_zeroed_mempool_type::getInstance().template malloc<unsigned int>(1);
      tidVal = device_zeroed_mempool_type::getInstance().template malloc<T>(256); // Eventually pass in template arg BLOCK_SIZE
      tally_or_val_ptr.val_ptr = tally_or_val_ptr.list->new_value(currentStream());
      //tally_or_val_ptr.list->new_value(currentStream());
      { //diag
        auto n = tally_or_val_ptr.list->begin();
        auto end = tally_or_val_ptr.list->end();
        printf("tally begin %p : end %p\n",n,end);
        printf("tally not using auto:  begin %p : end %p\n", tally_or_val_ptr.list->begin(),tally_or_val_ptr.list->end());
 
        if(n != end) 
          printf("tally for this %p has been advanced \n",this);
        else
          printf("tally n == end\n");
      }  

      own_device_ptr = true;
    }
    //printf("returning from ReduceAtomic_Data setupForDevice with act = %d\n",act);
    return act;
  }

  //! if own resources teardown device setup
  //  free device pointers
  RAJA_INLINE
  void teardownForDevice()
  {
    printf("ReduceAtomic teardownForDevice\n");
    if(own_device_ptr) {
      device_mempool_type::getInstance().free(device);  device = nullptr;
      device_zeroed_mempool_type::getInstance().free(device_count);  device_count = nullptr;
      tally_or_val_ptr.val_ptr = nullptr;
      own_device_ptr = false;
    }
  }

  //! transfers from the host to the device
  RAJA_INLINE
  void hostToDevice()
  {
  }

  //! transfers from the device to the host
  RAJA_INLINE
  void deviceToHost()
  {
    printf("ReduceAtomic_Data calling deviceToHost\n");
    auto end = tally_or_val_ptr.list->streamEnd();
    for(auto s = tally_or_val_ptr.list->streamBegin(); s != end; ++s) {
      synchronize(*s);
    }
  }

  //! frees all data used
  //  frees all values in the pinned tally
  RAJA_INLINE
  void cleanup()
  {
    tally_or_val_ptr.list->free_list();
  }
};

//! Cuda Reduction entity -- generalize on reduction, and type
template <bool Async, typename Combiner, typename T>
struct Reduce {
  Reduce() = delete;

  //! create a reduce object
  //  the original object's parent is itself
  explicit Reduce(T init_val)
      : parent{this},
        val(init_val)
  {
  }

  //! copy and on host attempt to setup for device
  RAJA_HOST_DEVICE
  Reduce(const Reduce & other)
#if !defined(__CUDA_ARCH__)
      : parent{other.parent},
#else
      : parent{&other},
#endif
        val(other.val)
  {
#if !defined(__CUDA_ARCH__)
    if (parent) {
      if (val.setupForDevice()) {
        parent = nullptr;
      }
    }
#endif
  }

  //! apply reduction upon destruction and cleanup resources owned by this copy
  //  on device store in pinned buffer on host
  RAJA_HOST_DEVICE
  ~Reduce()
  {
#if !defined(__CUDA_ARCH__)
    if (parent == this) {
      val.destroy();
    } else if (parent) {
#if defined(RAJA_ENABLE_OPENMP) && defined(_OPENMP)
      lock_guard<omp::mutex> lock(val.tally_or_val_ptr.list->m_mutex);
#endif
      parent->combine(val.value);
    } else {
      val.teardownForDevice();
    }
#else
    if (!parent->parent) {

      T temp = val.value;

      if (impl::grid_reduce<Combiner>(temp, val.device,
                                     val.device_count)) {
        val.tally_or_val_ptr.val_ptr[0] = temp;
      }
    } else {
      parent->combine(val.value);
    }
#endif
  }

  //! map result value back to host if not done already; return aggregate value
  operator T()
  {
    auto n = val.tally_or_val_ptr.list->begin();
    auto end = val.tally_or_val_ptr.list->end();
    if (n != end) {
      val.deviceToHost();
      for ( ; n != end; ++n) {
        Combiner{}(val.value, *n);
      }
      val.cleanup();
    }
    return val.value;
  }
  //! alias for operator T()
  T get() { return operator T(); }

  bool auxSetup() { return val.setupForDevice();}

  //! apply reduction
  RAJA_HOST_DEVICE
  Reduce &combine(T rhsVal)
  {
    Combiner{}(val.value, rhsVal);
    return *this;
  }

  //! apply reduction (const version) -- still combines internal values
  RAJA_HOST_DEVICE
  const Reduce &combine(T rhsVal) const
  {
    using NonConst = typename std::remove_const<decltype(this)>::type;
    auto ptr = const_cast<NonConst>(this);
    Combiner{}(ptr->val.value,rhsVal);
    return *this;
  }

private:
  const Reduce<Async, Combiner, T>* parent;
  //! storage for reduction data (host ptr, device ptr, value)
  cuda::Reduce_Data<Async, Combiner, T> val;
};


//! Cuda Reduction Atomic entity -- generalize on reduction, and type
template <bool Async, typename Combiner, typename T>
struct ReduceAtomic {
  ReduceAtomic() = delete;

  //! create a reduce object
  //  the original object's parent is itself
  explicit ReduceAtomic(T init_val)
      : parent{this},
        val{init_val}
  {
  }

  //! copy and on host attempt to setup for device
  //  on device initialize device memory
  RAJA_HOST_DEVICE
  ReduceAtomic(const ReduceAtomic & other)
#if !defined(__CUDA_ARCH__)
      : parent{other.parent},
#else
      : parent{&other},
#endif
        val(other.val)
  {
#if !defined(__CUDA_ARCH__)
    if (parent) {
      if (val.setupForDevice()) {
        parent = nullptr;
      }
    }
#endif
  }

  //! apply reduction upon destruction and cleanup resources owned by this copy
  //  on device store in pinned buffer on host
  RAJA_HOST_DEVICE
  ~ReduceAtomic()
  {
#if !defined(__CUDA_ARCH__)
    if (parent == this) {
      val.destroy();
    } else if (parent) {
#if defined(RAJA_ENABLE_OPENMP) && defined(_OPENMP)
      lock_guard<omp::mutex> lock(val.tally_or_val_ptr.list->m_mutex);
#endif
      parent->combine(val.value);
    } else {
      //val.teardownForDevice();
    }
#else
    if (!parent->parent) {
      int threadId = threadIdx.x + blockDim.x * threadIdx.y
                 + (blockDim.x * blockDim.y) * threadIdx.z;

      //T temp = val.value; // CAREFUL!!!  restore original code 
      T temp = val.tidVal[threadId]; // supports ReducerArray
      //printf("~ReduceAtomic temp = %f\n",temp);

      if (impl::grid_reduce_atomic<Combiner>(temp, val.device,
                                            val.device_count)) {
        val.tally_or_val_ptr.val_ptr[0] = temp;
      }
    } else {
      parent->combine(val.value); //debugging remove comment
    }
#endif
  }

  //! map result value back to host if not done already; return aggregate value
  operator T()
  {
    printf("ReduceAtomic operator T()\n");
    auto n = val.tally_or_val_ptr.list->begin();
    auto end = val.tally_or_val_ptr.list->end();
    if (n != end) {
      printf("ReduceAtomic operator T() n != end\n");
      val.deviceToHost();
      for ( ; n != end; ++n) {
        Combiner{}(val.value, *n);
      }
      val.cleanup();
    }
    return val.value;
  }
  //! alias for operator T()
  T get() { return operator T(); }


  bool auxSetup() {
    //printf("called auxSetup for %p\n",this);
    return val.setupForDevice();
  }

  RAJA_HOST_DEVICE
  void setParent(ReduceAtomic<Async, Combiner, T>* pValue){
    parent = pValue;
  }

  //! apply reduction
  RAJA_HOST_DEVICE
  ReduceAtomic &combine(T rhsVal)
  {
#if !defined(__CUDA_ARCH__)
    printf("Host ReduceAtomic combine rhsVal = %f\n",rhsVal);
    Combiner{}(val.value, rhsVal); 
#else
    int threadId = threadIdx.x + blockDim.x * threadIdx.y
                 + (blockDim.x * blockDim.y) * threadIdx.z;
    printf("Device ReduceAtomic combine rhsVal = %f from tid %d\n",rhsVal,threadId);
    Combiner{}(val.tidVal[threadId], rhsVal); 
#endif
    return *this;
  }

  //! apply reduction (const version) -- still combines internal values
  RAJA_HOST_DEVICE
  const ReduceAtomic &combine(T rhsVal) const
  {
    printf("ReduceAtomic const combine : rhsVal %f\n",rhsVal);
    using NonConst = typename std::remove_const<decltype(this)>::type;
    auto ptr = const_cast<NonConst>(this);
    Combiner{}(ptr->val.value,rhsVal);
    return *this;
  }

private:
  const ReduceAtomic<Async, Combiner, T>* parent;
  //! storage for reduction data (host ptr, device ptr, value)
  cuda::ReduceAtomic_Data<Async, Combiner, T> val;
};

}  // end namespace cuda

//! specialization of ReduceSum for cuda_reduce
template <size_t BLOCK_SIZE, bool Async, typename T>
struct ReduceSum<cuda_reduce<BLOCK_SIZE, Async>, T>
    : public cuda::Reduce<Async, RAJA::reduce::sum<T>, T> {
  using self = ReduceSum<cuda_reduce<BLOCK_SIZE, Async>, T>;
  using Base = cuda::Reduce<Async, RAJA::reduce::sum<T>, T>;
  using Base::Base;
  //! enable operator+= for ReduceSum -- alias for combine()
  RAJA_HOST_DEVICE
  self &operator+=(T rhsVal)
  {
    Base::combine(rhsVal);
    return *this;
  }
  //! enable operator+= for ReduceSum -- alias for combine()
  RAJA_HOST_DEVICE
  const self &operator+=(T rhsVal) const
  {
    Base::combine(rhsVal);
    return *this;
  }
};

//! specialization of ReduceSum for cuda_reduce_atomic
template <size_t BLOCK_SIZE, bool Async, typename T>
struct ReduceSum<cuda_reduce_atomic<BLOCK_SIZE, Async>, T>
    : public cuda::ReduceAtomic<Async, RAJA::reduce::sum<T>, T> {
  using self = ReduceSum<cuda_reduce_atomic<BLOCK_SIZE, Async>, T>;
  using Base = cuda::ReduceAtomic<Async, RAJA::reduce::sum<T>, T>;
  using Base::Base;
  //! enable operator+= for ReduceSum -- alias for combine()
  RAJA_HOST_DEVICE
  self &operator+=(T rhsVal)
  {
    //printf("Summing %f from reducer %p\n",rhsVal,this);
    Base::combine(rhsVal);
    return *this;
  }
  //! enable operator+= for ReduceSum -- alias for combine()
  RAJA_HOST_DEVICE
  const self &operator+=(T rhsVal) const
  {
    Base::combine(rhsVal);
    return *this;
  }
};

//! specialization of ReduceMin for cuda_reduce_atomic
template <size_t BLOCK_SIZE, bool Async, typename T>
struct ReduceMin<cuda_reduce_atomic<BLOCK_SIZE, Async>, T>
    : public cuda::ReduceAtomic<Async, RAJA::reduce::min<T>, T> {
  using self = ReduceMin<cuda_reduce_atomic<BLOCK_SIZE, Async>, T>;
  using Base = cuda::ReduceAtomic<Async, RAJA::reduce::min<T>, T>;
  using Base::Base;
  //! enable min() for ReduceMin -- alias for combine()
  RAJA_HOST_DEVICE
  self &min(T rhsVal)
  {
    Base::combine(rhsVal);
    return *this;
  }
  //! enable min() for ReduceMin -- alias for combine()
  RAJA_HOST_DEVICE
  const self &min(T rhsVal) const
  {
    Base::combine(rhsVal);
    return *this;
  }
};

//! specialization of ReduceMin for cuda_reduce
template <size_t BLOCK_SIZE, bool Async, typename T>
struct ReduceMin<cuda_reduce<BLOCK_SIZE, Async>, T>
    : public cuda::Reduce<Async, RAJA::reduce::min<T>, T> {
  using self = ReduceMin<cuda_reduce<BLOCK_SIZE, Async>, T>;
  using Base = cuda::Reduce<Async, RAJA::reduce::min<T>, T>;
  using Base::Base;
  //! enable min() for ReduceMin -- alias for combine()
  RAJA_HOST_DEVICE
  self &min(T rhsVal)
  {
    Base::combine(rhsVal);
    return *this;
  }
  //! enable min() for ReduceMin -- alias for combine()
  RAJA_HOST_DEVICE
  const self &min(T rhsVal) const
  {
    Base::combine(rhsVal);
    return *this;
  }
};

//! specialization of ReduceMax for cuda_reduce_atomic
template <size_t BLOCK_SIZE, bool Async, typename T>
struct ReduceMax<cuda_reduce_atomic<BLOCK_SIZE, Async>, T>
    : public cuda::ReduceAtomic<Async, RAJA::reduce::max<T>, T> {
  using self = ReduceMax<cuda_reduce_atomic<BLOCK_SIZE, Async>, T>;
  using Base = cuda::ReduceAtomic<Async, RAJA::reduce::max<T>, T>;
  using Base::Base;
  //! enable max() for ReduceMax -- alias for combine()
  RAJA_HOST_DEVICE
  self &max(T rhsVal)
  {
    Base::combine(rhsVal);
    return *this;
  }
  //! enable max() for ReduceMax -- alias for combine()
  RAJA_HOST_DEVICE
  const self &max(T rhsVal) const
  {
    Base::combine(rhsVal);
    return *this;
  }
};

//! specialization of ReduceMax for cuda_reduce
template <size_t BLOCK_SIZE, bool Async, typename T>
struct ReduceMax<cuda_reduce<BLOCK_SIZE, Async>, T>
    : public cuda::Reduce<Async, RAJA::reduce::max<T>, T> {
  using self = ReduceMax<cuda_reduce<BLOCK_SIZE, Async>, T>;
  using Base = cuda::Reduce<Async, RAJA::reduce::max<T>, T>;
  using Base::Base;
  //! enable max() for ReduceMax -- alias for combine()
  RAJA_HOST_DEVICE
  self &max(T rhsVal)
  {
    Base::combine(rhsVal);
    return *this;
  }
  //! enable max() for ReduceMax -- alias for combine()
  RAJA_HOST_DEVICE
  const self &max(T rhsVal) const
  {
    Base::combine(rhsVal);
    return *this;
  }
};

//! specialization of ReduceMinLoc for cuda_reduce
template <size_t BLOCK_SIZE, bool Async, typename T>
struct ReduceMinLoc<cuda_reduce<BLOCK_SIZE, Async>, T>
    : public cuda::Reduce<Async, RAJA::reduce::min<RAJA::reduce::detail::ValueLoc<T>>, RAJA::reduce::detail::ValueLoc<T>> {
  using value_type = RAJA::reduce::detail::ValueLoc<T>;
  using Base = cuda::Reduce<Async, RAJA::reduce::min<value_type>, value_type>;
  using Base::Base;

  //! constructor requires a default value for the reducer
  explicit ReduceMinLoc(T init_val, Index_type init_idx)
      : Base(value_type(init_val, init_idx))
  {
  }
  //! reducer function; updates the current instance's state
  RAJA_HOST_DEVICE
  ReduceMinLoc &minloc(T rhs, Index_type loc)
  {
    Base::combine(value_type(rhs, loc));
    return *this;
  }
  //! reducer function; updates the current instance's state
  RAJA_HOST_DEVICE
  const ReduceMinLoc &minloc(T rhs, Index_type loc) const
  {
    Base::combine(value_type(rhs, loc));
    return *this;
  }

  //! Get the calculated reduced value
  Index_type getLoc() { return Base::get().getLoc(); }

  //! Get the calculated reduced value
  T get() { return Base::get(); }

  //! Get the calculated reduced value
  operator T() { return Base::get(); }

};

//! specialization of ReduceMaxLoc for cuda_reduce
template <size_t BLOCK_SIZE, bool Async, typename T>
struct ReduceMaxLoc<cuda_reduce<BLOCK_SIZE, Async>, T>
    : public cuda::Reduce<Async, RAJA::reduce::max<RAJA::reduce::detail::ValueLoc<T, false>>, RAJA::reduce::detail::ValueLoc<T, false>> {
  using self = ReduceMaxLoc<cuda_reduce<BLOCK_SIZE, Async>, T>;
  using value_type = RAJA::reduce::detail::ValueLoc<T, false>;
  using Base = cuda::Reduce<Async, RAJA::reduce::max<value_type>, value_type>;
  using Base::Base;

  //! constructor requires a default value for the reducer
  explicit ReduceMaxLoc(T init_val, Index_type init_idx)
      : Base(value_type(init_val, init_idx))
  {
  }
  //! reducer function; updates the current instance's state
  RAJA_HOST_DEVICE
  ReduceMaxLoc &maxloc(T rhs, Index_type loc)
  {
    Base::combine(value_type(rhs, loc));
    return *this;
  }
  //! reducer function; updates the current instance's state
  RAJA_HOST_DEVICE
  const ReduceMaxLoc &maxloc(T rhs, Index_type loc) const
  {
    Base::combine(value_type(rhs, loc));
    return *this;
  }

  //! Get the calculated reduced value
  Index_type getLoc() { return Base::get().getLoc(); }

  //! Get the calculated reduced value
  T get() { return Base::get(); }

  //! Get the calculated reduced value
  operator T() { return Base::get(); }

};

}  // closing brace for RAJA namespace

#endif  // closing endif for RAJA_ENABLE_CUDA guard

#endif  // closing endif for header file include guard
