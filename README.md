# Type Annotation eXtension for JSON

There seem to be so many data exchange formats in addition to [JSON](https://www.json.org/),
such as [BSON](https://www.json.org/), [UBJSON](https://ubjson.org/), and especially,
[Protobuf](https://protobuf.dev/).

So, why yet another format?

All formats above are binary formats. They have their own data type extensions
to JSON. They are claimed to be 'more efficient' in some way than JSON. They
provide libraries that are fast enough and easy to use.

Until, they do not solve our problems.

We still work with a lot of legacy tools. There is native JSON support in
JavaScript and MySQL. There are a lot of third-party libraries in varieties of
languages. We sometimes work with command-line tools, copying and pasting JSON
results from consoles, into some random IM app, or via SSH to another machine.

The human brain is such a large piece of cr@p that it refuses to read binary
data.

We acknowledge the necessity of a 64-bit integer type, an arbitrary binary data
type, a date/time type, and maybe more. However, rupturing with JSON, ruining
readability by humans, and requiring some brand-new unstandardized library, is
a no-go.

---

Hence TAXON. The data types are defined as follows:

|C++ Type                |Remarks                |Examples                  |
|:-----------------------|:----------------------|:-------------------------|
|`nullptr`               |                       |`null`                    |
|`bool`                  |                       |`true`, `false`           |
|`int64_t`               |64-bit signed integer  |`"$l:123"`, `"$l:-0x7B"`  |
|`double` (finite)       |floating-point number  |`123.45`                  |
|`double` (infinity)     |                       |`"$d:inf"`, `"$d:-inf"`   |
|`double` (NaN)          |                       |`"$d:nan"`, `$d:-nan`     |
|`cow_string` (plain)    |UTF-8 string           |`"hello"`                 |
|`cow_string` (escaped)  |                       |`"$s:hello"`              |
|`cow_bstring` (hex)     |arbitrary bytes        |`"$h:68656c6c6f"`         |
|`cow_bstring` (base64)  |                       |`"$b:aGVsbG8="`           |
|`cow_vector`            |array                  |`[1,2,3]`                 |
|`cow_hashmap`           |object                 |`{"x":"$h:4546","y":99}`  |

1. Integers, non-finite floating-point numbers and binary data must be encoded
   as strings, according to the scheme above.
2. The `$l:` prefix in a string is called the type _annotator_. The remaining
   part of the string is called the _payload_. The type annotation determines
   how the payload shall be interpreted.
3. For integers, the payload shall be the decimal, binary or hexadecimal
   representation of a signed 64-bit integer. Padding characters are not
   allowed. The value shall be within range.
4. For floating-point numbers, the payload shall be the decimal, binary or
   hexadecimal representation of an IEEE-754 double-precision floating-point
   number, or one of the special strings `inf`, `Infinity`, `nan` and `NaN`
   with an optional sign. Padding characters are not allowed.
5. For binary data, an application may choose the hexadecimal encoding or
   base64 encoding as appropriate. The payload shall be a valid hexadecimal or
   base64 sequence without spacing.
6. Strings without annotators are to be interpreted verbatim. If a string value
   begins with `$`, it shall be annotated with `$s:`. The annotator is not
   otherwise required on strings.
7. When parsing a TAXON source, if a string begins with `$` but not with a
   known annotator, or if a payload fails a _shall_ requirement above, the
   parser shall reject the source.
