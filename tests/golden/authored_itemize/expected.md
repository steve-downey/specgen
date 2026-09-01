```cpp
class widget {
public:
  // @[widget.mod]{- .sref}@, modifiers
  void configure(int mode);
};
```

::: wording

## Modifiers [widget.mod]{- .sref} {-}

```cpp
void configure(int mode);
```

[#]{.pnum} *Constraints*: All of the following are true:

- [#.#]{.pnum} `mode >= 0`,
- [#.#]{.pnum} `mode <= 2`, and
- [#.#]{.pnum} `mode` denotes a supported mode as specified in ([external.mode.requirements]{- .sref}).

[#]{.pnum} *Effects*: Configures the widget.

:::
