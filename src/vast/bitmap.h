#ifndef VAST_BITMAP_H
#define VAST_BITMAP_H

#include <list>
#include <unordered_map>
#include "vast/bitstream.h"
#include "vast/exception.h"
#include "vast/operator.h"
#include "vast/optional.h"
#include "vast/serialization.h"
#include "vast/util/convert.h"
#include "vast/util/operators.h"
#include "vast/util/print.h"

namespace vast {
namespace detail {

struct storage_policy
{
  size_t rows = 0;

private:
  friend access;

  void serialize(serializer& sink) const
  {
    sink << rows;
  }

  void deserialize(deserializer& source)
  {
    source >> rows;
  }
};

/// A vector-based random access bitstream storage policy.
/// This storage policy maps values to indices. It provides *O(1)* access and
/// requires *O(max(T))* space. Hence it is only useful for very dense domains.
template <typename T, typename Bitstream>
struct vector_storage : storage_policy,
                        util::equality_comparable<vector_storage<T, Bitstream>>
{
  friend bool operator==(vector_storage const& x, vector_storage const& y)
  {
    if (x.cardinality_ != y.cardinality_)
      return false;
    // FIXME: std::vector<T>::operator== chokes on optional<T> and finds too
    // many overloads, so we'll do the comparison by hand.
    auto& xv = x.vector_;
    auto& yv = y.vector_;
    if (xv.size() != yv.size())
      return false;
    for (size_t i = 0; i < xv.size(); ++i)
      if ((xv[i] && ! yv[i]) || (! xv[i] && yv[i]))
        return false;
      else if (xv[i] && yv[i] && *xv[i] != *yv[i])
        return false;
    return true;
  }

  Bitstream const* find(T const& x) const
  {
    auto i = static_cast<size_t>(x);
    if (i >= vector_.size() || ! vector_[i])
      return nullptr;
    return &*vector_[i];
  }

  std::pair<Bitstream const*, Bitstream const*> find_bounds(T const& x) const
  {
    if (vector_.empty())
      return {nullptr, nullptr};
    else if (x >= vector_.size())
      return {&*vector_.back(), nullptr};

    Bitstream const* lower = nullptr;
    Bitstream const* upper = nullptr;
    bool found = false;
    for (size_t i = 0; ! found && i < vector_.size(); ++i)
      if (vector_[i] && i < x)
        lower = &*vector_[i];
      else if (lower && i >= x)
        found = true;

    found = false;
    for (size_t i = vector_.size(); ! found && i > 0; --i)
      if (vector_[i - 1] && i - 1 > x)
        upper = &*vector_[i - 1];
      else if (upper && i - 1 < x)
        found = true;

    return {lower, upper};
  }

  void each(std::function<void(T const&, Bitstream&)> f)
  {
    for (size_t i = 0; i < vector_.size(); ++i)
      f(i, *vector_[i]);
  }

  void each(std::function<void(T const&, Bitstream const&)> f) const
  {
    for (size_t i = 0; i < vector_.size(); ++i)
      f(i, *vector_[i]);
  }

  bool insert(T const& x, Bitstream b = {})
  {
    auto i = static_cast<size_t>(x);
    if (i >= vector_.size())
      vector_.resize(i + 1);
    else if (i < vector_.size() && vector_[i])
      return false;

    vector_[i] = std::move(b);
    ++cardinality_;
    return true;
  }

  uint64_t cardinality() const
  {
    return cardinality_;
  }

private:
  friend access;

  void serialize(serializer& sink) const
  {
    sink << static_cast<storage_policy const&>(*this);
    sink << cardinality_ << vector_;
  }

  void deserialize(deserializer& source)
  {
    source >> static_cast<storage_policy&>(*this);
    source >> cardinality_ >> vector_;
  }

  std::vector<optional<Bitstream>> vector_;
  uint64_t cardinality_ = 0;
};

/// A linked-list-plus-hash-table-based bitstream storage policy.
/// This storage policy offers *O(1)* lookup and *O(log(n))* bounds checks, at
/// the cost of *O(n * b + n)* space.
template <typename T, typename Bitstream>
struct list_storage : storage_policy,
                      util::equality_comparable<list_storage<T, Bitstream>>
{
  friend bool operator==(list_storage const& x, list_storage const& y)
  {
    return x.list_ == y.list_;
  }

  Bitstream const* find(T const& x) const
  {
    auto i = map_.find(x);
    return i == map_.end() ? nullptr : &i->second->second;
  }

  std::pair<Bitstream const*, Bitstream const*> find_bounds(T const& x) const
  {
    if (map_.empty())
      return {nullptr, nullptr};

    auto i = std::equal_range(
        list_.begin(), list_.end(), list_value_type(x, {}), key_comp_);

    // Note that std::lower_bound returns the first element that is *not* less
    // than x, but we want the element that *is* less than x, and nullptr
    // otherwise.
    auto upper = i.second != list_.end() ? &i.second->second : nullptr;
    auto lower = i.first != list_.begin() && (--i.first)->first < x
      ? &i.first->second
      : nullptr;

    return {lower, upper};
  }

  void each(std::function<void(T const&, Bitstream&)> f)
  {
    for (auto& p : list_)
      f(p.first, p.second);
  }

  void each(std::function<void(T const&, Bitstream const&)> f) const
  {
    for (auto& p : list_)
      f(p.first, p.second);
  }

  bool insert(T const& x, Bitstream b = {})
  {
    auto i = std::lower_bound(
        list_.begin(), list_.end(), list_value_type(x, {}), key_comp_);
    if (i == list_.end() || x < i->first)
    {
      i = list_.emplace(i, x, std::move(b));
      return map_.emplace(x, i).second;
    }
    return false;
  }

  uint64_t cardinality() const
  {
    return list_.size();
  }

private:
  using list_type = std::list<std::pair<T, Bitstream>>;
  using list_value_type = typename list_type::value_type;

  friend access;

  void serialize(serializer& sink) const
  {
    sink << static_cast<storage_policy const&>(*this);
    sink << list_;
  }

  void deserialize(deserializer& source)
  {
    source >> static_cast<storage_policy&>(*this);
    source >> list_;
    map_.reserve(list_.size());
    for (auto i = list_.begin(); i != list_.end(); ++i)
      map_.emplace(i->first, i);
  }

  struct key_comp
  {
    bool operator()(list_value_type const& x, list_value_type const& y)
    {
      return x.first < y.first;
    };
  };

  list_type list_;
  key_comp key_comp_;
  std::unordered_map<T, typename list_type::iterator> map_;
};

/// A purely hash-table-based bitstream storage policy.
/// This storage policy offers *O(1)* lookup and *O(n)* bounds check,
/// requiring *O(n * b)* space.
template <typename T, typename Bitstream>
struct unordered_storage
  : storage_policy,
    util::equality_comparable<unordered_storage<T, Bitstream>>
{
  friend bool operator==(unordered_storage const& x, unordered_storage const& y)
  {
    return x.map_ == y.map_;
  }

  Bitstream const* find(T const& x) const
  {
    auto i = map_.find(x);
    return i == map_.end() ? nullptr : &i->second;
  }

  std::pair<Bitstream const*, Bitstream const*> find_bounds(T const& x) const
  {
    if (map_.empty())
      return {nullptr, nullptr};

    auto l = map_.end();
    auto u = map_.end();
    for (auto p = map_.begin(); p != map_.end(); ++p)
    {
      if (p->first > x && (u == map_.end() || p->first < u->first))
        u = p;
      if (p->first < x && (l == map_.end() || p->first > u->first))
        l = p;
    }

    return {l == map_.end() ? nullptr : &l->second,
            u == map_.end() ? nullptr : &u->second};
  }

  void each(std::function<void(T const&, Bitstream&)> f)
  {
    for (auto& p : map_)
      f(p.first, p.second);
  }

  void each(std::function<void(T const&, Bitstream const&)> f) const
  {
    for (auto& p : map_)
      f(p.first, p.second);
  }

  bool insert(T const& x, Bitstream b = {})
  {
    auto p = map_.emplace(x, std::move(b));
    return p.second;
  }

  uint64_t cardinality() const
  {
    return map_.size();
  }

  void serialize(serializer& sink) const
  {
    sink << static_cast<storage_policy const&>(*this);
    sink << map_;
  }

  void deserialize(deserializer& source)
  {
    source >> static_cast<storage_policy&>(*this);
    source >> map_;
  }

  std::unordered_map<T, Bitstream> map_;
};

} // detail

template <typename T, typename Bitstream, typename Storage>
class coder : util::equality_comparable<coder<T, Bitstream, Storage>>
{
public:
  friend bool operator==(coder const& x, coder const& y)
  {
    return x.store_ == y.store_;
  }

