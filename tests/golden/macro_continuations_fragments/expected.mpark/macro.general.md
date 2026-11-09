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

[#]{.pnum} \placeholder{noex} is a placeholder in leading prose.

```cpp
int size() const;
```

[#]{.pnum} *Preconditions*: `T` meets the \oldconcept{Destructible} and \oldconcept{CopyConstructible} requirements.

[#]{.pnum} *Returns*: `0`.

[#]{.pnum} *Remarks*: The following descriptions apply:

- [#.#]{.pnum} Meets \oldconcept{CopyConstructible} requirements.

\placeholder{after} follows the list.

| Type \cv{} qualifier | Meaning |
|---|---|
| A \placeholder{key} | Meets \oldconcept{Destructible} requirements. |
: [Flat table \placeholder{caption}]{#macro.flat}

| | A | B |
|---|---|---|
| **Type \cv{} qualifier** | First \placeholder{value} | Second \placeholder{value} |
: [Grid table]{#macro.grid}

\placeholder{resume} follows the tables.

::: note

The type meets \oldconcept{Destructible} requirements.

:::

::: example

A \cv{} qualifier is written literally.

:::

:::
