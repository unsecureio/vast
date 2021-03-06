/******************************************************************************
 *                    _   _____   __________                                  *
 *                   | | / / _ | / __/_  __/     Visibility                   *
 *                   | |/ / __ |_\ \  / /          Across                     *
 *                   |___/_/ |_/___/ /_/       Space and Time                 *
 *                                                                            *
 * This file is part of VAST. It is subject to the license terms in the       *
 * LICENSE file found in the top-level directory of this distribution and at  *
 * http://vast.io/license. No part of VAST, including this file, may be       *
 * copied, modified, propagated, or distributed except according to the terms *
 * contained in the LICENSE file.                                             *
 ******************************************************************************/

#pragma once

#include <array>
#include <string>

#include "vast/detail/operators.hpp"

namespace vast {

struct access;
class json;

/// An IP address.
class address : detail::totally_ordered<address>, detail::bitwise<address> {
  friend access;

  /// Top 96 bits of v4-mapped-addr.
  static std::array<uint8_t, 12> const v4_mapped_prefix;

public:
  /// Array for storing 128-bit IPv6 addresses.
  using array_type = std::array<uint8_t, 16>;

  /// Address family.
  enum family { ipv4, ipv6 };

  /// Address byte order.
  enum byte_order { host, network };

  /// Constructs an IPv4 address from raw bytes.
  /// @param bytes A pointer to 4 bytes.
  /// @param order The byte order of *bytes*.
  /// @returns An IPv4 address constructed from *bytes*.
  static address v4(const void* bytes, byte_order order = host);

  /// Constructs an IPv6 address from raw bytes.
  /// @param bytes A pointer to 16 bytes.
  /// @param order The byte order of *bytes*.
  /// @returns An IPv6 address constructed from *bytes*.
  static address v6(const void* bytes, byte_order order = host);

  /// Default-constructs an (invalid) address.
  address();

  /// Constructs an address from raw bytes.
  /// @param bytes A pointer to the raw byte representation. This must point
  ///              to 4 bytes if *fam* is `ipv4`, and to 16 bytes if *fam* is
  ///              `ipv6`.
  /// @param fam The address family.
  /// @param order The byte order in which the address pointed to by *bytes*
  ///              is stored in.
  address(const void* bytes, family fam, byte_order order);

  /// Determines whether the address is IPv4.
  /// @returns @c true iff the address is an IPv4 address.
  bool is_v4() const;

  /// Determines whether the address is IPv4.
  /// @returns `true` iff the address is an IPv4 address.
  bool is_v6() const;

  /// Determines whether the address is an IPv4 loopback address.
  /// @returns `true` if the address is v4 and its first byte has the
  /// value 127.
  bool is_loopback() const;

  /// Determines whether the address is an IPv4 broadcast address.
  /// @returns `true` if the address is v4 and has the value 255.255.255.255.
  bool is_broadcast() const;

  /// Determines whether the address is a multicast address. For v4
  /// addresses, this means the first byte equals to 224. For v6 addresses,
  /// this means the first bytes equals 255.
  /// @returns `true` if the address is a multicast address.
  bool is_multicast() const;

  /// Masks out lower bits of the address.
  /// @param top_bits_to_keep The number of bits *not* to mask out,
  ///                         counting from the highest order bit. The value is
  ///                         always interpreted relative to the IPv6 bit
  ///                         width, even if the address is IPv4. That means if
  ///                         we compute 192.168.1.2/16, we need to pass in
  ///                         112 (i.e., 96 + 16). The value must be in the
  ///                         range from 0 to 128.
  /// @returns `true` on success.
  bool mask(unsigned top_bits_to_keep);

  /// AND's another address to this instance.
  /// @param other The other address.
  /// @returns A reference to `*this`.
  address& operator&=(const address& other);

  /// OR's another address to this instance.
  /// @param other The other address.
  /// @returns A reference to `*this`.
  address& operator|=(const address& other);

  /// XOR's another address to this instance.
  /// @param other The other address.
  /// @returns A reference to `*this`.
  address& operator^=(const address& other);

  /// Retrieves the underlying byte array.
  /// @returns A reference to an array of 16 bytes.
  const std::array<uint8_t, 16>& data() const;

  /// Compares the top-k bits of this address with another one.
  /// @param other The other address.
  /// @param k The number of bits to compare, starting from the top.
  /// @returns `true` if the first *k* bits of both addresses are equal
  /// @pre `k > 0 && k <= 128`
  bool compare(const address& other, size_t k) const;

  friend bool operator==(const address& x, const address& y);
  friend bool operator<(const address& x, const address& y);

  template <class Inspector>
  friend auto inspect(Inspector& f, address& a) {
    return f(a.bytes_);
  }

  friend bool convert(const address& a, json& j);

private:
  std::array<uint8_t, 16> bytes_;
};

/// @relates address
template <class Hasher>
void hash_append(Hasher& h, const address& x) {
  auto bytes = x.data().data();
  if (x.is_v4())
    h(bytes + 12, 4);
  else
    h(bytes, 16);
}

} // namespace vast
