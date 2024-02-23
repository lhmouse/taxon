// This file is part of Poseidon.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#ifndef TAXON_TAXON_HPP_
#define TAXON_TAXON_HPP_

#include <rocket/cow_string.hpp>
#include <rocket/cow_vector.hpp>
#include <rocket/cow_hashmap.hpp>
#include <rocket/prehashed_string.hpp>
#include <rocket/variant.hpp>
#include <rocket/tinyfmt.hpp>
#include <chrono>
namespace taxon {

struct Parser_Context;
class Value;

// Define aliases and enumerators for data types.
using V_null    = ::std::nullptr_t;
using V_array   = ::rocket::cow_vector<Value>;
using V_object  = ::rocket::cow_hashmap<::rocket::prehashed_string,
                       Value, ::rocket::prehashed_string::hash>;

using V_boolean = bool;
using V_integer = ::std::int64_t;
using V_number  = double;
using V_string  = ::rocket::cow_string;
using V_binary  = ::rocket::cow_bstring;
using V_time    = ::std::chrono::system_clock::time_point;

// Expand a sequence of alternatives without a trailing comma. This macro is part of
// the ABI.
#define TAXON_GENERATOR_IEZUVAH3_(U)  \
    /*  0 */  U##_null  \
    /*  1 */, U##_array  \
    /*  2 */, U##_object  \
    /*  3 */, U##_boolean  \
    /*  4 */, U##_integer  \
    /*  5 */, U##_number  \
    /*  6 */, U##_string  \
    /*  7 */, U##_binary  \
    /*  8 */, U##_time

// Define type enumerators such as `t_null`, `t_array`, `t_number`, and so on.
enum Type : ::std::uint8_t {TAXON_GENERATOR_IEZUVAH3_(t)};
using Variant = ::rocket::variant<TAXON_GENERATOR_IEZUVAH3_(V)>;

// This structure provides storage for all parser states. Some of these fields are
// for internal use. This structure need not be initialized before `parse()`.
struct Parser_Context
  {
    // stream offset of the next operation
    ::std::int64_t offset;

    // if no error, a null pointer; otherwise, a static string about the error
    const char* error;

    // internal fields
    unsigned char has_nextc : 1;
    unsigned char reserved_t0 : 7;
    unsigned char nextc;
    unsigned char reserved_t2;
    unsigned char reserved_t3;
  };

// This is the only and comprehensive class that is provided by this library. It is
// responsible for storing, parsing and formatting all the alternatives above.
class Value
  {
  private:
    Variant m_stor;

  public:
    // Initializes a null value.
    constexpr Value(V_null = nullptr) noexcept { }

    // Destroys this value. The destructor shall take care of a deep recursion, to
    // avoid running out of the system stack.
    ~Value();

    // Gets the type of the stored value.
    constexpr
    Type
    type() const noexcept
      { return static_cast<Type>(this->m_stor.index());  }

    // Swaps two values in a smart way.
    Value&
    swap(Value& other) noexcept
      {
        this->m_stor.swap(other.m_stor);
        return *this;
      }

    // Checks whether the stored value is null.
    bool
    is_null() const noexcept
      { return this->m_stor.index() == t_null;  }

    // Sets a null value.
    void
    clear() noexcept
      { this->m_stor.emplace<V_null>();  }

    // Sets a null value.
    Value&
    operator=(V_null) & noexcept
      {
        this->clear();
        return *this;
      }

    // Initializes an array.
    Value(const V_array& val) noexcept
      {
        this->m_stor.emplace<V_array>(val);
      }

    // Checks whether the stored value is an array.
    bool
    is_array() const noexcept
      { return this->m_stor.index() == t_array;  }

    // Gets an array. If the stored value is not an array, an exception is thrown,
    // and there is no effect.
    const V_array&
    as_array() const
      { return this->m_stor.as<V_array>();  }

    // Get a read-only range of an array.
    V_array::const_iterator
    as_array_begin() const
      { return this->as_array().begin();  }

    V_array::const_iterator
    as_array_end() const
      { return this->as_array().end();  }

    size_t
    as_array_size() const
      { return this->as_array().size();  }

    // Gets or creates an array (list). If the stored value is not an array, it is
    // overwritten with an empty array, and a reference to the new value is
    // returned.
    V_array&
    mut_array() noexcept
      {
        if(auto ptr = this->m_stor.mut_ptr<V_array>())
          return *ptr;
        else
          return this->m_stor.emplace<V_array>();
      }

