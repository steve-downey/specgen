::: wording

## Section prose [section]{- .sref} {-}

### General [section.general]{- .sref} {-}

[#]{.pnum} The header provides a `wrapper` for callable objects.

[#]{.pnum} A separate paragraph refers to ([section.class]{- .sref}).

### Class template wrapper [section.class]{- .sref} {-}

```cpp
namespace std {
  template<class T>
  struct wrapper {};
}
```

[#]{.pnum} The `wrapper` class template stores a callable object.

### Operations [section.operations]{- .sref} {-}

```cpp
inline void first();
```

[#]{.pnum} *Effects*: Performs the first operation.

[#]{.pnum} Wording between items stays between those items.

```cpp
inline void second();
```

[#]{.pnum} *Effects*: Performs the second operation.

#### Details [section.operations.detail]{- .sref} {-}

[#]{.pnum} A block-form paragraph belongs to the nested section.

[#]{.pnum} This paragraph belongs to the enclosing section again.

### Header synopsis [section.syn]{- .sref} {-}

```cpp
namespace std {
  struct section_part {};
}
```

[#]{.pnum} Prose from a followed header appears after the assembled synopsis.

:::
