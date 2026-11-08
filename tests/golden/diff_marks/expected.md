```cpp
class gadget {
public:
  constexpr bool ready() const noexcept;
@[  void reset(*val* v);]{.add}@
@[  void arm();]{.rm}@
};
```

::: wording

## Observers [gadget.observe]{- .sref} {-}

```cpp
constexpr bool ready() const@[ noexcept]{.rm}@;
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_copy_constructible_v<T>` is `true`,
- [#.#]{.pnum} `is_move_constructible_v<T>` is `true`.

[#]{.pnum} *Returns*: `true` if and only if the gadget is [ready]{.rm}[armed (see ([gadget.mod]{- .sref}))]{.add}.

::: rm

[#]{.pnum} *Throws*: Nothing.

:::

::: add

[x]{.pnum} *Remarks*: A remark the revision adds, naming `x.$val$`.

:::

::: add

```cpp
void reset($val$ v);
```

[x+1]{.pnum} *Effects*: Resets the gadget.

:::

::: rm

[#]{.pnum} A free paragraph the revision removes.

:::

[#]{.pnum} A free paragraph the revision keeps.

:::

::: rm

::: wording

## Arming [gadget.arm]{- .sref} {-}

```cpp
void arm();
```

[#]{.pnum} *Effects*: Arms the gadget.

:::

:::

::: add

::: wording

## Modifiers [gadget.mod]{- .sref} {-}

```cpp
void reset($val$ v);
```

[x+2]{.pnum} *Effects*: Equivalent to:

```cpp
$val$ = v;
```

:::

:::