    // Get a mutable range of an array.
    V_array::iterator
    mut_array_begin() noexcept
      { return this->mut_array().mut_begin();  }

    V_array::iterator
    mut_array_end() noexcept
      { return this->mut_array().mut_end();  }

    size_t
    mut_array_size() noexcept
      { return this->mut_array().size();  }

    // Sets an array.
    Value&
    operator=(const V_array& val) & noexcept
      {
        this->mut_array() = val;
        return *this;
      }

    // Initializes an object.
    Value(const V_object& val) noexcept
      {
        this->m_stor.emplace<V_object>(val);
      }

    // Checks whether the stored value is an object.
    bool
    is_object() const noexcept
      { return this->m_stor.index() == t_object;  }

    // Gets an object. If the stored value is not an object, an exception is thrown,
    // and there is no effect.
    const V_object&
    as_object() const
      { return this->m_stor.as<V_object>();  }

    // Get a read-only range of an object.
    V_object::const_iterator
    as_object_begin() const
      { return this->as_object().begin();  }

    V_object::const_iterator
    as_object_end() const
      { return this->as_object().end();  }

    size_t
    as_object_size() const
      { return this->as_object().size();  }

    // Gets or creates an object (dictionary). If the stored value is not an object,
    // it is overwritten with an empty object, and a reference to the new value is
    // returned.
    V_object&
    mut_object() noexcept
      {
        if(auto ptr = this->m_stor.mut_ptr<V_object>())
          return *ptr;
        else
          return this->m_stor.emplace<V_object>();
      }

    // Get a mutable range of an object.
    V_object::iterator
    mut_object_begin() noexcept
      { return this->mut_object().mut_begin();  }

    V_object::iterator
    mut_object_end() noexcept
      { return this->mut_object().mut_end();  }

    size_t
    mut_object_size() noexcept
      { return this->mut_object().size();  }

    // Sets an object.
    Value&
    operator=(const V_object& val) & noexcept
      {
        this->mut_object() = val;
        return *this;
      }

    // Initializes a boolean value.
    Value(bool val) noexcept
      {
        this->m_stor.emplace<V_boolean>(val);
      }

    // Checks whether the stored value is a boolean value.
    bool
    is_boolean() const noexcept
      { return this->m_stor.index() == t_boolean;  }

    // Gets a boolean value. If the stored value is not a boolean value, an exception
    // is thrown, and there is no effect.
    V_boolean
    as_boolean() const
      { return this->m_stor.as<V_boolean>();  }

    // Gets or creates a boolean value. If the stored value is not a boolean
    // value, it is overwritten with `false`, and a reference to the new value is
    // returned.
    V_boolean&
    mut_boolean() noexcept
      {
        if(auto ptr = this->m_stor.mut_ptr<V_boolean>())
          return *ptr;
        else
          return this->m_stor.emplace<V_boolean>();
      }

    // Sets a boolean value.
    Value&
    operator=(bool val) & noexcept
      {
        this->mut_boolean() = val;
        return *this;
      }

    // Initializes an integer. Only conversions from signed types are provided. We
    // don't use `int64_t` here due to some nasty overloading rules.
    Value(int val) noexcept
      {
        this->m_stor.emplace<V_integer>(val);
      }

    Value(long val) noexcept
      {
        this->m_stor.emplace<V_integer>(val);
      }

    Value(long long val) noexcept
      {
        this->m_stor.emplace<V_integer>(val);
      }

    // Checks whether the stored value is an integer.
    bool
    is_integer() const noexcept
      { return this->m_stor.index() == t_integer;  }

    // Gets an integer. If the stored value is not an integer, an exception is
    // thrown, and there is no effect.
    V_integer
    as_integer() const
      { return this->m_stor.as<V_integer>();  }

    // Gets or creates an integer. If the stored value is not an integer value, it
    // is overwritten with zero, and a reference to the new value is returned.
    V_integer&
    mut_integer() noexcept
      {
        if(auto ptr = this->m_stor.mut_ptr<V_integer>())
          return *ptr;
        else
          return this->m_stor.emplace<V_integer>();
      }

    // Sets an integer. Only conversions from signed types are provided. We don't
    // use `int64_t` here due to some nasty overloading rules.
    Value&
    operator=(int val) & noexcept
      {
        this->mut_integer() = val;
        return *this;
      }

