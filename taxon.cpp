// This file is part of TAXON.
// Copyleft 2024-2025, LH_Mouse. All wrongs reserved.

#define TAXON_DETAILS_DB168D30_B229_44D5_8C4C_7B3C52C686DD_
#include "taxon.hpp"
#include <rocket/tinybuf.hpp>
#include <rocket/ascii_numput.hpp>
#include <rocket/ascii_numget.hpp>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdio>
#include <climits>
#include <cfloat>
#include <cuchar>
#if defined __SSE2__
#include <x86intrin.h>
#include <xmmintrin.h>
#endif
#if defined __ARM_NEON
#include <arm_neon.h>
#endif
template class ::rocket::variant<TAXON_TYPES_IEZUVAH3_(::taxon::V)>;
template class ::rocket::cow_vector<::taxon::Value>;
template class ::rocket::cow_hashmap<::rocket::phcow_string,
    ::taxon::Value, ::rocket::phcow_string::hash>;
namespace taxon {
namespace {

using variant_type = ::rocket::variant<TAXON_TYPES_IEZUVAH3_(V)>;
using bytes_type = ::std::aligned_storage<sizeof(variant_type), sizeof(void*)>::type;

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
    return (... || (c == accept_set));
  }

ROCKET_ALWAYS_INLINE
void
do_err(Parser_Context& ctx, const char* error)
  {
    if(ctx.error)
      return;

    ctx.c = -1;
    ctx.offset = ctx.saved_offset;
    ctx.error = error;
  }

struct Memory_Source
  {
    const char* bptr;
    const char* sptr;
    const char* eptr;

    constexpr Memory_Source() noexcept
      : bptr(), sptr(), eptr()  { }

    constexpr Memory_Source(const char* s, size_t n) noexcept
      : bptr(s), sptr(s), eptr(s + n)  { }

    int
    getc() noexcept
      {
        int r = -1;
        if(this->sptr != this->eptr) {
          r = static_cast<unsigned char>(*(this->sptr));
          this->sptr ++;
        }
        return r;
      }

    size_t
    getn(char* s, size_t n) noexcept
      {
        size_t r = ::std::min(static_cast<size_t>(this->eptr - this->sptr), n);
        if(r != 0) {
          ::memcpy(s, this->sptr, r);
          this->sptr += r;
        }
        return r;
      }

    int64_t
    tell() const noexcept
      {
        return this->sptr - this->bptr;
      }
  };

struct Unified_Source
  {
    Memory_Source* mem = nullptr;
    ::rocket::tinybuf* buf = nullptr;
    ::std::FILE* fp = nullptr;

    Unified_Source(Memory_Source* m) : mem(m)  { }
    Unified_Source(::rocket::tinybuf* b) : buf(b)  { }
    Unified_Source(::std::FILE* f) : fp(f)  { }

    int
    getc() const
      {
        if(this->mem)
          return this->mem->getc();
        else if(this->buf)
          return this->buf->getc();
        else if(this->fp)
          return ::fgetc(this->fp);
        else
          ROCKET_UNREACHABLE();
      }

    size_t
    getn(char* s, size_t n) const
      {
        if(this->mem)
          return this->mem->getn(s, n);
        else if(this->buf)
          return this->buf->getn(s, n);
        else if(this->fp)
          return ::fread(s, 1, n, this->fp);
        else
          ROCKET_UNREACHABLE();
      }

    int64_t
    tell() const
      {
        if(this->mem)
          return this->mem->tell();
        else if(this->buf)
          return this->buf->tell();
        else if(this->fp)
          return ::ftello(this->fp);
        else
          ROCKET_UNREACHABLE();
      }
  };

struct Unified_Sink
  {
    ::rocket::cow_string* str = nullptr;
    ::rocket::tinybuf* buf = nullptr;
    ::std::FILE* fp = nullptr;

    Unified_Sink(::rocket::cow_string* s) : str(s)  { }
    Unified_Sink(::rocket::tinybuf* b) : buf(b)  { }
    Unified_Sink(::std::FILE* f) : fp(f)  { }

