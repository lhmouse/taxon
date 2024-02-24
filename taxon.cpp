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
namespace {

using ::std::uint8_t;
using ::std::uint16_t;
using ::std::uint32_t;
using ::std::uint64_t;
using ::std::ptrdiff_t;
using ::std::size_t;
using ::std::mbstate_t;

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

void
do_print_utf8_string_unquoted(::rocket::tinybuf& buf, const ::rocket::cow_string& str)
  {
    const char* bptr = str.c_str();
    const char* const eptr = str.c_str() + str.length();

    mbstate_t mbstate = { };
    char16_t c16;
    size_t cr;

    while(bptr != eptr) {
      cr = ::std::mbrtoc16(&c16, bptr, static_cast<size_t>(eptr - bptr), &mbstate);
      switch(static_cast<int>(cr))
        {
        case -3:
          // No input has been consumed. A trailing surrogate has been stored into
          // `c16`. Print `c16`.
          break;

        case -2:
          // The input is incomplete, but has been consumed entirely. As we have
          // passed the entire string, it must be invalid. Consume all bytes but
          // print a replacement character.
          bptr = eptr;
          c16 = 0xFFFD;
          break;

        case -1:
          // An invalid byte has been encountered. The `mbstate_t` structure is
          // undefined and shall be reset for the next character. Consume one byte
          // but print a replacement character.
          mbstate = { };
          bptr ++;
          c16 = 0xFFFD;
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

      switch(c16)
        {
        case '\b':
          // `\b`; backspace
          buf.putn("\\b", 2);
          break;

        case '\f':
          // `\f`; form feed
          buf.putn("\\f", 2);
          break;

        case '\n':
          // `\n`; line feed
          buf.putn("\\n", 2);
          break;

        case '\r':
          // `\r`; carrige return
          buf.putn("\\r", 2);
          break;

        case '\t':
          // `\t`; horizontal tab
          buf.putn("\\t", 2);
          break;

        case 0x20 ... 0x7E:
          {
            // ASCII printable
            if((c16 == '"') || (c16 == '\\') || (c16 == '/')) {
              char esc_seq[4] = "\\";
              esc_seq[1] = static_cast<char>(c16);
              buf.putn(esc_seq, 2);
            }
            else
              buf.putc(static_cast<char>(c16));
          }
          break;

        default:
          {
            // UTF-16
            ::rocket::ascii_numput nump;
            nump.put_XU(c16, 4);
            char esc_u_seq[8] = "\\u";
            ::std::memcpy(esc_u_seq + 2, nump.data() + 2, 4);
            buf.putn(esc_u_seq, 6);
          }
          break;
        }
    }
  }

}  // namespace

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
      ::std::fprintf(stderr, "WARNING: %s\n", stdex.what());
    }

    if(!stack.empty()) {
      // Destroy the this value. This will not result in recursion.
      ::rocket::destroy(&(this->m_stor));
      ::rocket::construct(&(this->m_stor), static_cast<Variant&&>(stack.mut_back()));
      stack.pop_back();
      goto do_unpack_loop_;
    }
  }

void
Value::
parse_with(Parser_Context& ctx, ::rocket::tinybuf& buf)
  {
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
;
  }

void
Value::
parse_with(Parser_Context& ctx, const ::rocket::cow_string& str)
  {
    ::rocket::tinybuf_str buf(str, ::rocket::tinybuf::open_read);
    this->parse_with(ctx, buf);
  }

void
Value::
parse_with(Parser_Context& ctx, ::std::FILE* fp)
  {
    ::rocket::tinybuf_file buf(fp, nullptr);
    this->parse_with(ctx, buf);
  }

bool
Value::
parse(::rocket::tinybuf& buf)
  {
    Parser_Context ctx;
    this->parse_with(ctx, buf);
    return !ctx.error;
  }

bool
Value::
parse(const ::rocket::cow_string& str)
  {
    Parser_Context ctx;
    this->parse_with(ctx, str);
    return !ctx.error;
  }

bool
Value::
parse(::std::FILE* fp)
  {
    Parser_Context ctx;
    this->parse_with(ctx, fp);
    return !ctx.error;
  }

