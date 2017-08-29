/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_BitScanReverse)
#endif

#include "arrow/util/int128.h"
#include "arrow/util/logging.h"

namespace arrow {
namespace decimal {

static constexpr uint64_t kIntMask = 0xFFFFFFFF;
static constexpr auto kCarryBit = static_cast<uint64_t>(1) << static_cast<uint64_t>(32);

Int128::Int128(const std::string& str) : Int128() {
  const size_t length = str.length();

  if (length > 0) {
    bool is_negative = str[0] == '-';
    auto posn = static_cast<size_t>(is_negative);

    while (posn < length) {
      const size_t group = std::min(static_cast<size_t>(18), length - posn);
      const auto chunk = static_cast<int64_t>(std::stoll(str.substr(posn, group)));
      const auto multiple =
          static_cast<int64_t>(std::pow(10.0, static_cast<double>(group)));

      *this *= multiple;
      *this += chunk;

      posn += group;
    }

    if (is_negative) {
      Negate();
    }
  }
}

Int128::Int128(const uint8_t* bytes)
    : Int128(reinterpret_cast<const int64_t*>(bytes)[0],
             reinterpret_cast<const uint64_t*>(bytes)[1]) {}

void Int128::ToBytes(uint8_t** out) const {
  DCHECK_NE(out, nullptr) << "Cannot fill nullptr of bytes from Int128";
  DCHECK_NE(*out, nullptr) << "Cannot fill nullptr of bytes from Int128";
  const uint64_t raw[] = {static_cast<uint64_t>(high_bits_), low_bits_};
  std::memcpy(*out, raw, 16);
}

Int128& Int128::Negate() {
  low_bits_ = ~low_bits_ + 1;
  high_bits_ = ~high_bits_;
  if (low_bits_ == 0) {
    ++high_bits_;
  }
  return *this;
}

Int128& Int128::operator+=(const Int128& right) {
  const uint64_t sum = low_bits_ + right.low_bits_;
  high_bits_ += right.high_bits_;
  if (sum < low_bits_) {
    ++high_bits_;
  }
  low_bits_ = sum;
  return *this;
}

Int128& Int128::operator-=(const Int128& right) {
  const uint64_t diff = low_bits_ - right.low_bits_;
  high_bits_ -= right.high_bits_;
  if (diff > low_bits_) {
    --high_bits_;
  }
  low_bits_ = diff;
  return *this;
}

Int128& Int128::operator/=(const Int128& right) {
  Int128 remainder;
  DCHECK(Divide(right, this, &remainder).ok());
  return *this;
}

Int128::operator char() const {
  DCHECK(high_bits_ == 0 || high_bits_ == -1)
      << "Trying to cast an Int128 greater than the value range of a "
         "char. high_bits_ must be equal to 0 or -1, got: "
      << high_bits_;
  DCHECK_LE(low_bits_, std::numeric_limits<char>::max())
      << "low_bits_ too large for C type char, got: " << low_bits_;
  return static_cast<char>(low_bits_);
}

Int128& Int128::operator|=(const Int128& right) {
  low_bits_ |= right.low_bits_;
  high_bits_ |= right.high_bits_;
  return *this;
}

Int128& Int128::operator&=(const Int128& right) {
  low_bits_ &= right.low_bits_;
  high_bits_ &= right.high_bits_;
  return *this;
}

Int128& Int128::operator<<=(uint32_t bits) {
  if (bits != 0) {
    if (bits < 64) {
      high_bits_ <<= bits;
      high_bits_ |= (low_bits_ >> (64 - bits));
      low_bits_ <<= bits;
    } else if (bits < 128) {
      high_bits_ = static_cast<int64_t>(low_bits_) << (bits - 64);
      low_bits_ = 0;
    } else {
      high_bits_ = 0;
      low_bits_ = 0;
    }
  }
  return *this;
}

Int128& Int128::operator>>=(uint32_t bits) {
  if (bits != 0) {
    if (bits < 64) {
      low_bits_ >>= bits;
      low_bits_ |= static_cast<uint64_t>(high_bits_ << (64 - bits));
      high_bits_ = static_cast<int64_t>(static_cast<uint64_t>(high_bits_) >> bits);
    } else if (bits < 128) {
      low_bits_ = static_cast<uint64_t>(high_bits_ >> (bits - 64));
      high_bits_ = static_cast<int64_t>(high_bits_ >= 0L ? 0L : -1L);
    } else {
      high_bits_ = static_cast<int64_t>(high_bits_ >= 0L ? 0L : -1L);
      low_bits_ = static_cast<uint64_t>(high_bits_);
    }
  }
  return *this;
}

Int128& Int128::operator*=(const Int128& right) {
  // Break the left and right numbers into 32 bit chunks
  // so that we can multiply them without overflow.
  const uint64_t L0 = static_cast<uint64_t>(high_bits_) >> 32;
  const uint64_t L1 = static_cast<uint64_t>(high_bits_) & kIntMask;
  const uint64_t L2 = low_bits_ >> 32;
  const uint64_t L3 = low_bits_ & kIntMask;

  const uint64_t R0 = static_cast<uint64_t>(right.high_bits_) >> 32;
  const uint64_t R1 = static_cast<uint64_t>(right.high_bits_) & kIntMask;
  const uint64_t R2 = right.low_bits_ >> 32;
  const uint64_t R3 = right.low_bits_ & kIntMask;

  uint64_t product = L3 * R3;
  low_bits_ = product & kIntMask;

  uint64_t sum = product >> 32;

  product = L2 * R3;
  sum += product;

  product = L3 * R2;
  sum += product;

  low_bits_ += sum << 32;

  high_bits_ = static_cast<int64_t>(sum < product ? kCarryBit : 0);
  if (sum < product) {
    high_bits_ += kCarryBit;
  }

  high_bits_ += static_cast<int64_t>(sum >> 32);
  high_bits_ += L1 * R3 + L2 * R2 + L3 * R1;
  high_bits_ += (L0 * R3 + L1 * R2 + L2 * R1 + L3 * R0) << 32;
  return *this;
}

/// Expands the given value into an array of ints so that we can work on
/// it. The array will be converted to an absolute value and the wasNegative
/// flag will be set appropriately. The array will remove leading zeros from
/// the value.
/// \param array an array of length 4 to set with the value
/// \param was_negative a flag for whether the value was original negative
/// \result the output length of the array
static int64_t FillInArray(const Int128& value, uint32_t* array, bool& was_negative) {
  uint64_t high;
  uint64_t low;
  const int64_t highbits = value.high_bits();
  const uint64_t lowbits = value.low_bits();

  if (highbits < 0) {
    low = ~lowbits + 1;
    high = static_cast<uint64_t>(~highbits);
    if (low == 0) {
      ++high;
    }
    was_negative = true;
  } else {
    low = lowbits;
    high = static_cast<uint64_t>(highbits);
    was_negative = false;
  }

  if (high != 0) {
    if (high > std::numeric_limits<uint32_t>::max()) {
      array[0] = static_cast<uint32_t>(high >> 32);
      array[1] = static_cast<uint32_t>(high);
      array[2] = static_cast<uint32_t>(low >> 32);
      array[3] = static_cast<uint32_t>(low);
      return 4;
    }

    array[0] = static_cast<uint32_t>(high);
    array[1] = static_cast<uint32_t>(low >> 32);
    array[2] = static_cast<uint32_t>(low);
    return 3;
  }

  if (low >= std::numeric_limits<uint32_t>::max()) {
    array[0] = static_cast<uint32_t>(low >> 32);
    array[1] = static_cast<uint32_t>(low);
    return 2;
  }

  if (low == 0) {
    return 0;
  }

  array[0] = static_cast<uint32_t>(low);
  return 1;
}

/// \brief Find last set bit in a 32 bit integer. Bit 1 is the LSB and bit 32 is the MSB.
static int64_t FindLastSetBit(uint32_t value) {
#if defined(__clang__) || defined(__GNUC__)
  // Count leading zeros
  return __builtin_clz(value) + 1;
#elif defined(_MSC_VER)
  unsigned long index;                                         // NOLINT
  _BitScanReverse(&index, static_cast<unsigned long>(value));  // NOLINT
  return static_cast<int64_t>(index + 1UL);
#endif
}

/// Shift the number in the array left by bits positions.
/// \param array the number to shift, must have length elements
/// \param length the number of entries in the array
/// \param bits the number of bits to shift (0 <= bits < 32)
static void ShiftArrayLeft(uint32_t* array, int64_t length, int64_t bits) {
  if (length > 0 && bits != 0) {
    for (int64_t i = 0; i < length - 1; ++i) {
      array[i] = (array[i] << bits) | (array[i + 1] >> (32 - bits));
    }
    array[length - 1] <<= bits;
  }
}

/// Shift the number in the array right by bits positions.
/// \param array the number to shift, must have length elements
/// \param length the number of entries in the array
/// \param bits the number of bits to shift (0 <= bits < 32)
static void ShiftArrayRight(uint32_t* array, int64_t length, int64_t bits) {
  if (length > 0 && bits != 0) {
    for (int64_t i = length - 1; i > 0; --i) {
      array[i] = (array[i] >> bits) | (array[i - 1] << (32 - bits));
    }
    array[0] >>= bits;
  }
}

/// \brief Fix the signs of the result and remainder at the end of the division based on
/// the signs of the dividend and divisor.
static void FixDivisionSigns(Int128* result, Int128* remainder,
                             bool dividend_was_negative, bool divisor_was_negative) {
  if (dividend_was_negative != divisor_was_negative) {
    result->Negate();
  }

  if (dividend_was_negative) {
    remainder->Negate();
  }
}

/// \brief Build a Int128 from a list of ints.
static Status BuildFromArray(Int128* value, uint32_t* array, int64_t length) {
  switch (length) {
    case 0:
      *value = {static_cast<int64_t>(0)};
      break;
    case 1:
      *value = {static_cast<int64_t>(array[0])};
      break;
    case 2:
      *value = {static_cast<int64_t>(0),
                (static_cast<uint64_t>(array[0]) << 32) + array[1]};
      break;
    case 3:
      *value = {static_cast<int64_t>(array[0]),
                (static_cast<uint64_t>(array[1]) << 32) + array[2]};
      break;
    case 4:
      *value = {(static_cast<int64_t>(array[0]) << 32) + array[1],
                (static_cast<uint64_t>(array[2]) << 32) + array[3]};
      break;
    case 5:
      if (array[0] != 0) {
        return Status::Invalid("Can't build Int128 with 5 ints.");
      }
      *value = {(static_cast<int64_t>(array[1]) << 32) + array[2],
                (static_cast<uint64_t>(array[3]) << 32) + array[4]};
      break;
    default:
      return Status::Invalid("Unsupported length for building Int128");
  }

  return Status::OK();
}

/// \brief Do a division where the divisor fits into a single 32 bit value.
static Status SingleDivide(const uint32_t* dividend, int64_t dividend_length,
                           uint32_t divisor, Int128* remainder,
                           bool dividend_was_negative, bool divisor_was_negative,
                           Int128* result) {
  uint64_t r = 0;
  uint32_t result_array[5];
  for (int64_t j = 0; j < dividend_length; j++) {
    r <<= 32;
    r += dividend[j];
    result_array[j] = static_cast<uint32_t>(r / divisor);
    r %= divisor;
  }
  RETURN_NOT_OK(BuildFromArray(result, result_array, dividend_length));
  *remainder = static_cast<int64_t>(r);
  FixDivisionSigns(result, remainder, dividend_was_negative, divisor_was_negative);
  return Status::OK();
}

Status Int128::Divide(const Int128& divisor, Int128* result, Int128* remainder) const {
  // Split the dividend and divisor into integer pieces so that we can
  // work on them.
  uint32_t dividend_array[5];
  uint32_t divisor_array[4];
  bool dividend_was_negative;
  bool divisor_was_negative;
  // leave an extra zero before the dividend
  dividend_array[0] = 0;
  int64_t dividend_length =
      FillInArray(*this, dividend_array + 1, dividend_was_negative) + 1;
  int64_t divisor_length = FillInArray(divisor, divisor_array, divisor_was_negative);

  // Handle some of the easy cases.
  if (dividend_length <= divisor_length) {
    *remainder = *this;
    *result = 0;
    return Status::OK();
  }

  if (divisor_length == 0) {
    return Status::Invalid("Division by 0 in Int128");
  }

  if (divisor_length == 1) {
    return SingleDivide(dividend_array, dividend_length, divisor_array[0], remainder,
                        dividend_was_negative, divisor_was_negative, result);
  }

  int64_t result_length = dividend_length - divisor_length;
  uint32_t result_array[4];

  // Normalize by shifting both by a multiple of 2 so that
  // the digit guessing is better. The requirement is that
  // divisor_array[0] is greater than 2**31.
  int64_t normalize_bits = 32 - FindLastSetBit(divisor_array[0]);
  ShiftArrayLeft(divisor_array, divisor_length, normalize_bits);
  ShiftArrayLeft(dividend_array, dividend_length, normalize_bits);

  // compute each digit in the result
  for (int64_t j = 0; j < result_length; ++j) {
    // Guess the next digit. At worst it is two too large
    uint32_t guess = std::numeric_limits<uint32_t>::max();
    auto high_dividend =
        static_cast<uint64_t>(dividend_array[j]) << 32 | dividend_array[j + 1];
    if (dividend_array[j] != divisor_array[0]) {
      guess = static_cast<uint32_t>(high_dividend / divisor_array[0]);
    }

    // catch all of the cases where guess is two too large and most of the
    // cases where it is one too large
    auto rhat = static_cast<uint32_t>(high_dividend -
                                      guess * static_cast<uint64_t>(divisor_array[0]));
    while (static_cast<uint64_t>(divisor_array[1]) * guess >
           (static_cast<uint64_t>(rhat) << 32) + dividend_array[j + 2]) {
      --guess;
      rhat += divisor_array[0];
      if (static_cast<uint64_t>(rhat) < divisor_array[0]) {
        break;
      }
    }

    // subtract off the guess * divisor from the dividend
    uint64_t mult = 0;
    for (int64_t i = divisor_length - 1; i >= 0; --i) {
      mult += static_cast<uint64_t>(guess) * divisor_array[i];
      uint32_t prev = dividend_array[j + i + 1];
      dividend_array[j + i + 1] -= static_cast<uint32_t>(mult);
      mult >>= 32;
      if (dividend_array[j + i + 1] > prev) {
        ++mult;
      }
    }
    uint32_t prev = dividend_array[j];
    dividend_array[j] -= static_cast<uint32_t>(mult);

    // if guess was too big, we add back divisor
    if (dividend_array[j] > prev) {
      --guess;

      uint32_t carry = 0;
      for (int64_t i = divisor_length - 1; i >= 0; --i) {
        uint64_t sum =
            static_cast<uint64_t>(divisor_array[i]) + dividend_array[j + i + 1] + carry;
        dividend_array[j + i + 1] = static_cast<uint32_t>(sum);
        carry = static_cast<uint32_t>(sum >> 32);
      }
      dividend_array[j] += carry;
    }

    result_array[j] = guess;
  }

  // denormalize the remainder
  ShiftArrayRight(dividend_array, dividend_length, normalize_bits);

  // return result and remainder
  RETURN_NOT_OK(BuildFromArray(result, result_array, result_length));
  RETURN_NOT_OK(BuildFromArray(remainder, dividend_array, dividend_length));
  FixDivisionSigns(result, remainder, dividend_was_negative, divisor_was_negative);
  return Status::OK();
}

bool operator==(const Int128& left, const Int128& right) {
  return left.high_bits() == right.high_bits() && left.low_bits() == right.low_bits();
}

bool operator!=(const Int128& left, const Int128& right) {
  return !operator==(left, right);
}

bool operator<(const Int128& left, const Int128& right) {
  return left.high_bits() < right.high_bits() ||
         (left.high_bits() == right.high_bits() && left.low_bits() < right.low_bits());
}

bool operator<=(const Int128& left, const Int128& right) {
  return !operator>(left, right);
}

bool operator>(const Int128& left, const Int128& right) { return operator<(right, left); }

bool operator>=(const Int128& left, const Int128& right) {
  return !operator<(left, right);
}

Int128 operator-(const Int128& operand) {
  Int128 result(operand.high_bits(), operand.low_bits());
  return result.Negate();
}

Int128 operator+(const Int128& left, const Int128& right) {
  Int128 result(left.high_bits(), left.low_bits());
  result += right;
  return result;
}

Int128 operator-(const Int128& left, const Int128& right) {
  Int128 result(left.high_bits(), left.low_bits());
  result -= right;
  return result;
}

Int128 operator*(const Int128& left, const Int128& right) {
  Int128 result(left.high_bits(), left.low_bits());
  result *= right;
  return result;
}

Int128 operator/(const Int128& left, const Int128& right) {
  Int128 remainder;
  Int128 result;
  DCHECK(left.Divide(right, &result, &remainder).ok());
  return result;
}

Int128 operator%(const Int128& left, const Int128& right) {
  Int128 remainder;
  Int128 result;
  DCHECK(left.Divide(right, &result, &remainder).ok());
  return remainder;
}

}  // namespace decimal
}  // namespace arrow