    void
    putc(char c) const
      {
        if(this->str)
          this->str->push_back(c);
        else if(this->buf)
          this->buf->putc(c);
        else if(this->fp)
          ::fputc(c, this->fp);
        else
          ROCKET_UNREACHABLE();
      }

    void
    putn(const char* s, size_t n) const
      {
        if(this->str)
          this->str->append(s, n);
        else if(this->buf)
          this->buf->putn(s, n);
        else if(this->fp)
          ::fwrite(s, 1, n, this->fp);
        else
          ROCKET_UNREACHABLE();
      }
  };

struct String_Pool
  {
    ::std::vector<::rocket::phcow_string> st;

    struct hash_less
      {
        bool
        operator()(const ::rocket::phcow_string& x, const ::rocket::phcow_string& y) const noexcept
          { return x.rdhash() < y.rdhash();  }

        bool
        operator()(const ::rocket::phcow_string& x, size_t y) const noexcept
          { return x.rdhash() < y;  }

        bool
        operator()(size_t x, const ::rocket::phcow_string& y) const noexcept
          { return x < y.rdhash();  }
      };

    const ::rocket::phcow_string&
    intern(const char* str, size_t len)
      {
        size_t hval = ::rocket::cow_string::hash()(str, len);
        auto range = ::std::equal_range(this->st.begin(), this->st.end(), hval, hash_less());

        // String already exists?
        for(auto it = range.first;  it != range.second;  ++it)
          if(it->rdstr().equals(str, len))
            return *it;

        // No. Allocate a new one, while keeping the pool sorted.
        auto it = this->st.insert(range.second, ::rocket::cow_string(str, len));
        ROCKET_ASSERT(it->rdhash() == hval);
        return *it;
      }
  };

void
do_load_next(Parser_Context& ctx, Unified_Source usrc)
  {
    ctx.c = usrc.getc();
    if(ctx.c < 0) {
      ctx.eof = true;
      return do_err(ctx, nullptr);
    }

    if(is_within(ctx.c, 0x80, 0xBF))
      return do_err(ctx, "Invalid UTF-8 byte");
    else if(ROCKET_UNEXPECT(ctx.c > 0x7F)) {
      // Parse a multibyte Unicode character.
      int u8len = ROCKET_LZCNT32(static_cast<uint32_t>(ctx.c ^ -1) << 24);
      ctx.c &= (1 << (7 - u8len)) - 1;
      for(int k = 1;  k < u8len;  ++k) {
        int next = usrc.getc();
        if(next < 0) {
          ctx.eof = true;
          return do_err(ctx, "Incomplete UTF-8 sequence");
        }

        if(!is_within(next, 0x80, 0xBF))
          return do_err(ctx, "Invalid UTF-8 sequence");

        ctx.c <<= 6;
        ctx.c |= next & 0x3F;
      }

      if((ctx.c < 0x80)  // overlong
          || (ctx.c < (1 << (u8len * 5 - 4)))  // overlong
          || is_within(ctx.c, 0xD800, 0xDFFF)  // surrogates
          || (ctx.c > 0x10FFFF))
        return do_err(ctx, "Invalid UTF character");
    }
  }

void
do_mov(::rocket::cow_string& token, Parser_Context& ctx, Unified_Source usrc)
  {
    char mbs[MB_LEN_MAX];
    size_t mblen = 1;

    mbs[0] = static_cast<char>(ctx.c);
    if(ROCKET_UNEXPECT(ctx.c > 0x7F)) {
      // Write this multibyte character in the current locale.
      ::std::mbstate_t mbst = { };
      mblen = ::std::c32rtomb(mbs, static_cast<char32_t>(ctx.c), &mbst);
      if(static_cast<int>(mblen) < 0)
        return do_err(ctx, "Character not representable in current locale");
    }

    // Always write one character, since `c32rtomb()` may return zero for a
    // null character.
    token.push_back(mbs[0]);
    if(ROCKET_UNEXPECT(mblen > 1))
      token.append(mbs + 1, mblen - 1);

    // If the token is an incomplete string, then try loading some characters
    // that are known to require no escaping.
    if(ROCKET_UNEXPECT(token[0] == '\"')) {
      if(usrc.mem) {
        const char* tptr = usrc.mem->sptr;
#if defined __SSE2__
        while(usrc.mem->eptr - tptr >= 16) {
          __m128i t = _mm_loadu_si128(reinterpret_cast<const __m128i*>(tptr));
          t = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(t, _mm_set1_epi8('\\')),
                                        _mm_cmpeq_epi8(t, _mm_set1_epi8('\"'))),
                           _mm_or_si128(_mm_cmplt_epi8(t, _mm_set1_epi8(0x20)),
                                        _mm_cmplt_epi8(_mm_set1_epi8(0x7E), t)));
          int mask = _mm_movemask_epi8(t);
          if(mask != 0) {
            tptr += __builtin_ctz(static_cast<uint32_t>(mask));
            break;
          }
          tptr += 16;
        }
#elif defined __ARM_NEON
        while(usrc.mem->eptr - tptr >= 16) {
          uint8x16_t t = vld1q_u8(reinterpret_cast<const uint8_t*>(tptr));
          t = vorrq_u8(vorrq_u8(vceqq_u8(t, vdupq_n_u8('\\')),
                                vceqq_u8(t, vdupq_n_u8('\"'))),
                       vorrq_u8(vcltq_u8(t, vdupq_n_u8(0x20)),
                                vcltq_u8(vdupq_n_u8(0x7E), t)));
          uint8x8_t vmask = vshrn_n_u16(vreinterpretq_u16_u8(t), 4);
          uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(vmask), 0);
          if(mask != 0) {
            tptr += __builtin_ctzll(mask) >> 2;
            break;
          }
          tptr += 16;
        }
#else
        while(usrc.mem->eptr != tptr) {
          if(is_any(*tptr, '\\', '\"') || !is_within(*tptr, 0x20, 0x7E))
            break;
          ++ tptr;
        }
#endif
        token.append(usrc.mem->sptr, tptr);
        usrc.mem->sptr = tptr;
      }
      else if(usrc.fp) {
        char temp[256];
        int len;
        while(::fscanf(usrc.fp, "%256[]-~ !#-[]%n", temp, &len) == 1) {
          token.append(temp, static_cast<unsigned>(len));
          if(len < 256)
            break;
        }
      }
    }

    do_load_next(ctx, usrc);
  }