  bool append(size_t n, bool bit)
  {
    auto success = true;
    store_.each(
        [&](T const&, Bitstream& bs)
        {
          if (! bs.append(n, bit))
            success = false;
        });
    if (success)
      store_.rows += n;
    return success;
  }

  Storage const& store() const
  {
    return store_;
  }

protected:
  Storage store_;

private:
  friend access;

  void serialize(serializer& sink) const
  {
    sink << store_;
  }

  void deserialize(deserializer& source)
  {
    source >> store_;
  }
};

/// An equality encoding policy for bitmaps.
template <typename T, typename Bitstream>
struct equality_coder
  : coder<T, Bitstream, detail::unordered_storage<T, Bitstream>>
{
  using super = coder<T, Bitstream, detail::unordered_storage<T, Bitstream>>;
  using super::store_;

  bool encode(T const& x)
  {
    if (! store_.find(x))
      if (! store_.insert(x, {store_.rows, 0}))
        return false;

    store_.each([&](T const& k, Bitstream& bs) { bs.push_back(x == k); });
    ++store_.rows;
    return true;
  }

  optional<Bitstream> decode(T const& x, relational_operator op = equal) const
  {
    auto result = store_.find(x);
    switch (op)
    {
      default:
        throw std::runtime_error(
            "unsupported relational operator: " + to<std::string>(op));
      case equal:
        if (result)
          return *result;
        else
          return {};
      case not_equal:
        if (result)
          return ~*result;
        else
          return {{store_.rows, true}};
    }
  }
};

/// A binary encoding policy for bitmaps.
/// This scheme is also known as *bit-sliced* encoding.
template <typename T, typename Bitstream>
struct binary_coder
  : coder<T, Bitstream, detail::vector_storage<uint8_t, Bitstream>>
{
  static_assert(std::is_integral<T>::value,
                "binary encoding requires an integral type");

  using super = coder<T, Bitstream, detail::vector_storage<uint8_t, Bitstream>>;
  using super::store_;

  static uint8_t constexpr bits = std::numeric_limits<T>::digits;

  binary_coder()
  {
    for (uint8_t i = 0; i < bits; ++i)
      store_.insert(i);
  }

  bool encode(T const& x)
  {
    store_.each([&](uint8_t i, Bitstream& bs) { bs.push_back((x >> i) & 1); });
    ++store_.rows;
    return true;
  }

  optional<Bitstream> decode(T const& x, relational_operator op = equal) const
  {
    switch (op)
    {
      default:
        throw std::runtime_error(
            "unsupported relational operator: " + to<std::string>(op));
      case equal:
        {
          Bitstream result{store_.rows, true};
          for (uint8_t i = 0; i < bits; ++i)
            result &= ((x >> i) & 1) ? *store_.find(i) : ~*store_.find(i);
          if (result.find_first() == Bitstream::npos)
            return {};
          else
            return std::move(result);
        }
      case not_equal:
        if (auto result = decode(x, equal))
          return std::move((*result).flip());
        else
          return {{store_.rows, true}};
    }
  }
};

/// A less-than-or-equal range encoding policy for bitmaps.
template <typename T, typename Bitstream>
struct range_coder : coder<T, Bitstream, detail::list_storage<T, Bitstream>>
{
  static_assert(! std::is_same<T, bool>::value,
                "range encoding for boolean value does not make sense");

