::: wording

## Header <nested> synopsis [nested.syn]{- .sref} {-}

```cpp
namespace std {
  struct $adaptor$ {}; // exposition only

  namespace views {
    // @[nested.adaptor]{- .sref}@, range adaptor objects

    inline constexpr $unspecified$ null_term;
  }

  namespace whatwg {
    enum class encoding { utf8 };

    // @[nested.whatwg]{- .sref}@, WHATWG operations

    template<encoding E> inline constexpr bool decode = true;

    struct $state$ {}; // exposition only
  }

  // @[nested.decode]{- .sref}@, decoding

  template<class D> constexpr bool decode(D);
}
```

### Range adaptor objects [nested.adaptor]{- .sref} {-}

```cpp
inline constexpr $unspecified$ views::null_term;
```

[#]{.pnum} *Effects*: Returns a null-terminated view of `r`.

### Decoding [nested.decode]{- .sref} {-}

```cpp
template<class D> constexpr bool decode(D);
```

[#]{.pnum} *Returns*: `true`.

### WHATWG operations [nested.whatwg]{- .sref} {-}

```cpp
template<encoding E> inline constexpr bool whatwg::decode = true;
```

[#]{.pnum} *Remarks*: `E` identifies an encoding.

:::
