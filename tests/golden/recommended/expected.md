::: wording

## Class template wrapper [recommended.class]{- .sref} {-}

```cpp
namespace std {
  template<class T>
  class wrapper {};
}
```

[#]{.pnum} *Recommended practice*: Implementations should avoid allocating memory for small values.

:::

::: wording

## Reset [recommended.reset]{- .sref} {-}

```cpp
inline void reset();
```

[#]{.pnum} *Effects*: Discards the stored value.

[#]{.pnum} *Remarks*: The wrapper may be reused.

[#]{.pnum} *Error conditions*: An error condition is reported by the caller.

[#]{.pnum} *Recommended practice*: Implementations should reuse allocated storage.

:::
