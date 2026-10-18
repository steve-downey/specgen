```cpp
namespace std {
  struct $token$; // exposition only
}
```

```cpp
namespace std {
  struct $token$ {
    int id;
  }; // exposition only
}
```

```cpp
namespace std {
  template<class T>
  struct $box$; // exposition only
}
```

```cpp
namespace std {
  template<class T>
  struct $box$ {
    T value;
  }; // exposition only
}
```

```cpp
namespace std {
  class widget {
  public:
    // @[widget.ops]{- .sref}@, operations
    $token$ issue($helper-t$ h) const;
    $count-t$ count() const;
    $mode-t$ mode() const;
    $box$<int> boxed() const;

  private:
    $token$ $last$; // exposition only
  };
}
```

::: wording

## Operations [widget.ops]{- .sref} {-}

```cpp
$token$ issue($helper-t$ h) const;
```

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return $token${h.value};
```

```cpp
$count-t$ count() const;
```

[#]{.pnum} *Returns*: The number of tokens issued.

```cpp
$mode-t$ mode() const;
```

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return $mode-t$::fast;
```

```cpp
$box$<int> boxed() const;
```

[#]{.pnum} *Returns*: A box holding zero.

:::
