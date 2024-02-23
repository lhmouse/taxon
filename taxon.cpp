// This file is part of Poseidon.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "taxon.hpp"
#include <rocket/tinybuf_str.hpp>
#include <rocket/tinybuf_file.hpp>
#include <rocket/ascii_numget.hpp>
#include <rocket/ascii_numput.hpp>
#include <cmath>
#include <cstdio>
#include <clocale>
#include <cuchar>
namespace taxon {

Value::
~Value()
  {
    // Break deep recursion with a handwritten stack.
    struct xVariant : Variant  { };
    ::rocket::cow_vector<xVariant> stack;

  do_unpack_loop_:
    try {
      // Unpack arrays or objects.
      auto psa = this->m_stor.mut_ptr<V_array>();
      if(psa && psa->unique())
        for(auto it = psa->mut_begin();  it != psa->end();  ++it)
          stack.emplace_back().swap(it->m_stor);

      auto pso = this->m_stor.mut_ptr<V_object>();
      if(pso && pso->unique())
        for(auto it = pso->mut_begin();  it != pso->end();  ++it)
          stack.emplace_back().swap(it->second.m_stor);
    }
    catch(::std::exception& stdex) {
      // Ignore this exception.
      ::fprintf(stderr, "WARNING: %s\n", stdex.what());
    }

    if(!stack.empty()) {
      // Destroy the this value. This will not result in recursion.
      ::rocket::destroy(&(this->m_stor));
      ::rocket::construct(&(this->m_stor), static_cast<Variant&&>(stack.mut_back()));
      stack.pop_back();
      goto do_unpack_loop_;
    }
  }

static
void
do_check_global_locale() noexcept
  {
    static constexpr char keywords[][8] = { ".utf8", ".utf-8", ".UTF-8" };
    const char* locale = ::std::setlocale(LC_ALL, nullptr);
    const char* found = nullptr;

    if(locale)
      for(const char* kw : keywords)
        if((found = ::std::strstr(locale, kw)) != nullptr)
          break;

    if(!found)
      ::std::fprintf(stderr,
            "WARNING: Please set a UTF-8 locale instead of `%s`.\n",
            locale ? locale : "(no locale)");
  }

bool
Value::
parse(::rocket::tinybuf& buf, Parser_Result* result_opt)
  {
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO

    return false;
  }

bool
Value::
parse_string(const ::rocket::cow_string& str, Parser_Result* result_opt)
  {
    ::rocket::tinybuf_str buf(str);
    bool succ = this->parse(buf, result_opt);
    return succ;
  }

bool
Value::
parse_file(::FILE* fp, Parser_Result* result_opt)
  {
    ::rocket::tinybuf_file buf(fp, nullptr);
    bool succ = this->parse(buf, result_opt);
    return succ;
  }

static
void
do_print_utf8_string_unquoted(::rocket::tinybuf& buf, ::rocket::ascii_numput& nump,
                              const ::rocket::cow_string& str)
  {
    const char* bptr = str.c_str();
    const char* const eptr = str.c_str() + str.length();
    ::std::mbstate_t mbstate = { };
    char16_t c16;
    ::std::size_t cr;

    while(bptr != eptr) {
      cr = ::std::mbrtoc16(&c16, bptr, static_cast<::std::size_t>(eptr - bptr), &mbstate);
      switch(static_cast<int>(cr))
        {
        case -3:
          // No input has been consumed. A trailing surrogate has been stored into
          // `c16`. Print `c16`.
          break;

        case -2:
          // The input is complete but has been consumed entirely. As we have passed
          // the entire string, it must be invalid. Consume all bytes but print a
          // replacement character.
          bptr = eptr;
          c16 = u'\uFFFD';
          break;

        case -1:
          // An invalid byte has been encountered. The `mbstate_t` structure is
          // undefined and shall be reset for the next character. Consume one byte
          // but print a replacement character.
          mbstate = { };
          bptr ++;
          c16 = u'\uFFFD';
          break;

        case 0:
          // A null byte has been consumed and stored into `c16`.
          bptr ++;
          break;

        default:
          // `cr` bytes have been consumed, converted and stored into `c16`.
          bptr += cr;
          break;
        }

      if(c16 == u'\"') {
        // `\"`
        buf.putn("\\\"", 2);
      }
      else if(c16 == u'\\') {
        // `\\`
        buf.putn("\\\\", 2);
      }
      else if(c16 == u'\b') {
        // `\b`; backspace
        buf.putn("\\b", 2);
      }
      else if(c16 == u'\f') {
        // `\f`; form feed
        buf.putn("\\f", 2);
      }
      else if(c16 == u'\n') {
        // `\n`; line feed
        buf.putn("\\n", 2);
      }
      else if(c16 == u'\r') {
        // `\r`; carrige return
        buf.putn("\\r", 2);
      }
      else if(c16 == u'\t') {
        // `\t`; horizontal tab
        buf.putn("\\t", 2);
      }
      else if((c16 >= 0x20) && (c16 <= 0x7E)) {
        // ASCII printable
        buf.putc(static_cast<char>(c16));
      }
      else {
        // UTF-16
        nump.put_XU(c16, 4);
        char temp[] = { '\\', 'u', nump[2], nump[3], nump[4], nump[5] };
        buf.putn(temp, sizeof(temp));
      }
    }
  }

static
void
do_print_binary_in_hex(::rocket::tinybuf& buf, ::rocket::ascii_numput& nump,
                       const ::rocket::cow_bstring& bin)
  {
    const unsigned char* bptr = bin.data();
    const unsigned char* const eptr = bin.data() + bin.size();
    ::std::uint64_t word;

    while(eptr - bptr >= 8) {
      // 8-byte group
      ::std::memcpy(&word, bptr, 8);
      bptr += 8;
      nump.put_XU(ROCKET_BETOH64(word), 16);
      buf.putn(nump.data() + 2, 16);
    }

    if(eptr - bptr >= 4) {
      // 4-byte group
      ::std::memcpy(&word, bptr, 4);
      bptr += 4;
      nump.put_XU(ROCKET_BETOH64(word), 16);
      buf.putn(nump.data() + 2, 8);
    }

    while(bptr != eptr) {
      // bytewise
      nump.put_XU(*bptr, 2);
      bptr ++;
      buf.putn(nump.data() + 2, 2);
    }
  }

static constexpr
char
do_get_base64_digit(::std::uint32_t word) noexcept
  {
    return (word < 26) ? static_cast<char>(u'A' + word)
         : (word < 52) ? static_cast<char>(u'a' + word - 26)
         : (word < 62) ? static_cast<char>(u'0' + word - 52)
         : (word < 63) ? '+' : '/';
  }

static
void
do_print_binary_in_base64(::rocket::tinybuf& buf, const ::rocket::cow_bstring& bin)
  {
    const unsigned char* bptr = bin.data();
    const unsigned char* const eptr = bin.data() + bin.size();
    ::std::ptrdiff_t remainder;
    ::std::uint32_t word;
    char b64word[4];

    while(eptr - bptr >= 3) {
      // 3-byte group
      ::std::memcpy(&word, bptr, 4);  // make use of the null terminator!
      bptr += 3;
      word = ROCKET_BETOH32(word);

      b64word[0] = do_get_base64_digit(word >> 26);
      b64word[1] = do_get_base64_digit(word >> 20 & 0x3F);
      b64word[2] = do_get_base64_digit(word >> 14 & 0x3F);
      b64word[3] = do_get_base64_digit(word >>  8 & 0x3F);
      buf.putn(b64word, 4);
    }

    if(bptr != eptr) {
      // 1-byte or 2-byte group
      remainder = eptr - bptr;
      word = 0;
      ::std::memcpy(&word, bptr, 2);  // make use of the null terminator!
      bptr = eptr;
      word = ROCKET_BETOH32(word);

      b64word[0] = do_get_base64_digit(word >> 26);
      b64word[1] = do_get_base64_digit(word >> 20 & 0x3F);
      b64word[2] = (remainder == 1) ? '=' : do_get_base64_digit(word >> 14 & 0x3F);
      b64word[3] = '=';
      buf.putn(b64word, 4);
    }
  }

::rocket::tinybuf&
Value::
print(::rocket::tinybuf& buf) const
  {
    do_check_global_locale();

    // Break deep recursion with a handwritten stack.
    struct xFrame
      {
        char closure;
        const V_array* psa;
        V_array::const_iterator ita;
        const V_object* pso;
        V_object::const_iterator ito;
      };

    ::rocket::cow_vector<xFrame> stack;
    ::rocket::ascii_numput nump;
    static constexpr ::std::uint8_t use_hex_for[] = { 1, 2, 4, 8, 16, 20, 32 };
    const Variant* pstor = &(this->m_stor);

  do_unpack_loop_:
    switch(static_cast<Type>(pstor->index()))
      {
      case t_null:
        buf.putn("null", 4);
        break;

      case t_array:
        if(!pstor->as<V_array>().empty()) {
          // open
          auto& frm = stack.emplace_back();
          frm.closure = ']';
          frm.psa = &(pstor->as<V_array>());
          frm.ita = frm.psa->begin();
          buf.putc('[');
          pstor = &(frm.ita->m_stor);
          goto do_unpack_loop_;
        }
        buf.putn("[]", 2);
        break;

      case t_object:
        if(!pstor->as<V_object>().empty()) {
          // open
          auto& frm = stack.emplace_back();
          frm.closure = '}';
          frm.pso = &(pstor->as<V_object>());
          frm.ito = frm.pso->begin();
          buf.putn("{\"", 2);
          do_print_utf8_string_unquoted(buf, nump, frm.ito->first.rdstr());
          buf.putn("\":", 2);
          pstor = &(frm.ito->second.m_stor);
          goto do_unpack_loop_;
        }
        buf.putn("{}", 2);
        break;

      case t_boolean:
        nump.put_TB(pstor->as<V_boolean>());
        buf.putn(nump.data(), nump.size());
        break;

      case t_integer:
        nump.put_DI(pstor->as<V_integer>());
        buf.putn("\"$l:", 4);
        buf.putn(nump.data(), nump.size());
        buf.putc('\"');
        break;

      case t_number:
        if(::std::isfinite(pstor->as<V_number>())) {
          // finite; unquoted
          nump.put_DD(pstor->as<V_number>());
          buf.putn(nump.data(), nump.size());
        }
        else {
          // non-finite; annotated
          buf.putn("\"$d:", 4);
          nump.put_DD(pstor->as<V_number>());
          buf.putn(nump.data(), nump.size());
          buf.putc('\"');
        }
        break;

      case t_string:
        if(pstor->as<V_string>()[0] != '$') {
          // general; quoted
          buf.putc('\"');
          do_print_utf8_string_unquoted(buf, nump, pstor->as<V_string>());
          buf.putc('\"');
        }
        else {
          // starts with `$`; annotated
          buf.putn("\"$s:", 4);
          do_print_utf8_string_unquoted(buf, nump, pstor->as<V_string>());
          buf.putc('\"');
        }
        break;

      case t_binary:
        if(::rocket::is_any_of(pstor->as<V_binary>().size(), use_hex_for)) {
          // short; hex
          buf.putn("\"$h:", 4);
          do_print_binary_in_hex(buf, nump, pstor->as<V_binary>());
          buf.putc('\"');
        }
        else {
          // general; base64
          buf.putn("\"$b:", 4);
          do_print_binary_in_base64(buf, pstor->as<V_binary>());
          buf.putc('\"');
        }
        break;

      case t_time:
        nump.put_DI(::std::chrono::time_point_cast<::std::chrono::milliseconds>(
                                     pstor->as<V_time>()).time_since_epoch().count());
        buf.putn("\"$t:", 4);
        buf.putn(nump.data(), nump.size());
        buf.putc('\"');
        break;

      default:
        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "taxon::Value: unknown type enumeration `%d`",
              static_cast<int>(pstor->index()));
      }

