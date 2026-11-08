::: wording

## Description blocks [blocks.general]{- .sref} {-}

```cpp
inline void run();
```

[#]{.pnum} *Effects*: Performs the operation.

::: note

An explanation with `int` and ([blocks.general]{- .sref}).

A second informative paragraph.

- The informative list is unnumbered.

:::

::: example

The following code illustrates an operation:

```cpp
  run();

// Commands in a code payload remain literal.
// \effects is part of this comment.
```

The example continues after its code.

:::

This continues the normative paragraph.

[#]{.pnum} This is a separate normative paragraph.

```cpp
inline int value();
```

[#]{.pnum} The result is equal to:

```cpp
1 + 2
```

Otherwise, the result is zero.

:::
