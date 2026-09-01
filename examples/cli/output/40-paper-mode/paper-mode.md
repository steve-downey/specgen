::: add

```cpp
class gadget {
public:
  constexpr bool ready() const noexcept;
};
```

::: wording

## Observers [gadget.observe]{- .sref} {-}

```cpp
constexpr bool ready() const noexcept;
```

[x]{.pnum} *Constraints*:

- [x.#]{.pnum} `is_copy_constructible_v<T>` is `true`,
- [x.#]{.pnum} `is_move_constructible_v<T>` is `true`.

[x+1]{.pnum} *Returns*: `true` if and only if the gadget is ready.

[x+2]{.pnum} A second paragraph, so the added numbering has to advance.

[x+3]{.pnum} A free paragraph closing the subclause.

:::

::: wording

## Modifiers [gadget.mod]{- .sref} {-}

```cpp
constexpr void reset() noexcept;
```

[x]{.pnum} *Effects*: Resets the gadget.

:::

:::