ROCKET_FLATTEN
void
do_token(::rocket::cow_string& token, Parser_Context& ctx, Unified_Source usrc)
  {
    // Clear the current token and skip whitespace.
    ctx.saved_offset = usrc.tell();
    ctx.error = nullptr;
    token.clear();

    while(is_any(ctx.c, -1, ' ', '\t', '\r', '\n')) {
      if(usrc.mem) {
        const char* tptr = usrc.mem->sptr;
#if defined __SSE2__
        while(usrc.mem->eptr - tptr >= 16) {
          __m128i t = _mm_loadu_si128(reinterpret_cast<const __m128i*>(tptr));
          t = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(t, _mm_set1_epi8(' ')),
                                        _mm_cmpeq_epi8(t, _mm_set1_epi8('\t'))),
                           _mm_or_si128(_mm_cmpeq_epi8(t, _mm_set1_epi8('\r')),
                                        _mm_cmpeq_epi8(t, _mm_set1_epi8('\n'))));
          int mask = 0xFFFF ^ _mm_movemask_epi8(t);
          if(mask != 0) {
            tptr += __builtin_ctz(static_cast<uint32_t>(mask));
            break;
          }
          tptr += 16;
        }
#elif defined __ARM_NEON
        while(usrc.mem->eptr - tptr >= 16) {
          uint8x16_t t = vld1q_u8(reinterpret_cast<const uint8_t*>(tptr));
          t = vorrq_u8(vorrq_u8(vceqq_u8(t, vdupq_n_u8(' ')),
                                vceqq_u8(t, vdupq_n_u8('\t'))),
                       vorrq_u8(vceqq_u8(t, vdupq_n_u8('\r')),
                                vceqq_u8(t, vdupq_n_u8('\n'))));
          uint8x8_t vmask = vshrn_n_u16(vreinterpretq_u16_u8(t), 4);
          uint64_t mask = UINT64_MAX ^ vget_lane_u64(vreinterpret_u64_u8(vmask), 0);
          if(mask != 0) {
            tptr += __builtin_ctzll(mask) >> 2;
            break;
          }
          tptr += 16;
        }
#else
        while(usrc.mem->eptr != tptr) {
          if(!is_any(*tptr, ' ', '\t', '\r', '\n'))
            break;
          ++ tptr;
        }
#endif
        usrc.mem->sptr = tptr;
      }
      else if(usrc.fp) {
        (void)! ::fscanf(usrc.fp, "%*[ \t\r\n]");
      }

      do_load_next(ctx, usrc);
      if(ctx.c < 0)
        return;
    }

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
          do_mov(token, ctx, usrc);
        while(is_within(ctx.c, '0', '9'));

        if(ctx.c == '.') {
          do_mov(token, ctx, usrc);
          if(!is_within(ctx.c, '0', '9'))
            return do_err(ctx, "Invalid number");
          else {
            do
              do_mov(token, ctx, usrc);
            while(is_within(ctx.c, '0', '9'));
          }
        }

        if(is_any(ctx.c, 'e', 'E')) {
          do_mov(token, ctx, usrc);
          if(is_any(ctx.c, '+', '-'))
            do_mov(token, ctx, usrc);
          if(!is_within(ctx.c, '0', '9'))
            return do_err(ctx, "Invalid number");
          else {
            do
              do_mov(token, ctx, usrc);
            while(is_within(ctx.c, '0', '9'));
          }
        }

        // If the end of input has been reached, `ctx.error` may be set. We will
        // not return an error, so clear it.
        ROCKET_ASSERT(token.size() != 0);
        ctx.error = nullptr;
        ctx.eof = false;
        break;

      case 'A' ... 'Z':
      case 'a' ... 'z':
      case '_':
      case '$':
        // Take an identifier. As in JavaScript, we accept dollar signs in
        // identifiers as an extension.
        do
          do_mov(token, ctx, usrc);
        while(is_any(ctx.c, '_', '$') || is_within(ctx.c, 'A', 'Z')
              || is_within(ctx.c, 'a', 'z') || is_within(ctx.c, '0', '9'));

        // If the end of input has been reached, `ctx.error` may be set. We will
        // not return an error, so clear it.
        ROCKET_ASSERT(token.size() != 0);
        ctx.error = nullptr;
        ctx.eof = false;
        break;

      case '\"':
        // Take a double-quoted string. When stored in `token`, it shall start
        // with a double-quote character, followed by the decoded string. No
        // terminating double-quote character is appended.
        do_mov(token, ctx, usrc);
        while(ctx.c != '\"')
          if(ctx.eof)
            return do_err(ctx, "String not terminated properly");
          else if((ctx.c <= 0x1F) || (ctx.c == 0x7F))
            return do_err(ctx, "Control character not allowed in string");
          else {
            if(ROCKET_UNEXPECT(ctx.c == '\\')) {
              // Read an escape sequence.
              int next = usrc.getc();
              if(next < 0) {
                ctx.eof = true;
                return do_err(ctx, "Incomplete escape sequence");
              }

              if(is_any(next, '\\', '\"', '/'))
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
                if(usrc.getn(temp + 2, 4) != 4)
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
                  if(usrc.getn(temp, 6) != 6)
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
                  ctx.c |= static_cast<int>(high & 0x3FF) << 10;
                  ctx.c += 0x10000;
                }
              }
              else
                return do_err(ctx, "Invalid escape sequence");
            }

            // Move the unescaped character into the token.
            do_mov(token, ctx, usrc);
          }

        // Drop the terminating quotation mark for simplicity; do not attempt to
        // get the next character, as the stream may be blocking but we can't
        // really know whether there are more data.
        ROCKET_ASSERT(token.size() != 0);
        ctx.error = nullptr;
        ctx.c = -1;
        break;

      default:
        return do_err(ctx, "Invalid character");
      }
  }