    Value&
    operator=(long val) & noexcept
      {
        this->mut_integer() = val;
        return *this;
      }

    Value&
    operator=(long long val) & noexcept
      {
        this->mut_integer() = val;
        return *this;
      }

    // Initialize a floating-point number.
    Value(float val) noexcept
      {
        this->m_stor.emplace<V_number>(val);
      }

    Value(double val) noexcept
      {
        this->m_stor.emplace<V_number>(val);
      }

    // Checks whether the stored value is an integer or a floating-point number.
    bool
    is_number() const noexcept
      { return (this->m_stor.index() == t_integer) || (this->m_stor.index() == t_number);  }

    // Gets a floating-point number. If the stored value is an integer, it can be
    // converted to a floating-point number implicitly, despite potential precision
    // loss. If the stored value is neither an integer nor a floating-point number,
    // an exception is thrown, and there is no effect.
    V_number
    as_number() const
      {
        if(auto psi = this->m_stor.ptr<V_integer>())
          return static_cast<V_number>(*psi);
        else
          return this->m_stor.as<V_number>();
      }

    // Gets or creates a floating-point number. If the stored value is an integer,
    // it can be converted to a floating-point number implicitly, despite potential
    // precision loss. If the stored value is neither an integer nor a floating-point
    // number, it is overwritten with zero, and a reference to the new value is
    // returned.
    V_number&
    mut_number() noexcept
      {
        if(auto ptr = this->m_stor.mut_ptr<V_number>())
          return *ptr;
        else if(auto psi = this->m_stor.ptr<V_integer>())
          return this->m_stor.emplace<V_number>(static_cast<V_number>(*psi));
        else
          return this->m_stor.emplace<V_number>();
      }

    // Sets a floating-point number.
    Value&
    operator=(float val) & noexcept
      {
        this->mut_number() = val;
        return *this;
      }

    Value&
    operator=(double val) & noexcept
      {
        this->mut_number() = val;
        return *this;
      }

    // Initialize a character string. The caller shall supply a valid UTF-8 string,
    // but actual validation is deferred to the `print()` function.
    Value(const ::rocket::cow_string& val) noexcept
      {
        this->m_stor.emplace<V_string>(val);
      }

    Value(::rocket::shallow_string val) noexcept
      {
        this->m_stor.emplace<V_string>(val);
      }

    template<size_t N>
    Value(const char (*val)[N]) noexcept
      {
        this->m_stor.emplace<V_string>(val);
      }

    // Checks whether the stored value is a character string.
    bool
    is_string() const noexcept
      { return this->m_stor.index() == t_string;  }

    // Gets a character string. If the stored value is not a character string, an
    // exception is thrown, and there is no effect.
    const V_string&
    as_string() const
      { return this->m_stor.as<V_string>();  }

    // Get a read-only range of a character string.
    const char*
    as_string_c_str() const
      { return this->as_string().c_str();  }

    size_t
    as_string_length() const
      { return this->as_string().length();  }

    // Gets or creates a character string. If the stored value is not a character
    // string, it is overwritten with an empty string, and a reference to the new
    // value is returned.
    V_string&
    mut_string() noexcept
      {
        if(auto ptr = this->m_stor.mut_ptr<V_string>())
          return *ptr;
        else
          return this->m_stor.emplace<V_string>();
      }

    // Get a mutable range of a character string.
    char*
    mut_string_c_str() noexcept
      { return this->mut_string().mut_data();  }

    size_t
    mut_string_length() noexcept
      { return this->mut_string().length();  }

    // Set a character string.
    Value&
    operator=(const ::rocket::cow_string& val) & noexcept
      {
        this->mut_string() = val;
        return *this;
      }

    Value&
    operator=(::rocket::shallow_string val) & noexcept
      {
        this->mut_string() = val;
        return *this;
      }

    template<size_t N>
    Value&
    operator=(const char (*val)[N]) & noexcept
      {
        this->mut_string() = val;
        return *this;
      }

    // Initialize a byte string.
    Value(const ::rocket::cow_bstring& val) noexcept
      {
        this->m_stor.emplace<V_binary>(val);
      }

    // Checks whether the stored value is a byte string.
    bool
    is_binary() const noexcept
      { return this->m_stor.index() == t_binary;  }

    // Gets a byte string. If the stored value is not a byte string, an exception
    // is thrown, and there is no effect.
    const V_binary&
    as_binary() const
      { return this->m_stor.as<V_binary>();  }

