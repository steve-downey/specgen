```cpp
namespace std {
  class gadget {
  public:
    // @[gadget.observe]{- .sref}@, observers
    bool ready() const @[noexcept]{.add}@;
    int level() const;

    // @[gadget.mod]{- .sref}@, modifiers
    void @[reset]{.rm}@@[clear]{.add}@();
    void arm(int level);

    @[// [gadget.query]{- .sref}, queries]{.add}@
    @[bool armed() const;]{.add}@
  };
}
```

::: wording

## Observers [gadget.observe]{- .sref} {-}

```cpp
bool ready() const @[noexcept]{.add}@;
```

[#]{.pnum} *Returns*: `true` if the gadget is [ready.]{.rm}[armed.]{.add}

::: add

[x]{.pnum} *Complexity*: Constant.

:::

```cpp
int level() const;
```

[#]{.pnum} *Returns*: The current level.

[#]{.pnum} *Remarks*: A gadget starts at level zero.

:::

::: wording

## Modifiers [gadget.mod]{- .sref} {-}

::: rm

```cpp
void reset();
```

[#]{.pnum} *Effects*: Resets the gadget to level zero.

:::

::: add

```cpp
void clear();
```

[x+1]{.pnum} *Effects*: Clears the gadget, returning it to level zero.

:::

```cpp
void arm(int level);
```

[#]{.pnum} *Preconditions*: `level` is [positive.]{.rm}[greater than zero.]{.add}

[#]{.pnum} *Effects*: Arms the gadget at `level`.

::: add

[x+2]{.pnum} Arming an armed gadget replaces its level.

:::

:::

::: add

::: wording

## Queries [gadget.query]{- .sref} {-}

```cpp
bool armed() const;
```

[x+3]{.pnum} *Returns*: `true` if the gadget has been armed.

:::

:::