  using super = coder<T, Bitstream, detail::list_storage<T, Bitstream>>;
  using super::store_;

  bool encode(T const& x)
  {
    if (! store_.find(x))
    {
      auto range = store_.find_bounds(x);
      auto success = true;
      if (range.first && range.second)
        success = store_.insert(x, {*range.first});
      else if (range.first)
        success = store_.insert(x, {store_.rows, true});
      else if (range.second)
        success = store_.insert(x, {store_.rows, false});
      else
        success = store_.insert(x, {store_.rows, true});
      if (! success)
        return false;
    }

    store_.each([&](T const& k, Bitstream& bs) { bs.push_back(x <= k); });
    ++store_.rows;
    return true;
  }

  optional<Bitstream>
  decode(T const& x, relational_operator op = less_equal) const
  {
    switch (op)
    {
      default:
        throw std::runtime_error(
            "unsupported relational operator: " + to<std::string>(op));
      case less:
        if (! std::is_integral<T>::value)
        throw std::runtime_error("operator less requires integral type");
        else if (x == std::numeric_limits<T>::lowest())
          return decode(x, less_equal);
        else
          return decode(x - 1, less_equal);
      case less_equal:
        if (auto result = store_.find(x))
          return *result;
        else if (auto lower = store_.find_bounds(x).first)
          return *lower;
        else
          return {};
      case greater:
        if (auto result = decode(x, less_equal))
          return std::move((*result).flip());
        else
          return {{store_.rows, true}};
      case greater_equal:
        if (auto result = decode(x, less))
          return std::move((*result).flip());
        else
          return {{store_.rows, true}};
      case equal:
        {
          // For a range-encoded bitstream v == x means z <= x & ~pred(z). If
          // pred(z) does not exist, the query reduces to z <= x.
          auto le = decode(x, less_equal);
          if (! le)
            return {};
          auto range = store_.find_bounds(x);
          if (! range.first)
            return le;
          *le &= ~*range.first;
          return le;
        }
      case not_equal:
        if (auto result = decode(x, equal))
          return std::move((*result).flip());
        else
          return {{store_.rows, true}};
    }
  }
};

/// A null binning policy acting as identity function.
template <typename T>
struct null_binner : util::equality_comparable<null_binner<T>>
{
  friend bool operator==(null_binner const&, null_binner const&)
  {
    return true;
  }

