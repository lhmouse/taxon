// This file is part of Poseidon.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "taxon.hpp"
#include <rocket/tinybuf_str.hpp>
#include <rocket/tinybuf_file.hpp>
namespace taxon {
namespace {

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

::rocket::tinybuf&
Value::
print(::rocket::tinybuf& buf) const
  {
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO

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
    return ::ferror(buf.get_handle()) == 0;
  }

}  // namespace taxon

template
class ::rocket::cow_vector<::taxon::Value>;

template
class ::rocket::cow_hashmap<::rocket::prehashed_string,
  ::taxon::Value, ::rocket::prehashed_string::hash>;
