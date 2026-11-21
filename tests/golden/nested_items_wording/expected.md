::: wording

## Waiting [demo.wait](){.sref} {-}

```cpp
inline void wait();
```

[#]{.pnum} *Effects*: Blocks until one of:

- [#.#]{.pnum} a value is ready, and then either

  - it is returned, or
  - it is discarded, equivalent to:

    ```cpp
    discard();
    ```


- [#.#]{.pnum} the timeout expires.

:::
