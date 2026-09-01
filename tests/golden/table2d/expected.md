```cpp
class optional {
public:
  // @[optional.assign]{- .sref}@, assignment
  optional& operator=(const optional& rhs);

private:
  int $val$ = 0; // exposition only
};
```

::: wording

## Assignment [optional.assign]{- .sref} {-}

```cpp
optional& operator=(const optional& rhs);
```

[#]{.pnum} *Effects*: See the following table.

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns the value of `rhs` to `$val$`. | direct-non-list-initializes `$val$` from `rhs`. |
| **`rhs` does not contain a value** | destroys `$val$`. | no effect. |
: [`optional::operator=(const optional&)` effects]{#optional.assign.copy}

:::