void
do_parse_with(variant_type& root, Parser_Context& ctx, Unified_Source usrc, Options opts)
  {
    // Initialize parser state.
    ::std::memset(&ctx, 0, sizeof(ctx));
    ctx.c = -1;

    // Break deep recursion with a handwritten stack.
    struct xFrame
      {
        variant_type* target;
        V_array* psa;
        V_object* pso;
      };

    ::std::vector<xFrame> stack;
    ::rocket::cow_string token;
    ::rocket::ascii_numget numg;
    String_Pool key_pool;
    variant_type* pstor = &root;

    do_token(token, ctx, usrc);
    if(ctx.error)
      return;

  do_pack_value_loop_:
    if(!(opts & option_bypass_nesting_limit) && (stack.size() > 32))
      return do_err(ctx, "Nesting limit exceeded");

    if(token[0] == '[') {
      // array
      do_token(token, ctx, usrc);
      if(ctx.eof)
        return do_err(ctx, "Array not terminated properly");
      else if(ctx.error)
        return;

      if(token[0] != ']') {
        // open
        auto& frm = stack.emplace_back();
        frm.target = pstor;
        frm.psa = &(pstor->emplace<V_array>());

        // first
        pstor = &(frm.psa->emplace_back().mf_stor());
        goto do_pack_value_loop_;
      }

      // empty
      pstor->emplace<V_array>();
    }
    else if(token[0] == '{') {
      // object
      do_token(token, ctx, usrc);
      if(ctx.eof)
        return do_err(ctx, "Object not terminated properly");
      else if(ctx.error)
        return;

      if(token[0] != '}') {
        // open
        auto& frm = stack.emplace_back();
        frm.target = pstor;
        frm.pso = &(pstor->emplace<V_object>());

        // We are inside an object, so this token must be a key string, followed
        // by a colon, followed by its value.
        if(token[0] != '\"')
          return do_err(ctx, "Missing key string");

        auto emr = frm.pso->try_emplace(key_pool.intern(token.data() + 1, token.size() - 1));
        ROCKET_ASSERT(emr.second);

        do_token(token, ctx, usrc);
        if(token[0] != ':')
          return do_err(ctx, "Missing colon");

        do_token(token, ctx, usrc);
        if(ctx.eof)
          return do_err(ctx, "Missing value");
        else if(ctx.error)
          return;

        // first
        pstor = &(emr.first->second.mf_stor());
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
      else if((token[2] == 'l') && (token[3] == ':')) {
        // 64-bit integer
        if(numg.parse_I(token.data() + 4, token.size() - 4) != token.size() - 4)
          return do_err(ctx, "Invalid 64-bit integer");

        numg.cast_I(pstor->emplace<V_integer>(), INT64_MIN, INT64_MAX);
        if(numg.overflowed())
          return do_err(ctx, "64-bit integer value out of range");
      }
      else if((token[2] == 'd') && (token[3] == ':')) {
        // double-precision number
        if(numg.parse_D(token.data() + 4, token.size() - 4) != token.size() - 4)
          return do_err(ctx, "Invalid double-precision number");

        // Values that are out of range are converted to infinities and are
        // always accepted.
        numg.cast_D(pstor->emplace<V_number>(), -HUGE_VAL, HUGE_VAL);
      }
      else if((token[2] == 's') && (token[3] == ':')) {
        // annotated string
        pstor->emplace<V_string>(token.data() + 4, token.size() - 4);
      }
      else if((token[2] == 't') && (token[3] == ':')) {
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
      else if((token[2] == 'h') && (token[3] == ':')) {
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
      else if((token[2] == 'b') && (token[3] == ':')) {
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
      const auto& frm = stack.back();
      if(frm.psa) {
        // array
        do_token(token, ctx, usrc);
        if(ctx.eof)
          return do_err(ctx, "Array not terminated properly");
        else if(ctx.error)
          return;

        if(token[0] == ',') {
          do_token(token, ctx, usrc);
          if(ctx.eof)
            return do_err(ctx, "Missing value");
          else if(ctx.error)
            return;

          // next
          pstor = &(frm.psa->emplace_back().mf_stor());
          goto do_pack_value_loop_;
        }

        if(token[0] != ']')
          return do_err(ctx, "Missing comma or closed bracket");
      }
      else {
        // object
        do_token(token, ctx, usrc);
        if(ctx.eof)
          return do_err(ctx, "Object not terminated properly");
        else if(ctx.error)
          return;

        if(token[0] == ',') {
          do_token(token, ctx, usrc);
          if(ctx.eof)
            return do_err(ctx, "Missing key string");
          else if(ctx.error)
            return;

          if(token[0] != '\"')
            return do_err(ctx, "Missing key string");

          auto emr = frm.pso->try_emplace(key_pool.intern(token.data() + 1, token.size() - 1));
          if(!emr.second)
            return do_err(ctx, "Duplicate key string");

          do_token(token, ctx, usrc);
          if(token[0] != ':')
            return do_err(ctx, "Missing colon");

          do_token(token, ctx, usrc);
          if(ctx.eof)
            return do_err(ctx, "Missing value");
          else if(ctx.error)
            return;

          // next
          pstor = &(emr.first->second.mf_stor());
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
do_escape_string_in_utf8(Unified_Sink usink, const ::rocket::cow_string& str)
  {
    char temp[16] = "\\";
    auto bptr = str.c_str();
    const auto eptr = str.c_str() + str.length();
    while(bptr != eptr) {
      temp[1] = *bptr;
      int ch = static_cast<uint8_t>(*bptr);
      bptr ++;

      if(is_any(ch, '\\', '\"', '/'))
        usink.putn(temp, 2);
      else if(is_within(ch, 0x20, 0x7E))
        usink.putc(temp[1]);
      else if(ch == '\b')
        usink.putn("\\b", 2);
      else if(ch == '\f')
        usink.putn("\\f", 2);
      else if(ch == '\n')
        usink.putn("\\n", 2);
      else if(ch == '\r')
        usink.putn("\\r", 2);
      else if(ch == '\t')
        usink.putn("\\t", 2);
      else {
        // Convert this character to UTF-16.
        char16_t c16;
        ::std::mbstate_t mbst = { };
        size_t mblen = ::std::mbrtoc16(&c16, bptr - 1, static_cast<size_t>(eptr - bptr) + 1, &mbst);
        if(mblen == 0) {
          // A null byte has been consumed and stored into `c16`.
          usink.putn("\\u0000", 6);
        }
        else if(static_cast<int>(mblen) < 0) {
          // The input string is invalid. Consume one byte anyway, but print a
          // replacement character.
          usink.putn("\\uFFFD", 6);
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
          usink.putn(temp, ntemp);
          bptr += mblen - 1;
        }
      }
    }
  }

void
do_print_to(Unified_Sink usink, const variant_type& root, Options opts)
  {
    // Break deep recursion with a handwritten stack.
    struct xFrame
      {
        const V_array* psa;
        V_array::const_iterator ita;
        const V_object* pso;
        V_object::const_iterator ito;
      };

    ::std::vector<xFrame> stack;
    ::rocket::ascii_numput nump;
    const variant_type* pstor = &root;

  do_unpack_loop_:
    switch(static_cast<Type>(pstor->index()))
      {
      case t_null:
        usink.putn("null", 4);
        break;

      case t_array:
        if(!pstor->as<V_array>().empty()) {
          // open
          auto& frm = stack.emplace_back();
          frm.psa = &(pstor->as<V_array>());
          frm.ita = frm.psa->begin();
          usink.putc('[');
          pstor = &(frm.ita->mf_stor());
          goto do_unpack_loop_;
        }

        usink.putn("[]", 2);
        break;

      case t_object:
        if(!pstor->as<V_object>().empty()) {
          // open
          auto& frm = stack.emplace_back();
          frm.pso = &(pstor->as<V_object>());
          frm.ito = frm.pso->begin();
          usink.putn("{\"", 2);
          do_escape_string_in_utf8(usink, frm.ito->first.rdstr());
          usink.putn("\":", 2);
          pstor = &(frm.ito->second.mf_stor());
          goto do_unpack_loop_;
        }

        usink.putn("{}", 2);
        break;

      case t_boolean:
        nump.put_TB(pstor->as<V_boolean>());
        usink.putn(nump.data(), nump.size());
        break;

      case t_integer:
        if(opts & option_json_mode) {
          // as floating-point number; inaccurate for large values
          nump.put_DD(static_cast<V_number>(pstor->as<V_integer>()));
          usink.putn(nump.data(), nump.size());
        }
        else {
          // precise; annotated
          nump.put_DI(pstor->as<V_integer>());
          usink.putn("\"$l:", 4);
          usink.putn(nump.data(), nump.size());
          usink.putc('\"');
        }
        break;

      case t_number:
        if(::std::isfinite(pstor->as<V_number>())) {
          // finite; unquoted
          nump.put_DD(pstor->as<V_number>());
          usink.putn(nump.data(), nump.size());
        }
        else if(opts & option_json_mode) {
          // invalid; nullified
          usink.putn("null", 4);
        }
        else {
          // non-finite; annotated
          usink.putn("\"$d:", 4);
          nump.put_DD(pstor->as<V_number>());
          usink.putn(nump.data(), nump.size());
          usink.putc('\"');
        }
        break;

      case t_string:
        if((opts & option_json_mode) || (pstor->as<V_string>()[0] != '$')) {
          // general; quoted
          usink.putc('\"');
          do_escape_string_in_utf8(usink, pstor->as<V_string>());
          usink.putc('\"');
        }
        else {
          // starts with `$`; annotated
          usink.putn("\"$s:", 4);
          do_escape_string_in_utf8(usink, pstor->as<V_string>());
          usink.putc('\"');
        }
        break;

      case t_binary:
        if(opts & option_json_mode) {
          // invalid; nullified
          usink.putn("null", 4);
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
            usink.putn("\"$h:", 4);

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

              usink.putn(hex_word, 16);
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

              usink.putn(hex_word, nrem * 2);
            }
          }
          else {
            // base64
            usink.putn("\"$b:", 4);

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

              usink.putn(b64_word, 4);
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

              usink.putn(b64_word, 4);
            }
          }
          usink.putc('\"');
        }
        break;

      case t_time:
        if(opts & option_json_mode) {
          // invalid; nullified
          usink.putn("null", 4);
        }
        else {
          // annotated
          nump.put_DI(::std::chrono::time_point_cast<::std::chrono::milliseconds>(
                                pstor->as<V_time>()).time_since_epoch().count());
          usink.putn("\"$t:", 4);
          usink.putn(nump.data(), nump.size());
          usink.putc('\"');
        }
        break;

      default:
        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "taxon::Value: unknown type enumeration `%d`",
              static_cast<int>(pstor->index()));
      }

    while(!stack.empty()) {
      auto& frm = stack.back();
      if(frm.psa) {
        // array
        if(++ frm.ita != frm.psa->end()) {
          // next
          usink.putc(',');
          pstor = &(frm.ita->mf_stor());
          goto do_unpack_loop_;
        }

        // end
        usink.putc(']');
      }
      else {
        // object
        if(++ frm.ito != frm.pso->end()) {
          // next
          usink.putn(",\"", 2);
          do_escape_string_in_utf8(usink, frm.ito->first.rdstr());
          usink.putn("\":", 2);
          pstor = &(frm.ito->second.mf_stor());
          goto do_unpack_loop_;
        }

        // end
        usink.putc('}');
      }

      // close
      stack.pop_back();
    }
  }

}  // namespace

// We assume that a all-bit-zero struct represents the `null` value.
// This is effectively undefined behavior. Don't play with this at home!
alignas(Value) const char null_storage[sizeof(Value)] = { };

void
Value::
do_nonrecursive_destructor() noexcept
  {
#ifdef ROCKET_DEBUG
    // Attempt to run out of stack in a rather stupid way.
    static char* volatile s_stupid_begin;
    static char* volatile s_stupid_end;

    char stupid[1000] = { };
    s_stupid_begin = stupid;
    s_stupid_end = stupid + sizeof(s_stupid_end);

    s_stupid_begin[0] = 1;
    s_stupid_end[-1] = 2;
#endif

    // Break deep recursion with a handwritten stack.
    ::std::vector<bytes_type> stack;

  do_unpack_loop_:
    switch(this->m_stor.index())
      {
      case t_array:
        try {
          auto& sa = this->m_stor.mut<V_array>();
          if(sa.unique())
            for(auto it = sa.mut_begin();  it != sa.end();  ++it)
              ::std::swap(stack.emplace_back(), reinterpret_cast<bytes_type&>(it->m_stor));
        }
        catch(::std::exception& stdex)
          { ::std::fprintf(stderr, "WARNING: %s\n", stdex.what());  }
        break;

      case t_object:
        try {
          auto& so = this->m_stor.mut<V_object>();
          if(so.unique())
            for(auto it = so.mut_begin();  it != so.end();  ++it)
              ::std::swap(stack.emplace_back(), reinterpret_cast<bytes_type&>(it->second.m_stor));
        }
        catch(::std::exception& stdex)
          { ::std::fprintf(stderr, "WARNING: %s\n", stdex.what());  }
        break;
      }

    ::rocket::destroy(&(this->m_stor));
    reinterpret_cast<bytes_type&>(this->m_stor) = bytes_type();

    if(!stack.empty()) {
      reinterpret_cast<bytes_type&>(this->m_stor) = stack.back();
      stack.pop_back();
      goto do_unpack_loop_;
    }
  }

void
Value::
parse_with(Parser_Context& ctx, ::rocket::tinybuf& buf, Options opts)
  {
    do_parse_with(this->m_stor, ctx, &buf, opts);
  }

void
Value::
parse_with(Parser_Context& ctx, const ::rocket::cow_string& str, Options opts)
  {
    Memory_Source msrc(str.data(), str.size());
    do_parse_with(this->m_stor, ctx, &msrc, opts);
  }

void
Value::
parse_with(Parser_Context& ctx, const char* str, size_t len, Options opts)
  {
    Memory_Source msrc(str, len);
    do_parse_with(this->m_stor, ctx, &msrc, opts);
  }

void
Value::
parse_with(Parser_Context& ctx, const char* str, Options opts)
  {
    Memory_Source msrc(str, ::strlen(str));
    do_parse_with(this->m_stor, ctx, &msrc, opts);
  }

void
Value::
parse_with(Parser_Context& ctx, ::std::FILE* fp, Options opts)
  {
    do_parse_with(this->m_stor, ctx, fp, opts);
  }

bool
Value::
parse(::rocket::tinybuf& buf, Options opts)
  {
    Parser_Context ctx;
    do_parse_with(this->m_stor, ctx, &buf, opts);
    return !ctx.error;
  }

bool
Value::
parse(const ::rocket::cow_string& str, Options opts)
  {
    Parser_Context ctx;
    Memory_Source msrc(str.data(), str.size());
    do_parse_with(this->m_stor, ctx, &msrc, opts);
    return !ctx.error;
  }

bool
Value::
parse(const char* str, size_t len, Options opts)
  {
    Parser_Context ctx;
    Memory_Source msrc(str, len);
    do_parse_with(this->m_stor, ctx, &msrc, opts);
    return !ctx.error;
  }

bool
Value::
parse(const char* str, Options opts)
  {
    Parser_Context ctx;
    Memory_Source msrc(str, ::strlen(str));
    do_parse_with(this->m_stor, ctx, &msrc, opts);
    return !ctx.error;
  }

bool
Value::
parse(::std::FILE* fp, Options opts)
  {
    Parser_Context ctx;
    do_parse_with(this->m_stor, ctx, fp, opts);
    return !ctx.error;
  }

void
Value::
print_to(::rocket::tinybuf& buf, Options opts) const
  {
    do_print_to(&buf, this->m_stor, opts);
  }

void
Value::
print_to(::rocket::cow_string& str, Options opts) const
  {
    do_print_to(&str, this->m_stor, opts);
  }

void
Value::
print_to(::std::FILE* fp, Options opts) const
  {
    do_print_to(fp, this->m_stor, opts);
  }

::rocket::cow_string
Value::
to_string(Options opts) const
  {
    ::rocket::cow_string str;
    do_print_to(&str, this->m_stor, opts);
    return str;
  }

void
Value::
print_to_stderr(Options opts) const
  {
    do_print_to(stderr, this->m_stor, opts);
  }

}  // namespace taxon
