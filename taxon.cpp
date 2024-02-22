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
#include <climits>
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

static
void
do_mov_char(::rocket::cow_string* tok_opt, Parser_Context& ctx, ::rocket::tinybuf& buf)
  {
    char mb_seq[MB_LEN_MAX];
    ::std::mbstate_t mbstate = { };
    ::std::size_t cr;
    int ch;

    if(tok_opt) {
      // Move the current character from `ctx` to `*tok_opt`.
      cr = ::std::c32rtomb(mb_seq, ctx.c, &mbstate);
      ROCKET_ASSERT(cr <= 4);
      tok_opt->append(mb_seq, cr);
    }

    // Move the next character from `buf` to `ctx`.
  do_get_char32_loop_:
    ctx.c = UINT32_MAX;
    ctx.offset = buf.tell();

    ch = buf.getc();
    if(ch == -1) {
      ctx.error = "no more input data";
      return;
    }

    mb_seq[0] = static_cast<char>(ch);
    cr = ::std::mbrtoc32(&(ctx.c), mb_seq, 1, &mbstate);
    switch(static_cast<int>(cr))
      {
      case -3:
        // UTF-32 is a fixed-length encoding, so this never happens.
        ROCKET_ASSERT(false);

      case -2:
        // The input byte is incomplete and has been consumed. Nothing has been
        // written to `c32`.
        goto do_get_char32_loop_;

      case -1:
        // An invalid byte has been encountered. The input is invalid. Nothing
        // has been written to `c32`.
        ctx.error = "invalid multibyte sequence";
        return;
      }
  }

static
void
do_get_token(::rocket::cow_string& token, Parser_Context& ctx, ::rocket::tinybuf& buf)
  {
    // ws ::= [ '\u0009' '\u000A' '\u000D' '\u0020' ]
    // number ::= '-'? integer fraction? exponent?
    // integer ::= '0' | [ '1'-'9' ] [ '0'-'9' ]*
    // fraction ::= '.' [ '0'-'9' ]+
    // exponent ::= [ 'e' 'E' ] [ '+' '-' ]? [ '0'-'9' ]+
    // string ::= '"' ( '"' | ( escape-seq | char )+ '"' )
    // escape-seq ::= '\' ( simple-escape-char | utf16-escape-seq )
    // simple-escape-char ::= [ '"' '\' '/' 'b' 'f' 'n' 'r' 't' ]
    // utf16-escape-seq ::= 'u' [ '0'-'9' 'A'-'F' 'a'-'f' ]{4}
    // char ::= [ '\u0020' '\u0021' '\u0023'-'\u005B' '\u005D'-'\u007E' '\u0080'-'\u00FF' ]
    token.clear();

    if(ctx.c == UINT32_MAX)
      do_mov_char(nullptr, ctx, buf);

    while((ctx.c == U'\t') || (ctx.c == U'\n') || (ctx.c == U'\r') || (ctx.c == U' '))
      do_mov_char(nullptr, ctx, buf);

    if(ctx.error)
      return;

    switch(ctx.c)
      {
      case U'_':
      case U'a' ... U'z':
      case U'A' ... U'Z':
        {
          // identifier
          while((ctx.c == U'_') || ((ctx.c >= U'0') && (ctx.c <= U'9'))
                || ((ctx.c >= U'A') && (ctx.c <= U'Z'))
                || ((ctx.c >= U'a') && (ctx.c <= U'z')))
            do_mov_char(&token, ctx, buf);
        }
        break;

      case U'+':  // extension
      case U'-':
      case U'0' ... U'9':
        {
          // sign?
          if((ctx.c == U'+') || (ctx.c == U'-'))
            do_mov_char(&token, ctx, buf);

          // integer
          if(ctx.c == '0')
            do_mov_char(&token, ctx, buf);
          else
            while((ctx.c >= U'0') && (ctx.c <= U'9'))
              do_mov_char(&token, ctx, buf);

          if(ctx.error)
            return;

          if(!((token.back() >= '0') && (token.back() <= '9'))) {
            ctx.error = "invalid number";
            return;
          }

          // fraction?
          if(ctx.c == U'.') {
            do_mov_char(&token, ctx, buf);

            while((ctx.c >= U'0') && (ctx.c <= U'9'))
              do_mov_char(&token, ctx, buf);

            if(ctx.error)
              return;

            if(!((token.back() >= '0') && (token.back() <= '9'))) {
              ctx.error = "invalid fraction";
              return;
            }
          }

          // exponent?
          if((ctx.c == U'e') || (ctx.c == U'E')) {
            do_mov_char(&token, ctx, buf);

            if((ctx.c == U'+') || (ctx.c == U'-'))
              do_mov_char(&token, ctx, buf);

            while((ctx.c >= U'0') && (ctx.c <= U'9'))
              do_mov_char(&token, ctx, buf);

            if(ctx.error)
              return;

            if(!((token.back() >= '0') && (token.back() <= '9'))) {
              ctx.error = "invalid exponent";
              return;
            }
          }
        }
        break;

      case '"':
        {
          // string
do_mov_char(&token, ctx, buf);

while((ctx.c != UINT32_MAX) && (ctx.c != U'"'))
  do_mov_char(&token, ctx, buf);
ctx.c = UINT32_MAX;

        }
        break;

      case '[':
      case ',':
      case ']':
      case '{':
      case ':':
      case '}':
        {
          // single character
          token.push_back(static_cast<char>(ctx.c));
          ctx.c = UINT32_MAX;
        }
        break;

      default:
        // invalid
        ctx.error = "invalid character";
        return;
      }
  }