    while(!stack.empty()) {
      auto& frm = stack.mut_back();
      switch(frm.closure)
        {
        case ']':
          if(++ frm.ita != frm.psa->end()) {
            // next
            buf.putc(',');
            pstor = &(frm.ita->m_stor);
            goto do_unpack_loop_;
          }
          break;

        case '}':
          if(++ frm.ito != frm.pso->end()) {
            // next
            buf.putn(",\"", 2);
            do_print_utf8_string_unquoted(buf, nump, frm.ito->first.rdstr());
            buf.putn("\":", 2);
            pstor = &(frm.ito->second.m_stor);
            goto do_unpack_loop_;
          }
          break;

        default:
          ROCKET_UNREACHABLE();
        }

      // close
      buf.putc(frm.closure);
      stack.pop_back();
    }

    return buf;
  }

::rocket::tinyfmt&
Value::
print(::rocket::tinyfmt& fmt) const
  {
    this->print(fmt.mut_buf());
    return fmt;
  }

::rocket::cow_string
Value::
print_to_string() const
  {
    ::rocket::tinybuf_str buf;
    this->print(buf);
    return buf.extract_string();
  }

bool
Value::
print_to_file(::FILE* fp) const
  {
    ::rocket::tinybuf_file buf(fp, nullptr);
    this->print(buf);
    return ::std::ferror(buf.get_handle()) == 0;
  }

}  // namespace taxon

template
class ::rocket::variant<TAXON_GENERATOR_IEZUVAH3_(::taxon::V)>;

template
class ::rocket::cow_vector<::taxon::Value>;

template
class ::rocket::cow_hashmap<::rocket::prehashed_string,
  ::taxon::Value, ::rocket::prehashed_string::hash>;
