::: wording

## Modifiers [optional.mod]{- .sref} {-}

```cpp
constexpr void reset() noexcept;
```

[#]{.pnum} *Effects*: Equivalent to:

```cpp
$engaged$ = false;
```

[#]{.pnum} *Postconditions*: `has_value()` is `false`.

```cpp
friend constexpr bool operator==(const optional& x, const optional& y);
```

[#]{.pnum} *Returns*: `true` if both operands are disengaged, or both are engaged with equal contained values.

:::