void
Value::
parse_with(Parser_Context& ctx, ::rocket::tinybuf& buf)
  {
    do_check_global_locale();

    // Initialize parser states.
    ctx.c = UINT32_MAX;
    ctx.offset = -1;
    ctx.error = nullptr;

    this->m_stor.emplace<V_null>();

    // Break deep recursion with a handwritten stack.
    struct xFrame
      {
        char closure;
        V_array* psa;
        V_object* pso;
        ::rocket::prehashed_string key;
      };

    ::rocket::cow_vector<xFrame> stack;
    ::rocket::ascii_numget numg;
    ::rocket::cow_string token;
    Variant* pstor = &(this->m_stor);

    // json ::= ws* value
    // value ::= 'null' | 'true' | 'false' | array | object | number | string
    // array ::= '[' ws* ( ']' | value ws* comma-value-ws* ']' )
    // comma-value-ws ::= ',' ws* value ws*
    // object ::= '{' ws* ( '}' | string ws* ':' ws* value ws* comma-kv-pair-ws* '}")
    // comma-kv-pair-ws ::= ',' ws* string ws* ':' ws* value ws*
  do_pack_loop_:

while(ctx.error == nullptr)
 do_get_token(token, ctx, buf);


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

static
void
do_print_utf8_string_unquoted(::rocket::tinybuf& buf, const ::rocket::cow_string& str)
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
          // The input is incomplete, but has been consumed entirely. As we have
          // passed the entire string, it must be invalid. Consume all bytes but
          // print a replacement character.
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

      switch(c16)
        {
        case u'\b':
          // `\b`; backspace
          buf.putn("\\b", 2);
          break;

        case u'\f':
          // `\f`; form feed
          buf.putn("\\f", 2);
          break;

        case u'\n':
          // `\n`; line feed
          buf.putn("\\n", 2);
          break;

        case u'\r':
          // `\r`; carrige return
          buf.putn("\\r", 2);
          break;

        case u'\t':
          // `\t`; horizontal tab
          buf.putn("\\t", 2);
          break;

        case u'\u0020' ... u'\u007E':
          {
            // ASCII printable
            if((c16 == u'"') || (c16 == u'\\') || (c16 == u'/')) {
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

static
void
do_print_binary_in_hex(::rocket::tinybuf& buf, const ::rocket::cow_bstring& bin)
  {
    const unsigned char* bptr = bin.data();
    const unsigned char* const eptr = bin.data() + bin.size();
    ::std::uint64_t word;
    ::rocket::ascii_numput nump;

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
do_get_base64_digit(::std::uint32_t word)
  {
    return (word < 26) ? static_cast<char>('A' + word)
         : (word < 52) ? static_cast<char>('a' + word - 26)
         : (word < 62) ? static_cast<char>('0' + word - 52)
         : (word < 63) ? '+' : '/';
  }

static
bool
do_use_hex_for(const ::rocket::cow_bstring& bin)
  {
    return (bin.size() == 1) || (bin.size() == 2) || (bin.size() == 4)
           || (bin.size() == 8) || (bin.size() == 12) || (bin.size() == 16)
           || (bin.size() == 20) || (bin.size() == 28)|| (bin.size() == 32);
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
        if(do_use_hex_for(pstor->as<V_binary>())) {
          // short; hex
          buf.putn("\"$h:", 4);
          do_print_binary_in_hex(buf, pstor->as<V_binary>());
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
