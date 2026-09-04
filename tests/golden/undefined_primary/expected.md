::: wording

## Undefined primaries [undef.primary]{- .sref} {-}

```cpp
template<class L, class R>
struct join;
```

[#]{.pnum} *Remarks*: A program may specialize `join` for cv-unqualified program-defined types.

```cpp
template<class T>
struct bottom;
template<class T>
struct unit;
```

[#]{.pnum} *Remarks*: Registration points for the bottom element.

```cpp
struct bottom_tag;
```

[#]{.pnum} *Remarks*: An undefined primary with a non-template head.

```cpp
struct registry {};
```

:::
