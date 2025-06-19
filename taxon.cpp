// This file is part of TAXON.
// Copyleft 2024-2025, LH_Mouse. All wrongs reserved.

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
template class ::rocket::cow_hashmap<rocket::phcow_string,
  ::taxon::Value, rocket::phcow_string::hash>;
namespace taxon {
namespace {

constexpr ROCKET_ALWAYS_INLINE
bool
is_within(int c, int lo, int hi)
  {
    return (c >= lo) && (c <= hi);
  }

template<typename... Ts>
constexpr ROCKET_ALWAYS_INLINE
bool
is_any(int c, Ts... accept_set)
  {
    for(int m : { accept_set... })
      if(c == m)
        return true;
    return false;
  }

void
do_err(Parser_Context& ctx, const char* error)
  {
    ctx.c = -1;
    ctx.offset = ctx.saved_offset;
    ctx.error = error;
  }

void
do_mov(::rocket::cow_string& token, Parser_Context& ctx, ::rocket::tinybuf& buf)
  {
    // Move the current character from `ctx.c` to `token`.
    if(ROCKET_EXPECT(ctx.c <= 0x7F))
      token.push_back(static_cast<char>(ctx.c));
    else {
      // Write this multibyte character in the current locale.
      char mbs[MB_LEN_MAX];
      ::std::mbstate_t mbst = { };
      size_t mblen = ::std::c32rtomb(mbs, static_cast<char32_t>(ctx.c), &mbst);
      if(static_cast<int>(mblen) < 0)
        return do_err(ctx, "Character not representable in current locale");

      // `c32rtomb()` may return zero for a null character.
      if(ROCKET_EXPECT(mblen <= 1))
        token.push_back(mbs[0]);
      else
        token.append(mbs, mblen);
    }

    // Move the next character from `buf` to `ctx.c`. Source text must be UTF-8.
    ctx.c = buf.getc();
    if(ctx.c == -1)
      return do_err(ctx, "End of input stream");
    else if(is_within(ctx.c, 0x80, 0xBF))
      return do_err(ctx, "Invalid UTF-8 byte");

    if(ROCKET_UNEXPECT(ctx.c > 0x7F)) {
      int u8len = ROCKET_LZCNT32(static_cast<uint32_t>(~ ctx.c) << 24);
      ctx.c &= 1 << (7 - u8len);

      for(int k = 1;  k < u8len;  ++k) {
        int next = buf.getc();
        if(!is_within(next, 0x80, 0xBF))
          return do_err(ctx, "Invalid UTF-8 sequence");
        else {
          ctx.c <<= 6;
          ctx.c |= next & 0x3F;
        }
      }

      if((ctx.c < 0x80)  // overlong
          || (ctx.c < (1 << (u8len * 5 - 4)))  // overlong
          || is_within(ctx.c, 0xD800, 0xDFFF)  // surrogates
          || (ctx.c > 0x10FFFF))
        return do_err(ctx, "Invalid UTF character");
    }
  }

void
do_token(::rocket::cow_string& token, Parser_Context& ctx, ::rocket::tinybuf& buf)
  {
    // Clear the current token and skip whitespace.
    ctx.saved_offset = buf.tell();
    ctx.error = nullptr;

    if(ctx.c == -1)
      do_mov(token, ctx, buf);
    while(is_any(ctx.c, '\t', '\r', '\n', ' '))
      do_mov(token, ctx, buf);
    token.clear();
    if(ctx.c == -1)
      return;

    switch(ctx.c)
      {
      case '[':
      case ']':
      case '{':
      case '}':
      case ':':
      case ',':
        // Take each of these characters as a single token; do not attempt to get
        // the next character, as some of these tokens may terminate the input,
        // and the stream may be blocking but we can't really know whether there
        // are more data.
        token.push_back(static_cast<char>(ctx.c));
        ctx.c = -1;
        break;

      case '0' ... '9':
      case '+':
      case '-':
        // Take a floating-point number. Strictly, JSON doesn't allow plus signs
        // or leading zeroes, but we accept them as extensions.
        do
          do_mov(token, ctx, buf);
        while(is_within(ctx.c, '0', '9'));

        if(ctx.c == '.') {
          do_mov(token, ctx, buf);
          if(!is_within(ctx.c, '0', '9'))
            return do_err(ctx, "Invalid number");
          else {
            do
              do_mov(token, ctx, buf);
            while(is_within(ctx.c, '0', '9'));
          }
        }

        if(is_any(ctx.c, 'e', 'E')) {
          do_mov(token, ctx, buf);
          if(is_any(ctx.c, '+', '-'))
            do_mov(token, ctx, buf);
          if(!is_within(ctx.c, '0', '9'))
            return do_err(ctx, "Invalid number");
          else {
            do
              do_mov(token, ctx, buf);
            while(is_within(ctx.c, '0', '9'));
          }
        }

        // If the end of input has been reached, `ctx.error` may be set. We will
        // not return an error, so clear it.
        ROCKET_ASSERT(!token.empty());
        ctx.error = nullptr;
        break;

      case 'A' ... 'Z':
      case 'a' ... 'z':
      case '_':
      case '$':
        // Take an identifier. As in JavaScript, we accept dollar signs in
        // identifiers as an extension.
        do
          do_mov(token, ctx, buf);
        while(is_any(ctx.c, '_', '$') || is_within(ctx.c, 'A', 'Z')
              || is_within(ctx.c, 'a', 'z') || is_within(ctx.c, '0', '9'));

        // If the end of input has been reached, `ctx.error` may be set. We will
        // not return an error, so clear it.
        ROCKET_ASSERT(!token.empty());
        ctx.error = nullptr;
        break;

      case '"':
        // Take a double-quoted string. When stored in `token`, it shall start
        // with a double-quote character, followed by the decoded string. No
        // terminating double-quote character is appended.
        do_mov(token, ctx, buf);
        while(ctx.c != '"')
          if(ctx.c == -1)
            return do_err(ctx, "String not terminated properly");
          else if((ctx.c <= 0x1F) || (ctx.c == 0x7F))
            return do_err(ctx, "Control character not allowed in string");
          else if(ctx.c != '\\')
            do_mov(token, ctx, buf);
          else {
            // Read an escape sequence.
            int next = buf.getc();
            if(next == -1)
              return do_err(ctx, "Incomplete escape sequence");
            else if(is_any(next, '\\', '\"', '/'))
              ctx.c = next;
            else if(next == 'b')
              ctx.c = '\b';
            else if(next == 'f')
              ctx.c = '\f';
            else if(next == 'n')
              ctx.c = '\n';
            else if(next == 'r')
              ctx.c = '\r';
            else if(next == 't')
              ctx.c = '\t';
            else if(next == 'u') {
              // Read the first UTF-16 code unit.
              char temp[16] = "0x";
              if(buf.getn(temp + 2, 4) != 4)
                return do_err(ctx, "Invalid escape sequence");

              ::rocket::ascii_numget numg;
              if(numg.parse_XU(temp, 6) != 6)
                return do_err(ctx, "Invalid hexadecimal digit");

              uint64_t high;
              numg.cast_U(high, 0, UINT64_MAX);
              ctx.c = static_cast<int>(high);
              if(is_within(ctx.c, 0xDC00, 0xDFFF))
                return do_err(ctx, "Dangling UTF-16 trailing surrogate");

              if(is_within(ctx.c, 0xD800, 0xDBFF)) {
                // Look for a trailing surrogate.
                if(buf.getn(temp, 6) != 6)
                  return do_err(ctx, "Missing UTF-16 trailing surrogate");

                if(::std::memcmp(temp, "\\u", 2) != 0)
                  return do_err(ctx, "Missing UTF-16 trailing surrogate");

                ::std::memcpy(temp, "0x", 2);
                if(numg.parse_XU(temp, 6) != 6)
                  return do_err(ctx, "Invalid hexadecimal digit");

                uint64_t low;
                numg.cast_U(low, 0, UINT64_MAX);
                ctx.c = static_cast<int>(low);
                if(!is_within(ctx.c, 0xDC00, 0xDFFF))
                  return do_err(ctx, "Missing UTF-16 trailing surrogate");

                ctx.c &= 0x3FF;
                ctx.c |= static_cast<int>(high << 10 & 0xFFC00);
                ctx.c += 0x10000;
              }
            }

            // Move the unescaped character into the token.
            do_mov(token, ctx, buf);
          }

        // Drop the terminating quotation mark for simplicity; do not attempt to
        // get the next character, as the stream may be blocking but we can't
        // really know whether there are more data.
        ROCKET_ASSERT(!token.empty());
        ctx.c = -1;
        ctx.error = nullptr;
        break;

      default:
        return do_err(ctx, "Invalid character");
      }
  }

void
do_escape_string_in_utf8(::rocket::tinybuf& buf, const ::rocket::cow_string& str)
  {
    char temp[16] = "\\";
    auto bptr = str.c_str();
    const auto eptr = str.c_str() + str.length();
    while(bptr != eptr) {
      temp[1] = *bptr;
      int ch = static_cast<uint8_t>(*bptr);
      bptr ++;

      if(is_any(ch, '\\', '\"', '/'))
        buf.putn(temp, 2);
      else if(is_within(ch, 0x20, 0x7E))
        buf.putc(temp[1]);
      else if(ch == '\b')
        buf.putn("\\b", 2);
      else if(ch == '\f')
        buf.putn("\\f", 2);
      else if(ch == '\n')
        buf.putn("\\n", 2);
      else if(ch == '\r')
        buf.putn("\\r", 2);
      else if(ch == '\t')
        buf.putn("\\t", 2);
      else {
        // Convert this character to UTF-16.
        char16_t c16;
        ::std::mbstate_t mbst = { };
        size_t mblen = ::std::mbrtoc16(&c16, bptr - 1, static_cast<size_t>(eptr - bptr) + 1, &mbst);
        if(mblen == 0) {
          // A null byte has been consumed and stored into `c16`.
          buf.putn("\\u0000", 6);
        }
        else if(static_cast<int>(mblen) < 0) {
          // The input string is invalid. Consume one byte anyway, but print a
          // replacement character.
          buf.putn("\\uFFFD", 6);
        }
        else {
          // `mblen` bytes have been consumed, converted and stored into `c16`.
          ::rocket::ascii_numput nump;
          nump.put_XU(c16, 4);
          temp[1] = 'u';
          ::std::memcpy(temp + 2, nump.data() + 2, 4);
          size_t ntemp = 6;

          if(static_cast<int>(::std::mbrtoc16(&c16, nullptr, 0, &mbst)) == -3) {
            // No input has been consumed, but a trailing surrogate has been
            // stored into `c16`.
            nump.put_XU(c16, 4);
            ::std::memcpy(temp + 6, "\\u", 2);
            ::std::memcpy(temp + 8, nump.data() + 2, 4);
            ntemp += 6;
          }

          // Write the escape sequence.
          buf.putn(temp, ntemp);
          bptr += mblen - 1;
        }
      }
    }
  }

}  // namespace

// We assume that a all-bit-zero struct represents the `null` value.
// This is effectively undefined behavior. Don't play with this at home!
alignas(Value) const char null_storage[sizeof(Value)] = { };

Value::
~Value()
  {
    // Break deep recursion with a handwritten stack.
    struct xVariant : variant_type  { };
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
      ::rocket::construct(&(this->m_stor), static_cast<variant_type&&>(stack.mut_back()));
      stack.pop_back();
      goto do_unpack_loop_;
    }
  }

