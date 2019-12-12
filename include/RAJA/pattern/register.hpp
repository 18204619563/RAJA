/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   RAJA header file defining SIMD/SIMT register operations.
 *
 ******************************************************************************
 */

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-19, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#ifndef RAJA_pattern_register_HPP
#define RAJA_pattern_register_HPP

#include "RAJA/config.hpp"

#include "RAJA/util/macros.hpp"

namespace RAJA
{


/*!
 * \file
 * Vector operation functions in the namespace RAJA

 *
 */

  template<typename REGISTER_POLICY, typename T, size_t NUM_ELEM>
  class Register;

  template<typename REGISTER_POLICY, typename T>
  struct RegisterTraits{
      using register_type = REGISTER_POLICY;
      using element_type = T;

      static constexpr size_t s_num_elem = 1;
      static constexpr size_t s_byte_width = sizeof(T);
      static constexpr size_t s_bit_width = s_byte_width*8;
  };


  template<typename ST, typename REGISTER_POLICY, typename RT, size_t NUM_ELEM>
  Register<REGISTER_POLICY, RT, NUM_ELEM>
  operator+(ST x, Register<REGISTER_POLICY, RT, NUM_ELEM> const &y){
    using register_t = Register<REGISTER_POLICY, RT, NUM_ELEM>;
    return register_t(x).add(y);
  }

  template<typename ST, typename REGISTER_POLICY, typename RT, size_t NUM_ELEM>
  Register<REGISTER_POLICY, RT, NUM_ELEM>
  operator-(ST x, Register<REGISTER_POLICY, RT, NUM_ELEM> const &y){
    using register_t = Register<REGISTER_POLICY, RT, NUM_ELEM>;
    return register_t(x).subtract(y);
  }

  template<typename ST, typename REGISTER_POLICY, typename RT, size_t NUM_ELEM>
  Register<REGISTER_POLICY, RT, NUM_ELEM>
  operator*(ST x, Register<REGISTER_POLICY, RT, NUM_ELEM> const &y){
    using register_t = Register<REGISTER_POLICY, RT, NUM_ELEM>;
    return register_t(x).multiply(y);
  }

  template<typename ST, typename REGISTER_POLICY, typename RT, size_t NUM_ELEM>
  Register<REGISTER_POLICY, RT, NUM_ELEM>
  operator/(ST x, Register<REGISTER_POLICY, RT, NUM_ELEM> const &y){
    using register_t = Register<REGISTER_POLICY, RT, NUM_ELEM>;
    return register_t(x).divide(y);
  }

  namespace internal {
  /*!
   * Register base class that provides some default behaviors and simplifies
   * the implementation of new register types.
   *
   * This uses CRTP to provide static polymorphism
   */
  template<typename Derived>
  class RegisterBase;

  template<typename REGISTER_POLICY, typename T, size_t NUM_ELEM>
  class RegisterBase<Register<REGISTER_POLICY, T, NUM_ELEM>>{
    public:
      using self_type = Register<REGISTER_POLICY, T, NUM_ELEM>;
      using element_type = T;

    private:
      RAJA_INLINE
      RAJA_HOST_DEVICE
      self_type *getThis(){
        return static_cast<self_type *>(this);
      }

      RAJA_INLINE
      RAJA_HOST_DEVICE
      constexpr
      self_type const *getThis() const{
        return static_cast<self_type const *>(this);
      }

    public:

      RAJA_HOST_DEVICE
      RAJA_INLINE
      static
      constexpr
      bool is_root() {
        return true;
      }


      /*!
       * @brief Set entire vector to a single scalar value
       * @param value Value to set all vector elements to
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator=(element_type value)
      {
        getThis()->broadcast(value);
        return *this;
      }

      /*!
       * @brief Assign one register to antoher
       * @param x Vector to copy
       * @return Value of (*this)
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator=(self_type const &x)
      {
        getThis()->copy(x);
        return *this;
      }

      /*!
       * @brief Get scalar value from vector register
       * @param i Offset of scalar to get
       * @return Returns scalar value at i
       */
      template<typename IDX>
      constexpr
      RAJA_INLINE
      RAJA_DEVICE
      element_type operator[](IDX i) const
      {
        return getThis()->get(i);
      }

      /*!
       * @brief Add two vector registers
       * @param x Vector to add to this register
       * @return Value of (*this)+x
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type operator+(self_type const &x) const
      {
        return getThis()->add(x);
      }


      /*!
       * @brief Add a vector to this vector
       * @param x Vector to add to this register
       * @return Value of (*this)+x
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator+=(self_type const &x)
      {
        *getThis() = getThis()->add(x);
        return *getThis();
      }

      /*!
       * @brief Subtract two vector registers
       * @param x Vector to subctract from this register
       * @return Value of (*this)+x
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type operator-(self_type const &x) const
      {
        return getThis()->subtract(x);
      }

      /*!
       * @brief Subtract a vector from this vector
       * @param x Vector to subtract from this register
       * @return Value of (*this)+x
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator-=(self_type const &x)
      {
        *getThis() = getThis()->subtract(x);
        return *getThis();
      }

      /*!
       * @brief Multiply two vector registers, element wise
       * @param x Vector to subctract from this register
       * @return Value of (*this)+x
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type operator*(self_type const &x) const
      {
        return getThis()->multiply(x);
      }

      /*!
       * @brief Multiply a vector with this vector
       * @param x Vector to multiple with this register
       * @return Value of (*this)+x
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator*=(self_type const &x)
      {
        *getThis() = getThis()->multiply(x);
        return *getThis();
      }

      /*!
       * @brief Divide two vector registers, element wise
       * @param x Vector to subctract from this register
       * @return Value of (*this)+x
       */
      RAJA_INLINE
      RAJA_HOST_DEVICE
      self_type operator/(self_type const &x) const
      {
        return getThis()->divide(x);
      }

      /*!
       * @brief Divide this vector by another vector
       * @param x Vector to divide by
       * @return Value of (*this)+x
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator/=(self_type const &x)
      {
        *getThis() = getThis()->divide(x);
        return *getThis();
      }


      /*!
       * @brief Dot product of two vectors
       * @param x Other vector to dot with this vector
       * @return Value of (*this) dot x
       */
      RAJA_INLINE
      RAJA_HOST_DEVICE
      element_type dot(self_type const &x) const
      {
        return ((*getThis()) * x).sum();
      }

  };

  }
}  // namespace RAJA




#endif