  T operator()(T x) const
  {
    return std::move(x);
  }

  // Nothing to do here.
  void serialize(serializer&) const { }
  void deserialize(deserializer&) { }
};

/// A binning policy that reduces value to a given precision.
template <typename T>
class precision_binner
{
  template <typename B>
  using is_bool = typename std::is_same<B, bool>::type;

  template <typename F>
  using is_double = typename std::is_same<F, double>::type;

  static_assert(std::is_arithmetic<T>::value && !is_bool<T>::value,
      "precision binning works only with number types");

  constexpr static int default_precision =
    Conditional<is_double<T>,
      std::integral_constant<int, -2>,
      std::integral_constant<int, 1>
    >::value;

public:
  /// Constructs a precision binner.
  ///
  /// @param precision The number of decimal digits. For example, a value of 3
  /// means that the values 1000 and 1300 end up in the same bin having a value
  /// of 1.
  ///
  /// For integral types, the sign of *precision* has no meaning, but for
  /// floating point types, the sign indiciates the precision of the fractional
  /// component. For example, a precision of -2 means that the values 42.03 and
  /// 42.04 end up in the same bin 42.00.
  ///
  /// @note Integral types are truncated and fractional types are rounded.
  precision_binner(int precision = default_precision)
  {
    integral_ = std::pow(10, precision < 0 ? -precision : precision);
    if (precision < 0)
      fractional_ = integral_;
  }

