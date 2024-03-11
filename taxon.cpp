// This file is part of TAXON.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "taxon.hpp"
#include <rocket/tinybuf_str.hpp>
#include <rocket/tinybuf_file.hpp>
#include <rocket/ascii_numput.hpp>
#include <rocket/ascii_numget.hpp>
#include <cmath>
#include <cstdio>
#include <climits>
#include <cfloat>
#include <cuchar>
template class ::rocket::variant<TAXON_TYPES_IEZUVAH3_(::taxon::V)>;
template class ::rocket::cow_vector<::taxon::Value>;
template class ::rocket::cow_hashmap<::rocket::prehashed_string,
  ::taxon::Value, ::rocket::prehashed_string::hash>;
namespace taxon {
namespace {

void
do_set_error(Parser_Context& ctx, const char* error)
  {
    ctx.c = -1;
    ctx.error = error;
  }

bool
do_is_digit(char c)
  {
    return static_cast<uint8_t>(c - '0') <= 9;
  }

void
do_mov_char(::rocket::cow_string* tok_opt, Parser_Context& ctx, ::rocket::tinybuf& buf)
  {
    if(tok_opt) {
      // Move the current character from `ctx` to `*tok_opt`.
      ::std::mbstate_t mbstate = { };
      char mbs[MB_LEN_MAX];
      size_t cr = ::std::c32rtomb(mbs, static_cast<char32_t>(ctx.c), &mbstate);
      if(static_cast<int>(cr) < 0)
        return do_set_error(ctx, "character not representable in current locale");

      // If `c32rtomb()` has returned zero, then it is a null character.
      if(cr <= 1)
        tok_opt->push_back(mbs[0]);
      else
        tok_opt->append(mbs, cr);
    }

    // Move the next character from `buf` to `ctx.c`. Source data must be UTF-8.
    ctx.c_offset = buf.tell();
    ctx.c = buf.getc();
    switch(ctx.c)
      {
      case 0x00 ... 0x7F:
        // one byte
        break;

      case 0xC2 ... 0xDF:
      case 0xE0 ... 0xEF:
      case 0xF0 ... 0xF4:
        {
          // 2 bytes: 110? ????
          // 3 bytes: 1110 ????
          // 4 bytes: 1111 0???
          int nbytes = (((ctx.c >> 4) | ~(ctx.c >> 5)) & 3) + 1;

          for(int t = 1;  t != nbytes;  ++t) {
            int ch = buf.getc();
            if((ch < 0x80) || (ch > 0xBF))
              return do_set_error(ctx, "invalid UTF-8 sequence in source string");

            ctx.c <<= 6;
            ctx.c |= ch - 0x80;
            ctx.c &= (1 << (nbytes * 5 + 1)) - 1;
          }

          if((ctx.c < 0x80) || (ctx.c < (1 << (nbytes * 5 - 4)))  // overlong
                || ((ctx.c >= 0xD800) && (ctx.c <= 0xDFFF))  // surrogates
                || (ctx.c >= 0x110000))
            return do_set_error(ctx, "invalid UTF-8 sequence in source string");
        }
        break;

      case -1:
        return do_set_error(ctx, "no more input data");

      default:
        return do_set_error(ctx, "invalid UTF-8 sequence in source string");
      }
  }

struct utf_range
  {
    int32_t lo, hi;

