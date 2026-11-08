::: wording

## Header <demo> synopsis [demo.syn]{- .sref} {-}

```cpp
namespace std {
  // @[demo.buffer]{- .sref}@, class template buffer

  template<class T> class buffer;

  namespace views {
    // @[demo.views]{- .sref}@, range adaptor objects

    inline constexpr $unspecified$ adapt;
  }

  namespace proto {
    // @[demo.encoding]{- .sref}@, encodings

    enum class encoding { utf8, utf16 };

    // @[demo.decoder]{- .sref}@, class template decoder

    template<encoding E> class decoder;

    // @[demo.decode]{- .sref}@, decoding

    template<encoding E> decoder<E> decode(const char* text);
  }

  // @[demo.decode]{- .sref}@, decoding

  template<class D> D decode(D d);

  template<class T> inline constexpr bool adapts = sizeof(views::adapt) == sizeof(T);

  template<class T>
  inline constexpr bool adapts_qualified = sizeof(views::adapt) == sizeof(T);

  namespace ranges {
    template<class T> inline constexpr bool enable_borrowed_range<buffer<T>> = true;
  }
}
```

:::

::: wording

## Class template `buffer` [demo.buffer]{- .sref} {-}

```cpp
namespace std {
  template<class T>
  class buffer {
  public:
    // @[demo.buffer.obs]{- .sref}@, observers
    constexpr bool empty() const;
  };
}
```

### Observers [demo.buffer.obs]{- .sref} {-}

```cpp
constexpr bool empty() const;
```

[#]{.pnum} *Returns*: `true`.

:::

::: wording

## Range adaptor objects [demo.views]{- .sref} {-}

```cpp
inline constexpr $unspecified$ views::adapt;
```

[#]{.pnum} *Remarks*: The name `views::adapt` denotes a range adaptor object. Given a subexpression `E`, the expression `views::adapt(E)` is expression-equivalent to `buffer(E)`.

:::

::: wording

## Encodings [demo.encoding]{- .sref} {-}

```cpp
enum class encoding { utf8, utf16 };
```

[#]{.pnum} *Remarks*: The values name the protocol families a decoder reads.

:::

::: wording

## Class template `decoder` [demo.decoder]{- .sref} {-}

```cpp
namespace std::proto {
  template<encoding E>
  class decoder {
  public:
    // @[demo.decoder.obs]{- .sref}@, observers
    constexpr encoding scheme() const;
  };
}
```

### Observers [demo.decoder.obs]{- .sref} {-}

```cpp
constexpr encoding scheme() const;
```

[#]{.pnum} *Returns*: `E`.

:::

::: wording

## Decoding [demo.decode]{- .sref} {-}

```cpp
template<encoding E>
  decoder<E> proto::decode(const char* text);
```

[#]{.pnum} *Returns*: A `decoder<E>` over `text`.

```cpp
template<class D>
  D decode(D d);
```

[#]{.pnum} *Returns*: `d`.

:::