  T operator()(T x) const
  {
    return dispatch(x, is_double<T>());
  }

private:
  friend access;

  void serialize(serializer& sink) const
  {
    sink << integral_ << fractional_;
  }

  void deserialize(deserializer& source)
  {
    source >> integral_ >> fractional_;
  }

  T dispatch(T x, std::true_type) const
  {
    if (fractional_ != 0.0)
    {
      double i;
      auto f = std::modf(x, &i);
      return i + std::round(f * fractional_) / fractional_;
    }
    else if (integral_)
    {
      return std::round(x / integral_);
    }
    return x;
  }

  T dispatch(T x, std::false_type) const
  {
    return x / integral_;
  }

  friend bool operator==(precision_binner const& x, precision_binner const& y)
  {
    return x.integral_ == y.integral_ && x.fractional_ == y.fractional_;
  }

  T integral_;
  double fractional_ = 0.0;
};

/// A bitmap which maps values to [bitstreams](@ref bitstream).
template <
  typename T,
  typename Bitstream = ewah_bitstream,
  template <typename, typename> class Coder = equality_coder,
  template <typename> class Binner = null_binner
>
class bitmap : util::equality_comparable<bitmap<T, Bitstream, Coder, Binner>>,
               util::printable<bitmap<T, Bitstream, Coder, Binner>>
{
public:
  using coder_type = Coder<T, Bitstream>;
  using binner_type = Binner<T>;

  /// Constructs an empty bitmap.
  bitmap(binner_type binner = binner_type{}, coder_type coder = coder_type{})
    : coder_{coder}, binner_{binner}
  {
  }

  /// Adds a value to the bitmap. For example, in the case of equality
  /// encoding, this entails appending 1 to the single bitstream for the given
  /// value and 0 to all other bitstreams.
  ///
  /// @param x The value to add.
  ///
  /// @returns `true` on success and `false` if the bitmap is full, i.e., has
  /// `2^std::numeric_limits<size_t>::digits() - 1` elements.
  bool push_back(T const& x)
  {
    auto success = coder_.encode(binner_(x));
    return success && valid_.push_back(true);
  }

  /// Appends a given number of invalid rows/elements to the bitmaps.
  /// @param n The number of elements to append.
  /// @param bit The value of the bit to append.
  /// @returns `true` on success and `false` if the bitmap is full.
  bool append(size_t n = 1, bool bit = false)
  {
    return valid_.append(n, bit) && coder_.append(n, bit);
  }

  /// Shorthand for `lookup(equal, x)`.
  optional<Bitstream> operator[](T const& x) const
  {
    return lookup(equal, x);
  }

  /// Retrieves a bitstream of a given value with respect to a given operator.
  ///
  /// @param op The relational operator to use for looking up *x*.
  ///
  /// @param x The value to find the bitstream for.
  ///
  /// @returns An @link option vast::option@endlink containing a bitstream
  /// for all values *v* such that *op(v,x)* is `true`.
  optional<Bitstream> lookup(relational_operator op, T const& x) const
  {
    auto result = coder_.decode(binner_(x), op);
    if (result)
      *result &= valid_;
    return result;
  }

  /// Retrieves the raw bistream without decoding the result.
  /// @param x The raw value to lookup.
  /// @returns A pointer to the bitstream for *x* or `nullptr` if not found.
  template <typename U>
  Bitstream const* lookup_raw(U const& x) const
  {
    return coder_.store().find(x);
  }

  /// Retrieves the bitstream marking the valid results.
  /// @returns The bitstream holding the valid results.
  Bitstream const& valid() const
  {
    return valid_;
  }

  /// Retrieves the bitmap size.
  /// @returns The number of elements contained in the bitmap.
  size_t size() const
  {
    return coder_.store().rows;
  }

  /// Checks whether the bitmap is empty.
  /// @returns `true` *iff* the bitmap has 0 entries.
  bool empty() const
  {
    return size() == 0;
  }

  coder_type coder_;
  binner_type binner_;
  Bitstream valid_;

private:
  friend access;

  void serialize(serializer& sink) const
  {
    sink << binner_ << valid_ << coder_;
  }

  void deserialize(deserializer& source)
  {
    source >> binner_ >> valid_ >> coder_;
  }

  template <typename Iterator>
  bool print(Iterator& out, bool with_header = true, char delim = '\t') const
  {
    if (empty())
      return true;

    std::string str;
    if (with_header)
    {
      coder_.store().each(
          [&](T const& x, Bitstream const&)
          {
            str += to_string(x) + delim;
          });
      str.pop_back();
      str += '\n';
    }
    std::copy(str.begin(), str.end(), out);

    std::vector<Bitstream> cols;
    coder_.store().each(
        [&](T const&, Bitstream const& bs) { cols.push_back(bs); });

    return render(out, cols);
  }

  friend bool operator==(bitmap const& x, bitmap const& y)
  {
    return x.coder_ == y.coder_
        && x.binner_ == y.binner_
        && x.valid_ == y.valid_;
  }
};

/// A bitmap specialization for `bool`.
template <
  typename Bitstream,
  template <typename, typename> class Coder,
  template <typename> class Binner
>
class bitmap<bool, Bitstream, Coder, Binner>
{
public:
  using coder_type = Coder<bool, Bitstream>;
  using binner_type = Binner<bool>;