void
Value::
parse_with(Parser_Context& ctx, ::rocket::tinybuf& buf, Options opts)
  {
    // Initialize parser state.
    ctx.c = -1;
    ctx.offset = -1;
    ctx.saved_offset = 0;
    ctx.error = nullptr;

    // Break deep recursion with a handwritten stack.
    struct xFrame
      {
        variant_type* target;
        V_array* psa;
        V_object* pso;
      };

    ::rocket::cow_vector<xFrame> stack;
    ::rocket::cow_string token;
    ::rocket::ascii_numget numg;
    variant_type* pstor = &(this->m_stor);

    do_token(token, ctx, buf);
    if(ctx.error)
      return;

  do_pack_value_loop_:
    if(!(opts & option_bypass_nesting_limit) && (stack.size() > 32))
      return do_err(ctx, "Nesting limit exceeded");

    if(token[0] == '[') {
      // array
      do_token(token, ctx, buf);
      if(token.empty())
        return do_err(ctx, "Array not terminated properly");
      else if(ctx.error)
        return;

      if(token[0] != ']') {
        // open
        auto& frm = stack.emplace_back();
        frm.target = pstor;
        frm.psa = &(pstor->emplace<V_array>());
        pstor = &(frm.psa->emplace_back().m_stor);
        goto do_pack_value_loop_;
      }

      // empty
      pstor->emplace<V_array>();
    }
    else if(token[0] == '{') {
      // object
      do_token(token, ctx, buf);
      if(token.empty())
        return do_err(ctx, "Object not terminated properly");
      else if(ctx.error)
        return;

      if(token[0] != '}') {
        // We are inside an object, so this token must be a key string, followed
        // by a colon, followed by its value.
        if(token[0] != '"')
          return do_err(ctx, "Missing key string");

        rocket::phcow_string key;
        key.assign(token.data() + 1, token.size() - 1);

        do_token(token, ctx, buf);
        if(token != ":")
          return do_err(ctx, "Missing colon");

        do_token(token, ctx, buf);
        if(token.empty())
          return do_err(ctx, "Missing value");
        else if(ctx.error)
          return;

        // open
        auto& frm = stack.emplace_back();
        frm.target = pstor;
        frm.pso = &(pstor->emplace<V_object>());
        auto emplace_result = frm.pso->try_emplace(::std::move(key), nullptr);
        ROCKET_ASSERT(emplace_result.second);
        pstor = &(emplace_result.first->second.m_stor);
        goto do_pack_value_loop_;
      }

      // empty
      pstor->emplace<V_object>();
    }
    else if(is_any(token[0], '+', '-') || is_within(token[0], '0', '9')) {
      // number
      numg.parse_DD(token.data(), token.size());
      numg.cast_D(pstor->emplace<V_number>(), -DBL_MAX, DBL_MAX);
      if(numg.overflowed())
        return do_err(ctx, "Number value out of range");
    }
    else if(token[0] == '\"') {
      // string
      if((opts & option_json_mode) || (token[1] != '$')) {
        // plain
        pstor->emplace<V_string>(token.data() + 1, token.size() - 1);
      }
      else if(token.starts_with("\"$l:")) {
        // 64-bit integer
        if(numg.parse_I(token.data() + 4, token.size() - 4) != token.size() - 4)
          return do_err(ctx, "Invalid 64-bit integer");

        numg.cast_I(pstor->emplace<V_integer>(), INT64_MIN, INT64_MAX);
        if(numg.overflowed())
          return do_err(ctx, "64-bit integer value out of range");
      }
      else if(token.starts_with("\"$d:")) {
        // double-precision number
        if(numg.parse_D(token.data() + 4, token.size() - 4) != token.size() - 4)
          return do_err(ctx, "Invalid double-precision number");

        // Values that are out of range are converted to infinities and are
        // always accepted.
        numg.cast_D(pstor->emplace<V_number>(), -HUGE_VAL, HUGE_VAL);
      }
      else if(token.starts_with("\"$s:")) {
        // annotated string
        pstor->emplace<V_string>(token.data() + 4, token.size() - 4);
      }
      else if(token.starts_with("\"$t:")) {
        // timestamp in milliseconds
        if(numg.parse_I(token.data() + 4, token.size() - 4) != token.size() - 4)
          return do_err(ctx, "Invalid timestamp");

        // The allowed timestamp values are from '1900-01-01T00:00:00.000Z' to
        // '9999-12-31T23:59:59.999Z'.
        int64_t count;
        numg.cast_I(count, -2208988800000, 253402300799999);
        pstor->emplace<V_time>(::std::chrono::milliseconds(count));
        if(numg.overflowed())
          return do_err(ctx, "Timestamp value out of range");
      }
      else if(token.starts_with("\"$h:")) {
        // hex-encoded data
        size_t units = (token.size() - 4) / 2;
        if(units * 2 != token.size() - 4)
          return do_err(ctx, "Invalid hex string");

        auto& bin = pstor->emplace<V_binary>();
        bin.reserve(units);

        auto bptr = token.data() + 4;
        const auto eptr = token.data() + token.size();
        while(bptr != eptr) {
          uint32_t value = 0;
          for(int k = 0;  k != 2;  ++k) {
            value <<= 4;
            int c = static_cast<uint8_t>(bptr[k]);
            if(is_within(c, '0', '9'))
              value |= static_cast<uint32_t>(c - '0');
            else if(is_within(c, 'A', 'F'))
              value |= static_cast<uint32_t>(c - 'A' + 10);
            else if(is_within(c, 'a', 'f'))
              value |= static_cast<uint32_t>(c - 'a' + 10);
            else
              return do_err(ctx, "Invalid hex digit");
          }

          bin.push_back(static_cast<uint8_t>(value));
          bptr += 2;
        }
      }
      else if(token.starts_with("\"$b:")) {
        // base64-encoded data
        size_t units = (token.size() - 4) / 4;
        if(units * 4 != token.size() - 4)
          return do_err(ctx, "Invalid base64 string");

        auto& bin = pstor->emplace<V_binary>();
        bin.reserve(units);

        auto bptr = token.data() + 4;
        const auto eptr = token.data() + token.size();
        while(bptr != eptr) {
          uint32_t value = 0;
          uint32_t out_bytes = 3;
          for(int k = 0;  k != 4;  ++k) {
            value <<= 6;
            int c = static_cast<uint8_t>(bptr[k]);
            if(is_within(c, 'A', 'Z'))
              value |= static_cast<uint32_t>(c - 'A');
            else if(is_within(c, 'a', 'z'))
              value |= static_cast<uint32_t>(c - 'a' + 26);
            else if(is_within(c, '0', '9'))
              value |= static_cast<uint32_t>(c - '0' + 52);
            else if(c == '+')
              value |= 62;
            else if(c == '/')
              value |= 63;
            else if(c == '=') {
              if(k >= 2)
                out_bytes --;
              else
                return do_err(ctx, "Invalid base64 string");
            }
            else
              return do_err(ctx, "Invalid base64 digit");
          }

          uint32_t temp = ROCKET_HTOBE32(value << 8);
          bin.append(reinterpret_cast<uint8_t*>(&temp), out_bytes);
          bptr += 4;
        }
      }
      else
       return do_err(ctx, "Unknown type annotator");
    }
    else if(token == "null")
      pstor->emplace<V_null>();
    else if(token == "true")
      pstor->emplace<V_boolean>(true);
    else if(token == "false")
      pstor->emplace<V_boolean>(false);
    else
      return do_err(ctx, "Invalid token");

    while(!stack.empty()) {
      auto& frm = stack.back();
      if(frm.psa) {
        // array
        do_token(token, ctx, buf);
        if(token.empty())
          return do_err(ctx, "Array not terminated properly");
        else if(ctx.error)
          return;

        if(token[0] == ',') {
          do_token(token, ctx, buf);
          if(token.empty())
            return do_err(ctx, "Missing value");
          else if(ctx.error)
            return;

          // next
          pstor = &(frm.psa->emplace_back().m_stor);
          goto do_pack_value_loop_;
        }

        if(token[0] != ']')
          return do_err(ctx, "Missing comma or closed bracket");
      }
      else {
        // object
        do_token(token, ctx, buf);
        if(token.empty())
          return do_err(ctx, "Object not terminated properly");
        else if(ctx.error)
          return;

        if(token[0] == ',') {
          do_token(token, ctx, buf);
          if(token.empty())
            return do_err(ctx, "Missing key string");
          else if(ctx.error)
            return;

          if(token[0] != '"')
            return do_err(ctx, "Missing key string");

          rocket::phcow_string key;
          key.assign(token.data() + 1, token.size() - 1);
          auto result = frm.pso->try_emplace(::std::move(key), nullptr);
          if(!result.second)
            return do_err(ctx, "Duplicate key string");

          do_token(token, ctx, buf);
          if(token != ":")
            return do_err(ctx, "Missing colon");

          do_token(token, ctx, buf);
          if(ctx.error)
            return do_err(ctx, "Missing value");

          // next
          pstor = &(result.first->second.m_stor);
          goto do_pack_value_loop_;
        }

        if(token[0] != '}')
          return do_err(ctx, "Missing comma or closed brace");
      }

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
    const variant_type* pstor = &(this->m_stor);

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
          do_escape_string_in_utf8(buf, frm.ito->first.rdstr());
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
          do_escape_string_in_utf8(buf, pstor->as<V_string>());
          buf.putc('\"');
        }
        else {
          // starts with `$`; annotated
          buf.putn("\"$s:", 4);
          do_escape_string_in_utf8(buf, pstor->as<V_string>());
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
          auto bptr = pstor->as<V_binary>().data();
          const auto eptr = bptr + pstor->as<V_binary>().size();

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
                if(b < 10)
                  return static_cast<char>('0' + b);
                else
                  return static_cast<char>('a' + b - 10);
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
                if(b < 26)
                  return static_cast<char>('A' + b);
                else if(b < 52)
                  return static_cast<char>('a' + b - 26);
                else if(b < 62)
                  return static_cast<char>('0' + b - 52);
                else if(b < 63)
                  return '+';
                else
                  return '/';
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
            do_escape_string_in_utf8(buf, frm.ito->first.rdstr());
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
