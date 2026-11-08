```cpp
namespace std {
  struct wrapper {
    // @[leading.members]{- .sref}@, members
    wrapper() = default;

    virtual void run() = 0;
  };
}
```

::: wording

## Leading descriptions [leading]{- .sref} {-}

### General [leading.general]{- .sref} {-}

[#]{.pnum} A wrapper stores one value.

[#]{.pnum} Its description is routed independently of its synopsis.

### Members [leading.members]{- .sref} {-}

```cpp
wrapper() = default;
```

[#]{.pnum} Constructs an empty wrapper.

```cpp
virtual void run() = 0;
```

[#]{.pnum} A concrete wrapper supplies this operation.

### Functions [leading.functions]{- .sref} {-}

```cpp
inline void first();
```

[#]{.pnum} The first function has prose only.

```cpp
inline void second();
```

[#]{.pnum} The second function has its own description.

```cpp
inline void third();
inline void third(int);
```

[#]{.pnum} The third function introduces the labelled requirements.

[#]{.pnum} This is a separate leading paragraph with ([leading.general]{- .sref}).

[#]{.pnum} *Effects*: Performs the operation.

:::
