// This file is part of Poseidon.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "taxon.hpp"
#include <new>
#include <climits>
#include <cmath>
#undef NDEBUG
#include <assert.h>

::std::size_t alloc_count;

void*
operator new(::std::size_t size)
  {
    void* ptr = ::std::malloc(size);
    if(!ptr)
      ::std::abort();

    ::alloc_count ++;
    return ptr;
  }

void
operator delete(void* ptr) noexcept
  {
    if(!ptr)
      return;

    ::alloc_count --;
    ::std::free(ptr);
  }

void
operator delete(void* ptr, ::std::size_t) noexcept
  {
    operator delete(ptr);
  }

void*
operator new[](::std::size_t size)
  {
    return operator new(size);
  }

void
operator delete[](void* ptr) noexcept
  {
    operator delete(ptr);
  }

void
operator delete[](void* ptr, ::std::size_t) noexcept
  {
    operator delete(ptr);
  }

int
main(void)
  {
    delete new int;
    ::alloc_count = 0;

    {
      ::taxon::Value val;
      assert(val.type() == ::taxon::t_null);
      assert(val.is_null());
    }

    {
      ::taxon::Value val = nullptr;
      assert(val.type() == ::taxon::t_null);
      assert(val.is_null());
    }

    {
      ::taxon::V_array arr = { 1, &"hello", false };
      ::taxon::Value val = arr;
      assert(val.type() == ::taxon::t_array);
      assert(val.is_array());
      assert(val.as_array().size() == 3);
      assert(val.as_array().at(0).type() == ::taxon::t_integer);
      assert(val.as_array().at(0).as_integer() == 1);
      assert(val.as_array().at(1).type() == ::taxon::t_string);
      assert(val.as_array().at(1).as_string() == "hello");
      assert(val.as_array().at(2).type() == ::taxon::t_boolean);
      assert(val.as_array().at(2).as_boolean() == false);
    }

    {
      ::taxon::V_object obj = { { &"x", 1 }, { &"y", &"hello" } };
      ::taxon::Value val = obj;
      assert(val.type() == ::taxon::t_object);
      assert(val.is_object());
      assert(val.as_object().size() == 2);
      assert(val.as_object().at(&"x").type() == ::taxon::t_integer);
      assert(val.as_object().at(&"x").as_integer() == 1);
      assert(val.as_object().at(&"y").type() == ::taxon::t_string);
      assert(val.as_object().at(&"y").as_string() == "hello");
    }

    {
      ::taxon::Value val = true;
      assert(val.type() == ::taxon::t_boolean);
      assert(val.is_boolean());
      assert(val.as_boolean() == true);

      val.mut_integer() = 42;
      assert(val.type() == ::taxon::t_integer);
      assert(val.is_integer());
      assert(val.as_integer() == 42);
    }

    {
      ::taxon::Value val = 0;
      assert(val.type() == ::taxon::t_integer);
      assert(val.is_integer());
      assert(val.as_integer() == 0);

      val.mut_number() = -5;
      assert(val.is_number());
      assert(val.as_number() == -5);
    }

    {
      ::taxon::Value val = INT64_MAX;
      assert(val.type() == ::taxon::t_integer);
      assert(val.is_integer());
      assert(val.as_integer() == INT64_MAX);
    }

    {
      ::taxon::Value val = INT64_MIN;
      assert(val.type() == ::taxon::t_integer);
      assert(val.is_integer());
      assert(val.as_integer() == INT64_MIN);
    }

    {
      ::taxon::Value val = 1.5;
      assert(val.type() == ::taxon::t_number);
      assert(val.is_number());
      assert(val.as_number() == 1.5);

      val = &"world";
      assert(val.type() == ::taxon::t_string);
      assert(val.is_string());
      assert(val.as_string() == "world");
      assert(val.as_string_length() == 5);
      assert(::memcmp(val.as_string_c_str(), "world", 6) == 0);
    }

    {
      ::taxon::Value val = ::std::numeric_limits<double>::quiet_NaN();
      assert(val.type() == ::taxon::t_number);
      assert(val.is_number());
      assert(::std::isnan(val.as_number()));
    }

    {
      ::taxon::Value val = 42;
      assert(val.type() == ::taxon::t_integer);
      assert(val.is_number());
      assert(val.as_number() == 42.0);
      assert(val.type() == ::taxon::t_integer);
    }

    {
      ::taxon::Value val = &"hello";
      assert(val.type() == ::taxon::t_string);
      assert(val.is_string());
      assert(val.as_string().size() == 5);
      assert(val.as_string() == "hello");
      assert(val.as_string_length() == 5);
      assert(::memcmp(val.as_string_c_str(), "hello", 6) == 0);

      static constexpr unsigned char bytes[] = "\xFF\x00\xFE\x7F\x80";
      val = ::rocket::cow_bstring(bytes, 5);
      assert(val.is_binary());
      assert(val.as_binary().size() == 5);
      assert(val.as_binary().compare(bytes, 5) == 0);
      assert(val.as_binary_size() == 5);
      assert(::memcmp(val.as_binary_data(), bytes, 5) == 0);
    }

    {
      static constexpr unsigned char bytes[] = "\x01\x00\x03";
      ::taxon::Value val = ::rocket::cow_bstring(bytes, 3);
      assert(val.type() == ::taxon::t_binary);
      assert(val.is_binary());
      assert(val.as_binary().size() == 3);
      assert(val.as_binary().compare(bytes, 3) == 0);
      assert(val.as_binary_size() == 3);
      assert(::memcmp(val.as_binary_data(), bytes, 3) == 0);

      static constexpr ::std::chrono::milliseconds dur(123456789);
      static constexpr ::std::chrono::system_clock::time_point tp(dur);
      val = tp;
      assert(val.type() == ::taxon::t_time);
      assert(val.is_time());
      assert(val.as_time() == tp);
    }

    {
      static constexpr ::std::chrono::milliseconds dur(123456789);
      static constexpr ::std::chrono::system_clock::time_point tp(dur);
      ::taxon::Value val = tp;
      assert(val.type() == ::taxon::t_time);
      assert(val.is_time());
      assert(val.as_time() == tp);

      val = false;
      assert(val.type() == ::taxon::t_boolean);
      assert(val.is_boolean());
      assert(val.as_boolean() == false);
    }

    {
      // `["meow",{"x":true},12.5,[37,null]]`
      ::taxon::Value val;
      val.mut_array().resize(4);
      val.mut_array().mut(0) = &"meow";
      val.mut_array().mut(1).mut_object().try_emplace(&"x", true);
      val.mut_array().mut(2) = 12.5;
      val.mut_array().mut(3).mut_array().resize(2);
      val.mut_array().mut(3).mut_array().mut(0) = 37;
      val.mut_array().mut(3).mut_array().mut(1) = nullptr;

      // TODO
    }

    // leak check
    assert(::alloc_count == 0);
  }