void
Value::
print_to(::rocket::tinybuf& buf) const
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
          do_print_utf8_string_unquoted(buf, frm.ito->first.rdstr());
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
          do_print_utf8_string_unquoted(buf, pstor->as<V_string>());
          buf.putc('\"');
        }
        else {
          // starts with `$`; annotated
          buf.putn("\"$s:", 4);
          do_print_utf8_string_unquoted(buf, pstor->as<V_string>());
          buf.putc('\"');
        }
        break;

      case t_binary:
        {
          const auto& bin = pstor->as<V_binary>();
          const unsigned char* bptr = bin.data();
          const unsigned char* const eptr = bin.data() + bin.size();

          constexpr uint8_t hex_sizes[] = { 1, 2, 4, 8, 12, 16, 20, 28, 32 };
          if(::rocket::is_any_of(bin.size(), hex_sizes)) {
            // hex
            buf.putn("\"$h:", 4);

            uint64_t word;
            size_t nrem;
            char hex_word[16];

            const auto hex_digit = [](uint64_t b)
              {
                switch(b) {
                  case  0 ...  9: return static_cast<char>('0' + b);
                  case 10 ... 15: return static_cast<char>('a' + b - 10);
                }
                ROCKET_ASSERT(false);
              };

            while(eptr - bptr >= 8) {
              // 8-byte group
              ::std::memcpy(&word, bptr, 8);
              word = ROCKET_BETOH64(word);
              bptr += 8;

              for(uint32_t t = 0;  t != 16;  ++t) {
                hex_word[t] = hex_digit(word >> 60);
                word <<= 4;
              }

              buf.putn(hex_word, 16);
            }

            if(bptr != eptr) {
              // <=7-byte group
              nrem = static_cast<size_t>(eptr - bptr);
              word = 0;

              for(uint32_t t = 0;  t != nrem;  ++t) {
                word = word << 8 | static_cast<uint64_t>(*bptr) << (64 - nrem * 8);
                bptr ++;
              }

              for(uint32_t t = 0;  t != nrem * 2;  ++t) {
                hex_word[t] = hex_digit(word >> 60);
                word <<= 4;
              }

              buf.putn(hex_word, nrem * 2);
            }
          }
          else {
            // base64
            buf.putn("\"$b:", 4);

            uint32_t word;
            size_t nrem;
            char b64_word[4];

            const auto base64_digit = [](uint32_t b)
              {
                switch(b) {
                  case  0 ... 25: return static_cast<char>('A' + b);
                  case 26 ... 51: return static_cast<char>('a' + b - 26);
                  case 52 ... 61: return static_cast<char>('0' + b - 52);
                  case        62: return '+';
                  case        63: return '/';
                }
                ROCKET_ASSERT(false);
              };

            while(eptr - bptr >= 3) {
              // 3-byte group
              ::std::memcpy(&word, bptr, 4);  // use the null terminator!
              word = ROCKET_BETOH32(word);
              bptr += 3;

              for(uint32_t t = 0;  t != 4;  ++t) {
                b64_word[t] = base64_digit(word >> 26);
                word <<= 6;
              }

              buf.putn(b64_word, 4);
            }

            if(bptr != eptr) {
              // 1-byte or 2-byte group
              nrem = static_cast<size_t>(eptr - bptr);
              word = 0;
              ::std::memcpy(&word, bptr, 2);  // use the null terminator!
              word = ROCKET_BETOH32(word);
              bptr += nrem;

              b64_word[2] = '=';
              b64_word[3] = '=';

              for(uint32_t t = 0;  t != nrem + 1;  ++t) {
                b64_word[t] = base64_digit(word >> 26);
                word <<= 6;
              }

              buf.putn(b64_word, 4);
            }
          }
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
            do_print_utf8_string_unquoted(buf, frm.ito->first.rdstr());
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
  }

void
Value::
print_to(::rocket::cow_string& str) const
  {
    ::rocket::tinybuf_str buf(::std::move(str), ::rocket::tinybuf::open_write);
    this->print_to(buf);
    str = buf.extract_string();
  }

void
Value::
print_to(::std::FILE* fp) const
  {
    ::rocket::tinybuf_file buf(fp, nullptr);
    this->print_to(buf);
  }

::rocket::cow_string
Value::
print_to_string() const
  {
    ::rocket::tinybuf_str buf(::rocket::tinybuf::open_write);
    this->print_to(buf);
    return buf.extract_string();
  }

void
Value::
print_to_stderr() const
  {
    ::rocket::tinybuf_file buf(stderr, nullptr);
    this->print_to(buf);
  }

}  // namespace taxon

template
class ::rocket::variant<TAXON_GENERATOR_IEZUVAH3_(::taxon::V)>;

template
class ::rocket::cow_vector<::taxon::Value>;

template
class ::rocket::cow_hashmap<::rocket::prehashed_string,
  ::taxon::Value, ::rocket::prehashed_string::hash>;
