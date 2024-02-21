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
    ::setlocale(LC_ALL, "C.UTF-8");
    delete new int;
    ::alloc_count = 0;

    {
      ::taxon::Value val;
      assert(val.type() == ::taxon::t_null);
      assert(val.is_null());
      assert(val.print_to_string() == "null");
    }

    {
      ::taxon::Value val = nullptr;
      assert(val.type() == ::taxon::t_null);
      assert(val.is_null());
      assert(val.print_to_string() == "null");
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
      assert(val.print_to_string() == R"(["$l:1","hello",false])");
    }

    {
      ::taxon::V_object obj = { { &"x", 3.5 }, { &"y", &"hello" } };
      ::taxon::Value val = obj;
      assert(val.type() == ::taxon::t_object);
      assert(val.is_object());
      assert(val.as_object().size() == 2);
      assert(val.as_object().at(&"x").type() == ::taxon::t_number);
      assert(val.as_object().at(&"x").as_number() == 3.5);
      assert(val.as_object().at(&"y").type() == ::taxon::t_string);
      assert(val.as_object().at(&"y").as_string() == "hello");
      assert(val.print_to_string() == R"({"x":3.5,"y":"hello"})"
             || val.print_to_string() == R"({"y":"hello","x":3.5})");
    }

    {
      ::taxon::Value val = true;
      assert(val.type() == ::taxon::t_boolean);
      assert(val.is_boolean());
      assert(val.as_boolean() == true);
      assert(val.print_to_string() == "true");

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
      assert(val.print_to_string() == R"("$l:0")");

      val.mut_number() = -5;
      assert(val.is_number());
      assert(val.as_number() == -5);
    }

    {
      ::taxon::Value val = INT64_MAX;
      assert(val.type() == ::taxon::t_integer);
      assert(val.is_integer());
      assert(val.as_integer() == INT64_MAX);
      assert(val.print_to_string() == R"("$l:9223372036854775807")");
    }

    {
      ::taxon::Value val = INT64_MIN;
      assert(val.type() == ::taxon::t_integer);
      assert(val.is_integer());
      assert(val.as_integer() == INT64_MIN);
      assert(val.print_to_string() == R"("$l:-9223372036854775808")");
    }

    {
      ::taxon::Value val = 1.5;
      assert(val.type() == ::taxon::t_number);
      assert(val.is_number());
      assert(val.as_number() == 1.5);
      assert(val.print_to_string() == "1.5");

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
      assert(val.print_to_string() == R"("$d:nan")");
    }

    {
      ::taxon::Value val = 42;
      assert(val.type() == ::taxon::t_integer);
      assert(val.is_number());
      assert(val.as_number() == 42.0);
      assert(val.type() == ::taxon::t_integer);
      assert(val.print_to_string() == R"("$l:42")");
    }

    {
      ::taxon::Value val = &"\b\f\n\r\t\x1B\x7F";
      assert(val.type() == ::taxon::t_string);
      assert(val.is_string());
      assert(val.as_string().size() == 7);
      assert(val.as_string() == "\b\f\n\r\t\x1B\x7F");
      assert(val.as_string_length() == 7);
      assert(::memcmp(val.as_string_c_str(), "\b\f\n\r\t\x1B\x7F", 8) == 0);
      assert(val.print_to_string() == R"("\b\f\n\r\t\u001B\u007F")");

      static constexpr unsigned char bytes[] = "\xFF\x00\xFE\x7F\x80";
      val = ::rocket::cow_bstring(bytes, 5);
      assert(val.is_binary());
      assert(val.as_binary().size() == 5);
      assert(val.as_binary().compare(bytes, 5) == 0);
      assert(val.as_binary_size() == 5);
      assert(::memcmp(val.as_binary_data(), bytes, 5) == 0);
      assert(val.print_to_string() == R"("$b:/wD+f4A=")");
    }

    {
      static constexpr unsigned char bytes[] = "\x01\x00\x03\xC0";
      ::taxon::Value val = ::rocket::cow_bstring(bytes, 4);
      assert(val.type() == ::taxon::t_binary);
      assert(val.is_binary());
      assert(val.as_binary().size() == 4);
      assert(val.as_binary().compare(bytes, 4) == 0);
      assert(val.as_binary_size() == 4);
      assert(::memcmp(val.as_binary_data(), bytes, 4) == 0);
      assert(val.print_to_string() == R"("$h:010003C0")");

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
      assert(val.print_to_string() == R"("$t:123456789")");

      val = false;
      assert(val.type() == ::taxon::t_boolean);
      assert(val.is_boolean());
      assert(val.as_boolean() == false);
    }

    {
      // `["$s:meow",{"x":true},12.5,[37,null]]`
      ::taxon::Value val;
      val.mut_array().resize(4);
      val.mut_array().mut(0) = &"$meow";
      val.mut_array().mut(1).mut_object().try_emplace(&"x", true);
      val.mut_array().mut(2) = 12.5;
      val.mut_array().mut(3).mut_array().resize(2);
      val.mut_array().mut(3).mut_array().mut(0) = 37;
      val.mut_array().mut(3).mut_array().mut(1) = nullptr;
      assert(val.print_to_string() == R"(["$s:$meow",{"x":true},12.5,["$l:37",null]])");
    }

    // leak check
    assert(::alloc_count == 0);
  }