  bitmap() = default;

  bool push_back(bool x)
  {
    auto success = bool_.push_back(x);
    return valid_.push_back(true) && success;
  }

  bool append(size_t n = 1, bool bit = false)
  {
    return bool_.append(n, bit) && valid_.append(n, bit);
  }

  optional<Bitstream> operator[](bool x) const
  {
    return lookup(x);
  }

  optional<Bitstream> lookup(relational_operator op, bool x) const
  {
    switch (op)
    {
      default:
        throw std::runtime_error(
            "unsupported relational operator: " + to<std::string>(op));
      case not_equal:
        return {(x ? ~bool_ : bool_) & valid_};
      case equal:
        return {(x ? bool_ : ~bool_) & valid_};
    }
  }

  Bitstream const& valid() const
  {
    return valid_;
  }

  size_t size() const
  {
    return bool_.size();
  }

  bool empty() const
  {
    return bool_.empty();
  }

private:
  Bitstream bool_;
  Bitstream valid_;

private:
  friend access;

  void serialize(serializer& sink) const
  {
    sink << valid_ << bool_;
  }

  void deserialize(deserializer& source)
  {
    source >> valid_ >> bool_;
  }

  template <typename Iterator>
  bool print(Iterator& out) const
  {
    typename Bitstream::size_type last = 0;
    auto i = bool_.begin();
    auto end = bool_.end();

    while (i != end)
    {
      auto delta = *i - last;
      last = *i + 1;

      for (decltype(delta) zero = 0; zero < delta; ++zero)
      {
        *out++ = '0';
        *out++ = '\n';
      }

      *out++ = '1';
      if (++i != end)
        *out++ = '\n';
    }

    auto remaining_zeros = last < bool_.size() ? bool_.size() - last : 0;
    for (decltype(last) zero = 0; zero < remaining_zeros; ++zero)
    {
      *out++ = '0';
      if (zero != remaining_zeros)
        *out++ = '\n';
    }

    *out++ = '\n';

    return true;
  }

  friend bool operator==(bitmap const& x, bitmap const& y)
  {
    return x.bool_ == y.bool_ && x.valid_ == y.valid_;
  }
};

} // namespace vast

#endif