    // Get a read-only range of a byte string.
    const unsigned char*
    as_binary_data() const
      { return this->as_binary().data();  }

    size_t
    as_binary_size() const
      { return this->as_binary().size();  }

    // Gets or creates a byte string. If the stored value is not a byte string, it
    // is overwritten with an empty string, and a reference to the new value is
    // returned.
    V_binary&
    mut_binary() noexcept
      {
        if(auto ptr = this->m_stor.mut_ptr<V_binary>())
          return *ptr;
        else
          return this->m_stor.emplace<V_binary>();
      }

    // Get a mutable range of a byte string.
    unsigned char*
    mut_binary_data() noexcept
      { return this->mut_binary().mut_data();  }

    size_t
    mut_binary_size() noexcept
      { return this->mut_binary().length();  }

    // Set a byte string.
    Value&
    operator=(const ::rocket::cow_bstring& val) & noexcept
      {
        this->mut_binary() = val;
        return *this;
      }

    // Initializes a timestamp.
    Value(::std::chrono::system_clock::time_point val) noexcept
      {
        this->m_stor.emplace<V_time>(val);
      }

    // Checks whether the stored value is a timestamp.
    bool
    is_time() const noexcept
      { return this->m_stor.index() == t_time;  }

    // Gets a timestamp. If the stored value is not a timestamp, an exception is
    // thrown, and there is no effect.
    V_time
    as_time() const
      { return this->m_stor.as<V_time>();  }

    // Gets or creates a byte string. If the stored value is not a byte string, it
    // is overwritten with a zero timestamp denoting `1970-01-01T00:00:00Z`, and a
    // reference to the new value is returned.
    V_time&
    mut_time() noexcept
      {
        if(auto ptr = this->m_stor.mut_ptr<V_time>())
          return *ptr;
        else
          return this->m_stor.emplace<V_time>();
      }

    // Sets a timestamp.
    Value&
    operator=(::std::chrono::system_clock::time_point val) & noexcept
      {
        this->mut_time() = val;
        return *this;
      }

    // Parse a buffer for a value, and store it into the current object. Errors are
    // stored into the `Parser_Context`. The context object does not have to be
    // initialized. If this function throws an exception, the value of the current
    // object is indeterminate.
    void
    parse_with(Parser_Context& ctx, ::rocket::tinybuf& buf);

    void
    parse_with(Parser_Context& ctx, const ::rocket::cow_string& str);

    void
    parse_with(Parser_Context& ctx, ::std::FILE* fp);

    bool
    parse(::rocket::tinybuf& buf);

    bool
    parse(const ::rocket::cow_string& str);

    bool
    parse(::std::FILE* fp);

    // Print this value. Invalid values are sanitized so they may become garbage or
    // null, but the entire output will always be valid TAXON. This function should
    // not throw exceptions on invalid inputs; only in case of an I/O error or
    // failure to allocate memory. A byte string of length 1, 2, 4, 8, 16, 20 or 32
    // is encoded in hex and a byte string of any other length is encoded in base64.
    void
    print_to(::rocket::tinybuf& buf) const;

    void
    print_to(::rocket::cow_string& str) const;

    void
    print_to(::std::FILE* fp) const;

    ::rocket::cow_string
    print_to_string() const;

    void
    print_to_stderr() const;
  };

inline
void
swap(Value& lhs, Value& rhs) noexcept
  {
    lhs.swap(rhs);
  }

inline
::rocket::tinyfmt&
operator<<(::rocket::tinyfmt& fmt, const Value& value)
  {
    value.print_to(fmt.mut_buf());
    return fmt;
  }

// Values are reference-counting so all these will not throw exceptions. It is
// recommended that they be passed by value or by const reference.
static_assert(::std::is_nothrow_copy_constructible<Value>::value, "");
static_assert(::std::is_nothrow_copy_assignable<Value>::value, "");
static_assert(::std::is_nothrow_move_constructible<Value>::value, "");
static_assert(::std::is_nothrow_move_assignable<Value>::value, "");

}  // namespace taxon

extern template
class ::rocket::variant<TAXON_GENERATOR_IEZUVAH3_(::taxon::V)>;

extern template
class ::rocket::cow_vector<::taxon::Value>;

extern template
class ::rocket::cow_hashmap<::rocket::prehashed_string,
  ::taxon::Value, ::rocket::prehashed_string::hash>;
#endif