    constexpr utf_range(int32_t x) : lo(x), hi(x) { }
    constexpr utf_range(int32_t x, int32_t y) : lo(x), hi(y) { }
  };

bool
do_char_in(int32_t c, initializer_list<utf_range> range)
  {
    for(const auto& r : range)
      if((c >= r.lo) && (c <= r.hi))
        return true;
    return false;
  }

void
do_get_token(::rocket::cow_string& token, Parser_Context& ctx, ::rocket::tinybuf& buf)
  {
    if(ctx.c == -1)
      ctx.c = '\t';

    while((ctx.c == '\t') || (ctx.c == '\n') || (ctx.c == '\r') || (ctx.c == ' '))
      do_mov_char(nullptr, ctx, buf);

    token.clear();
    ctx.offset = ctx.c_offset;
    ctx.error = nullptr;

    switch(ctx.c)
      {
      case '+':  // extension
      case '-':
      case '0' ... '9':
        {
          // sign?
          if(do_char_in(ctx.c, { '+', '-' }))
            do_mov_char(&token, ctx, buf);

          // integer
          if(ctx.c == '0')
            do_mov_char(&token, ctx, buf);
          else
            while(do_char_in(ctx.c, { {'0','9'} }))
              do_mov_char(&token, ctx, buf);

          if(!do_is_digit(token.back()))
            return do_set_error(ctx, "invalid number");

          // fraction?
          if(ctx.c == '.') {
            do_mov_char(&token, ctx, buf);

            while(do_char_in(ctx.c, { {'0','9'} }))
              do_mov_char(&token, ctx, buf);

            if(!do_is_digit(token.back()))
              return do_set_error(ctx, "invalid fraction");
          }

          // exponent?
          if(do_char_in(ctx.c, { 'e', 'E' })) {
            do_mov_char(&token, ctx, buf);

            if(do_char_in(ctx.c, { '+', '-' }))
              do_mov_char(&token, ctx, buf);

            while(do_char_in(ctx.c, { {'0','9'} }))
              do_mov_char(&token, ctx, buf);

            if(!do_is_digit(token.back()))
              return do_set_error(ctx, "invalid exponent");
          }

          // If the end of input has been reached, `ctx.error` may be set. We will
          // not return an error, so clear it.
          ROCKET_ASSERT(!token.empty());
          ctx.error = nullptr;
        }
        break;

      case '"':
        {
          // string
          do_mov_char(&token, ctx, buf);

          while(ctx.c != '"') {
            if(ctx.c == -1)
              return do_set_error(ctx, "unterminated string");

            if((ctx.c <= 0x1F) || (ctx.c == 0x7F))
              return do_set_error(ctx, "invalid character in string");

            if(ctx.c == '\\') {
              // Unescape the sequence and store it into `ctx.c`.
              do_mov_char(nullptr, ctx, buf);
              switch(ctx.c)
                {
                case '"':
                case '\\':
                case '/':
                  break;

                case 'b':
                  ctx.c = '\b';
                  break;

                case 'f':
                  ctx.c = '\f';
                  break;

                case 'n':
                  ctx.c = '\n';
                  break;

                case 'r':
                  ctx.c = '\r';
                  break;

                case 't':
                  ctx.c = '\t';
                  break;

                case 'u':
                  {
                    int32_t utf_hi = 0;
                    int32_t utf_lo = 0;

                    // Get a UTF-32 character, which may be either a non-surrogate
                    // UTF-16 code unit, or a surrogate pair. Leading surrogates are
                    // within ['\uD800','\uDBFF'] and trailing surrogates are within
                    // ['\uDC00','\uDFFF'].
                    for(uint32_t t = 0;  t != 4;  ++t) {
                      do_mov_char(nullptr, ctx, buf);
                      utf_hi <<= 4;
                      switch(ctx.c)
                        {
                        case '0' ... '9':
                          utf_hi |= ctx.c - '0';
                          break;

                        case 'A' ... 'F':
                        case 'a' ... 'f':
                          utf_hi |= (ctx.c | 0x20) - 'a' + 10;
                          break;

                        default:
                          return do_set_error(ctx, "invalid hexadecimal digit");
                        }
                    }

                    if((utf_hi >= 0xDC00) && (utf_hi <= 0xDFFF))
                      return do_set_error(ctx, "dangling trailing surrogate");

                    if((utf_hi <= 0xD7FF) || (utf_hi >= 0xE000)) {
                      // single
                      ctx.c = utf_hi;
                    }
                    else {
                      // surrogates
                      do_mov_char(nullptr, ctx, buf);
                      if(ctx.c != '\\')
                        return do_set_error(ctx, "missing trailing surrogate");

                      do_mov_char(nullptr, ctx, buf);
                      if(ctx.c != 'u')
                        return do_set_error(ctx, "missing trailing surrogate");

                      for(uint32_t t = 0;  t != 4;  ++t) {
                        do_mov_char(nullptr, ctx, buf);
                        utf_lo <<= 4;
                        switch(ctx.c)
                          {
                          case '0' ... '9':
                            utf_lo |= ctx.c - '0';
                            break;

                          case 'A' ... 'F':
                          case 'a' ... 'f':
                            utf_lo |= (ctx.c | 0x20) - 'a' + 10;
                            break;

                          default:
                            return do_set_error(ctx, "invalid hexadecimal digit");
                          }
                      }

                      if((utf_lo < 0xDC00) || (utf_lo > 0xDFFF))
                        return do_set_error(ctx, "dangling leading surrogate");

                      ctx.c = 0x10000 + ((utf_hi - 0xD800) << 10) + (utf_lo - 0xDC00);
                    }
                  }
                  break;

                default:
                  return do_set_error(ctx, "invalid escape sequence");
                }
            }

            // Copy `ctx.c`.
            do_mov_char(&token, ctx, buf);
            continue;
          }

          // Drop the terminating quotation mark for simplicity. Do not attempt to
          // get the next character, as some of these tokens may terminate the input,
          // and the stream may be blocking but we can't really know whether there
          // are more data.
          ctx.c = -1;
        }
        break;

      case '_':
      case 'a' ... 'z':
      case 'A' ... 'Z':
        {
          // identifier
          while(do_char_in(ctx.c, { '_', {'0','9'}, {'A','Z'}, {'a','z'} }))
            do_mov_char(&token, ctx, buf);

          // If the end of input has been reached, `ctx.error` may be set. We will
          // not return an error, so clear it.
          ROCKET_ASSERT(!token.empty());
          ctx.error = nullptr;
        }
        break;

      case '[':
      case ',':
      case ']':
      case '{':
      case ':':
      case '}':
        {
          // Take each of these characters as a single token. Do not attempt to get
          // the next character, as some of these tokens may terminate the input,
          // and the stream may be blocking but we can't really know whether there
          // are more data.
          token.push_back(static_cast<char>(ctx.c));
          ctx.c = -1;
        }
        break;

      case -1:
        return do_set_error(ctx, "end of input");

      default:
        return do_set_error(ctx, "invalid character");
      }
  }

void
do_print_utf8_string_unquoted(::rocket::tinybuf& buf, const ::rocket::cow_string& str)
  {
    const char* bptr = str.c_str();
    const char* const eptr = bptr + str.length();

    ::std::mbstate_t mbstate = { };
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

        case '"':   // 0x22
        case '/':   // 0x2F
        case '\\':  // 0x5C
          {
            // plain escaped
            char esc_seq[4] = "\\";
            esc_seq[1] = static_cast<char>(c16);
            buf.putn(esc_seq, 2);
          }
          break;

        case 0x20 ... 0x21:
        case 0x23 ... 0x2E:
        case 0x30 ... 0x5B:
        case 0x5D ... 0x7E:
          // ASCII printable
          buf.putc(static_cast<char>(c16));
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
parse_with(Parser_Context& ctx, ::rocket::tinybuf& buf, Options opts)
  {
    // Initialize parser states.
    ctx.c = -1;
    ctx.offset = -1;
    ctx.error = nullptr;

    // Break deep recursion with a handwritten stack.
    struct xFrame
      {
        char closure;
        Variant* target;
        V_array* psa;
        V_object* pso;
      };

    ::rocket::cow_vector<xFrame> stack;
    ::rocket::cow_string token;
    ::rocket::ascii_numget numg;
    Variant* pstor = &(this->m_stor);

    do_get_token(token, ctx, buf);
    if(ctx.error)
      return;

  do_pack_value_loop_:
    switch(token[0])
      {
      case 'n':
        {
          if(token == "null")
            pstor->emplace<V_null>();
          else
            return do_set_error(ctx, "invalid token");
        }
        break;

      case 't':
        {
          if(token == "true")
            pstor->emplace<V_boolean>(true);
          else
            return do_set_error(ctx, "invalid token");
        }
        break;

      case 'f':
        {
          if(token == "false")
            pstor->emplace<V_boolean>(false);
          else
            return do_set_error(ctx, "invalid token");
        }
        break;

      case '[':
        // array
        do_get_token(token, ctx, buf);
        if(token.empty())
          return do_set_error(ctx, "unterminated array");
        else if(ctx.error)
          return;

        if(token != "]") {
          // open
          auto& frm = stack.emplace_back();
          frm.closure = ']';
          frm.target = pstor;
          frm.psa = &(pstor->emplace<V_array>());
          pstor = &(frm.psa->emplace_back().m_stor);
          goto do_pack_value_loop_;
        }

        // empty
        pstor->emplace<V_array>();
        break;

      case '{':
        // object
        do_get_token(token, ctx, buf);
        if(token.empty())
          return do_set_error(ctx, "unterminated object");
        else if(ctx.error)
          return;

        if(token != "}") {
          // We are inside an object, so this token must be a key string, followed
          // by a colon, followed by its value.
          if(token[0] != '"')
            return do_set_error(ctx, "missing key string");

          ::rocket::prehashed_string key;
          key.assign(token.data() + 1, token.size() - 1);

          do_get_token(token, ctx, buf);
          if(token != ":")
            return do_set_error(ctx, "missing colon");

          do_get_token(token, ctx, buf);
          if(token.empty())
            return do_set_error(ctx, "missing value");
          else if(ctx.error)
            return;

          // open
          auto& frm = stack.emplace_back();
          frm.closure = '}';
          frm.target = pstor;
          frm.pso = &(pstor->emplace<V_object>());
          auto emplace_result = frm.pso->try_emplace(::std::move(key), nullptr);
          ROCKET_ASSERT(emplace_result.second);
          pstor = &(emplace_result.first->second.m_stor);
          goto do_pack_value_loop_;
        }

        // empty
        pstor->emplace<V_object>();
        break;

      case '+':
      case '-':
      case '0' ... '9':
        {
          // number
          numg.parse_DD(token.data(), token.size());
          numg.cast_D(pstor->emplace<V_number>(), -DBL_MAX, DBL_MAX);
          if(numg.overflowed())
            return do_set_error(ctx, "number out of range");
        }
        break;

      case '"':
        if((opts & option_json_mode) || (token[1] != '$')) {
          // plain string
          pstor->emplace<V_string>(token.data() + 1, token.size() - 1);
        }
        else {
          // annotated string
          if((token.size() < 4) || (token[3] != ':'))
            return do_set_error(ctx, "invalid type annotator");

          const char* bptr = token.c_str() + 4;
          size_t tlen = token.length() - 4;
          const char* const eptr = bptr + tlen;

          switch(token[2])
            {
            case 'l':
              {
                // 64-bit integer
                if(numg.parse_I(bptr, tlen) != tlen)
                  return do_set_error(ctx, "invalid 64-bit integer");

                numg.cast_I(pstor->emplace<V_integer>(), INT64_MIN, INT64_MAX);
                if(numg.overflowed())
                  return do_set_error(ctx, "64-bit integer out of range");
              }
              break;

            case 'd':
              {
                // double-precision number
                if(numg.parse_D(bptr, tlen) != tlen)
                  return do_set_error(ctx, "invalid double-precision number");

                // Values that are out of range are converted to infinities and
                // are always accepted.
                numg.cast_D(pstor->emplace<V_number>(), -HUGE_VAL, HUGE_VAL);
              }
              break;

            case 's':
              {
                // string
                pstor->emplace<V_string>(bptr, tlen);
              }
              break;

            case 'h':
              {
                // hex
                if(tlen / 2 * 2 != tlen)
                  return do_set_error(ctx, "invalid hexadecimal string");

                auto& bin = pstor->emplace<V_binary>();
                bin.reserve(tlen / 2);

                while(bptr != eptr) {
                  uint32_t word = 0;

                  for(uint32_t t = 0;  t != 2;  ++t) {
                    uint32_t ch = static_cast<uint8_t>(*bptr);
                    bptr ++;
                    word <<= 4;

                    switch(ch)
                      {
                      case '0' ... '9':
                        word |= ch - '0';
                        break;

                      case 'A' ... 'F':
                      case 'a' ... 'f':
                        word |= (ch | 0x20) - 'a' + 10;
                        break;

                      default:
                        return do_set_error(ctx, "invalid hexadecimal digit");
                      }
                  }

                  bin.push_back(static_cast<unsigned char>(word));
                }
              }
              break;

            case 'b':
              {
                // base64
                if(tlen / 4 * 4 != tlen)
                  return do_set_error(ctx, "invalid base64 string");

                auto& bin = pstor->emplace<V_binary>();
                bin.reserve(tlen / 4 * 3);

                while(bptr != eptr) {
                  uint32_t word = 0;
                  uint32_t ndigits = 4;

                  for(uint32_t t = 0;  t != 4;  ++t) {
                    word <<= 6;
                    uint32_t ch = static_cast<uint8_t>(*bptr);
                    bptr ++;

                    // A padding character may only occur at subscript 2 or 3. If
                    // the character at subscript 2 is a padding character, the
                    // character on subscript 3 must also be.
                    if((t <= 1) && (ch == '='))
                      return do_set_error(ctx, "invalid base64 string");

                    if((ndigits != 4) && (ch != '='))
                      return do_set_error(ctx, "invalid base64 string");

                    switch(ch)
                      {
                      case 'A' ... 'Z':
                        word |= ch - 'A';
                        break;

                      case 'a' ... 'z':
                        word |= ch - 'a' + 26;
                        break;

                      case '0' ... '9':
                        word |= ch - '0' + 52;
                        break;

                      case '+':
                        word |= 62;
                        break;

                      case '/':
                        word |= 63;
                        break;

                      case '=':
                        ndigits --;
                        break;

                      default:
                        return do_set_error(ctx, "invalid base64 string");
                      }
                  }

                  for(uint32_t t = 0;  t != ndigits - 1;  ++t) {
                    bin.push_back(static_cast<unsigned char>(word >> 16));
                    word <<= 8;
                  }
                }
              }
              break;

            case 't':
              {
                // timestamp in milliseconds
                if(numg.parse_I(bptr, tlen) != tlen)
                  return do_set_error(ctx, "invalid timestamp");

                // The allowed timestamp values are from '1900-01-01T00:00:00.000Z'
                // to '9999-12-31T23:59:59.999Z'.
                int64_t count;
                numg.cast_I(count, -2208988800000, 253402300799999);
                pstor->emplace<V_time>(::std::chrono::milliseconds(count));
                if(numg.overflowed())
                  return do_set_error(ctx, "timestamp out of range");
              }
              break;

            default:
              return do_set_error(ctx, "unknown type annotator");
            }
        }
        break;

      default:
        return do_set_error(ctx, "invalid token");
      }

    while(!stack.empty()) {
      auto& frm = stack.mut_back();
      switch(frm.closure)
        {
        case ']':
          {
            do_get_token(token, ctx, buf);
            if(token.empty())
              return do_set_error(ctx, "unterminated array");
            else if(ctx.error)
              return;

            if(token == ",") {
              do_get_token(token, ctx, buf);
              if(token.empty())
                return do_set_error(ctx, "missing value");
              else if(ctx.error)
                return;

              // next
              pstor = &(frm.psa->emplace_back().m_stor);
              goto do_pack_value_loop_;
            }
          }
          break;

        case '}':
          {
            do_get_token(token, ctx, buf);
            if(token.empty())
              return do_set_error(ctx, "unterminated object");
            else if(ctx.error)
              return;

            if(token == ",") {
              do_get_token(token, ctx, buf);
              if(token.empty())
                return do_set_error(ctx, "missing key string");
              else if(ctx.error)
                return;

              if(token[0] != '"')
                return do_set_error(ctx, "missing key string");

              ::rocket::prehashed_string key;
              key.assign(token.data() + 1, token.size() - 1);
              auto emplace_result = frm.pso->try_emplace(::std::move(key), nullptr);
              if(!emplace_result.second)
                return do_set_error(ctx, "duplicate key string");

              do_get_token(token, ctx, buf);
              if(token != ":")
                return do_set_error(ctx, "missing colon");

              do_get_token(token, ctx, buf);
              if(ctx.error)
                return do_set_error(ctx, "missing value");

              // next
              pstor = &(emplace_result.first->second.m_stor);
              goto do_pack_value_loop_;
            }
          }
          break;

        default:
          ROCKET_UNREACHABLE();
        }

      if(token[0] != frm.closure)
        return do_set_error(ctx, "missing comma");

      // close
      pstor = frm.target;
      stack.pop_back();
    }
  }

void
Value::
parse_with(Parser_Context& ctx, const ::rocket::cow_string& str, Options opts)
  {
    ::rocket::tinybuf_str buf(str, ::rocket::tinybuf::open_read);
    this->parse_with(ctx, buf, opts);
  }

void
Value::
parse_with(Parser_Context& ctx, ::std::FILE* fp, Options opts)
  {
    ::rocket::tinybuf_file buf(fp, nullptr);
    this->parse_with(ctx, buf, opts);
  }

bool
Value::
parse(::rocket::tinybuf& buf, Options opts)
  {
    Parser_Context ctx;
    this->parse_with(ctx, buf, opts);
    return !ctx.error;
  }

bool
Value::
parse(const ::rocket::cow_string& str, Options opts)
  {
    Parser_Context ctx;
    this->parse_with(ctx, str, opts);
    return !ctx.error;
  }

bool
Value::
parse(::std::FILE* fp, Options opts)
  {
    Parser_Context ctx;
    this->parse_with(ctx, fp, opts);
    return !ctx.error;
  }

void
Value::
print_to(::rocket::tinybuf& buf, Options opts) const
  {
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
        if(opts & option_json_mode) {
          // as floating-point number; inaccurate for large values
          nump.put_DD(static_cast<V_number>(pstor->as<V_integer>()));
          buf.putn(nump.data(), nump.size());
        }
        else {
          // precise; annotated
          nump.put_DI(pstor->as<V_integer>());
          buf.putn("\"$l:", 4);
          buf.putn(nump.data(), nump.size());
          buf.putc('\"');
        }
        break;

      case t_number:
        if(::std::isfinite(pstor->as<V_number>())) {
          // finite; unquoted
          nump.put_DD(pstor->as<V_number>());
          buf.putn(nump.data(), nump.size());
        }
        else if(opts & option_json_mode) {
          // invalid; nullified
          buf.putn("null", 4);
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
        if((opts & option_json_mode) || (pstor->as<V_string>()[0] != '$')) {
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
        if(opts & option_json_mode) {
          // invalid; nullified
          buf.putn("null", 4);
        }
        else {
          // annotated
          const unsigned char* bptr = pstor->as<V_binary>().data();
          const unsigned char* const eptr = bptr + pstor->as<V_binary>().size();

          bool use_hex = true;
          if(opts & option_bin_as_base64)
            use_hex = false;
          else
            use_hex = (pstor->as<V_binary>().size() <= 4)  // small
                      || ((pstor->as<V_binary>().size() % 4 == 0)
                          && (pstor->as<V_binary>().size() / 4 <= 8));

          if(use_hex) {
            // hex
            buf.putn("\"$h:", 4);

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
              char hex_word[16];
              uint64_t word;

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
              size_t nrem = static_cast<size_t>(eptr - bptr);
              char hex_word[16];
              uint64_t word = 0;

              for(uint32_t t = 0;  t != nrem;  ++t) {
                word <<= 8;
                word |= static_cast<uint64_t>(*bptr) << (64 - nrem * 8);
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
              char b64_word[4];
              uint32_t word;

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
              size_t nrem = static_cast<size_t>(eptr - bptr);
              char b64_word[4] = { 0, 0, '=', '=' };
              uint32_t word = 0;

              ::std::memcpy(&word, bptr, 2);  // use the null terminator!
              word = ROCKET_BETOH32(word);
              bptr += nrem;

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
        if(opts & option_json_mode) {
          // invalid; nullified
          buf.putn("null", 4);
        }
        else {
          // annotated
          nump.put_DI(::std::chrono::time_point_cast<::std::chrono::milliseconds>(
                                       pstor->as<V_time>()).time_since_epoch().count());
          buf.putn("\"$t:", 4);
          buf.putn(nump.data(), nump.size());
          buf.putc('\"');
        }
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
print_to(::rocket::cow_string& str, Options opts) const
  {
    ::rocket::tinybuf_str buf(::std::move(str), ::rocket::tinybuf::open_write);
    this->print_to(buf, opts);
    str = buf.extract_string();
  }

void
Value::
print_to(::std::FILE* fp, Options opts) const
  {
    ::rocket::tinybuf_file buf(fp, nullptr);
    this->print_to(buf, opts);
  }

::rocket::cow_string
Value::
print_to_string(Options opts) const
  {
    ::rocket::tinybuf_str buf(::rocket::tinybuf::open_write);
    this->print_to(buf, opts);
    return buf.extract_string();
  }

void
Value::
print_to_stderr(Options opts) const
  {
    ::rocket::tinybuf_file buf(stderr, nullptr);
    this->print_to(buf, opts);
  }

}  // namespace taxon
