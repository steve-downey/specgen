::: wording

## Macro continuations [macro.general]{- .sref} {-}

```cpp
namespace std {
  template<class T>
  class gadget {
  public:
    int size() const;
  };
}
```

[#]{.pnum} *noex* is a placeholder in leading prose.

```cpp
int size() const;
```

[#]{.pnum} *Preconditions*: `T` meets the *Cpp17Destructible* and *Cpp17CopyConstructible* requirements.

[#]{.pnum} *Returns*: `0`.

[#]{.pnum} *Remarks*: The following descriptions apply:

- [#.#]{.pnum} Meets *Cpp17CopyConstructible* requirements.

*after* follows the list.

| Type *cv* qualifier | Meaning |
|---|---|
| A *key* | Meets *Cpp17Destructible* requirements. |
: [Flat table *caption*]{#macro.flat}

| | A | B |
|---|---|---|
| **Type *cv* qualifier** | First *value* | Second *value* |
: [Grid table]{#macro.grid}

*resume* follows the tables.

::: note

The type meets *Cpp17Destructible* requirements.

:::

::: example

A *cv* qualifier is written literally.

:::

:::
