// This file is part of Poseidon.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "taxon.hpp"
#include <new>
#include <climits>
#include <cmath>
#include <cstdlib>
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
      static constexpr unsigned char bytes[] =
          "\xc9\x89\x0d\x33\xa3\x9b\x0e\x85\x88\x33\x44\x7c";
      ::taxon::Value val = ::rocket::cow_bstring(bytes, 12);
      assert(val.type() == ::taxon::t_binary);
      assert(val.is_binary());
      assert(val.as_binary().size() == 12);
      assert(val.as_binary().compare(bytes, 12) == 0);
      assert(val.as_binary_size() == 12);
      assert(::memcmp(val.as_binary_data(), bytes, 13) == 0);
      assert(val.print_to_string() == R"("$h:c9890d33a39b0e858833447c")");

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
      // `["$s:$meow",{"x":true},12.5,[37,null]]`
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

    {
      ::taxon::Value val;
      assert(!val.parse(&""));
      assert(!val.parse(&"  \t\r\n\t\n\t\r\t"));
      assert(!val.parse(&R"({"a":1,"a":1})"));
    }

    {
      ::taxon::Value val;
      assert(val.parse(&"[]"));
      assert(val.is_array());
      assert(val.as_array().size() == 0);
    }

    {
      ::taxon::Value val;
      assert(val.parse(&"{}"));
      assert(val.is_object());
      assert(val.as_object().size() == 0);
    }

    {
      static constexpr char source[] = R"({"A":"$b:aWVnaHUzQWhndWVqNGVvSg==","B":"$t:987654321"})";
      ::taxon::Value val;
      assert(val.parse(&source));
      assert(val.is_object());
      assert(val.as_object().at(&"A").is_binary());
      assert(val.as_object().at(&"A").as_binary_size() == 16);
      assert(val.as_object().at(&"A").as_binary_size() == 16);
      assert(::std::memcmp(val.as_object().at(&"A").as_binary_data(), "ieghu3Ahguej4eoJ", 17) == 0);
      assert(val.as_object().at(&"B").is_time());
      assert(::std::chrono::time_point_cast<::std::chrono::milliseconds>(
                 val.as_object().at(&"B").as_time()).time_since_epoch().count() == 987654321);
    }

    {
      static constexpr char source[] = R"([ "$h:414345", true	, "$d:-inf",{"y"	:1.5},[1,"\u732b"]])";
      ::taxon::Value val;
      assert(val.parse(&source));
      assert(val.is_array());
      assert(val.as_array().size() == 5);
      assert(val.as_array().at(0).is_binary());
      assert(val.as_array().at(0).as_binary_size() == 3);
      assert(::std::memcmp(val.as_array().at(0).as_binary_data(), "ACE", 4) == 0);
      assert(val.as_array().at(1).is_boolean());
      assert(val.as_array().at(1).as_boolean() == true);
      assert(val.as_array().at(2).is_number());
      assert(val.as_array().at(2).as_number() == -::std::numeric_limits<double>::infinity());
      assert(val.as_array().at(3).is_object());
      assert(val.as_array().at(3).as_object_size() == 1);
      assert(val.as_array().at(3).as_object().at(&"y").is_number());
      assert(val.as_array().at(3).as_object().at(&"y").as_number() == 1.5);
      assert(val.as_array().at(4).is_array());
      assert(val.as_array().at(4).as_array_size() == 2);
      assert(val.as_array().at(4).as_array().at(0).is_number());
      assert(val.as_array().at(4).as_array().at(0).as_number() == 1);
      assert(val.as_array().at(4).as_array().at(1).is_string());
      assert(val.as_array().at(4).as_array().at(1).as_string() == "猫");
    }

    {
      static constexpr char source[] = R"("T\b\f\n\r\t\"\\\/\ud83d\ude02😂")";
      ::taxon::Value val;
      assert(val.parse(&source));
      assert(val.is_string());
      assert(val.as_string_length() == 17);
      assert(::std::memcmp(val.as_string_c_str(), "T\b\f\n\r\t\"\\/😂😂", 18) == 0);
    }

    {
      ::taxon::Value val;
      ::taxon::Parser_Context ctx;

      val.parse_with(ctx, &R"(#)");
      assert(ctx.offset == 0);
      assert(::std::strcmp(ctx.error, "invalid character") == 0);

      val.parse_with(ctx, &R"([)");
      assert(ctx.offset == 1);
      assert(::std::strcmp(ctx.error, "unterminated array") == 0);

      val.parse_with(ctx, &R"([1)");
      assert(ctx.offset == 2);
      assert(::std::strcmp(ctx.error, "unterminated array") == 0);

      val.parse_with(ctx, &R"([1,)");
      assert(ctx.offset == 3);
      assert(::std::strcmp(ctx.error, "missing value") == 0);

      val.parse_with(ctx, &R"([1,])");
      assert(ctx.offset == 3);
      assert(::std::strcmp(ctx.error, "invalid token") == 0);

      val.parse_with(ctx, &R"({)");
      assert(ctx.offset == 1);
      assert(::std::strcmp(ctx.error, "unterminated object") == 0);

      val.parse_with(ctx, &R"({x)");
      assert(ctx.offset == 1);
      assert(::std::strcmp(ctx.error, "missing key string") == 0);

      val.parse_with(ctx, &R"({true)");
      assert(ctx.offset == 1);
      assert(::std::strcmp(ctx.error, "missing key string") == 0);

      val.parse_with(ctx, &R"({"x)");
      assert(ctx.offset == 1);
      assert(::std::strcmp(ctx.error, "unterminated string") == 0);

      val.parse_with(ctx, &R"({"x")");
      assert(ctx.offset == 4);
      assert(::std::strcmp(ctx.error, "missing colon") == 0);

      val.parse_with(ctx, &R"({"x"1)");
      assert(ctx.offset == 4);
      assert(::std::strcmp(ctx.error, "missing colon") == 0);

      val.parse_with(ctx, &R"({"x":)");
      assert(ctx.offset == 5);
      assert(::std::strcmp(ctx.error, "missing value") == 0);

      val.parse_with(ctx, &R"({"x":42)");
      assert(ctx.offset == 7);
      assert(::std::strcmp(ctx.error, "unterminated object") == 0);

      val.parse_with(ctx, &R"({"x":42,)");
      assert(ctx.offset == 8);
      assert(::std::strcmp(ctx.error, "missing key string") == 0);

      val.parse_with(ctx, &R"({"x":42,})");
      assert(ctx.offset == 8);
      assert(::std::strcmp(ctx.error, "missing key string") == 0);
    }

    {
      // recursion
      ::taxon::Value val;
      constexpr ::std::size_t N = 1000000;
      for(::std::size_t i = 0; i < N; ++i) {
        ::taxon::V_array arr;
        arr.emplace_back(::std::move(val));
        val = ::std::move(arr);
      }

      ::rocket::cow_string str;
      str.append(N, '[');
      str.append("null", 4);
      str.append(N, ']');
      assert(val.print_to_string() == str);
    }

    // leak check
    assert(::alloc_count == 0);
  }
