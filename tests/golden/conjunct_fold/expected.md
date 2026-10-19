::: wording

## Folded conjuncts [demo.fold]{- .sref} {-}

```cpp
template<class FIRST_ARGUMENT_TYPE, class SECOND_ARGUMENT_TYPE>
void g(FIRST_ARGUMENT_TYPE, SECOND_ARGUMENT_TYPE);
```

[#]{.pnum} *Constraints*: `interconvertible_with_every_other_argument_of_the_same_rather_long_family_of_argument_types_too<FIRST_ARGUMENT_TYPE, SECOND_ARGUMENT_TYPE>` is satisfied and `all_three_of_these_types_are_layout_compatible_with_each_other_in_turn(static_cast<FIRST_ARGUMENT_TYPE*>(nullptr), static_cast<SECOND_ARGUMENT_TYPE*>(nullptr), static_cast<FIRST_ARGUMENT_TYPE*>(nullptr))` is `true`.

[#]{.pnum} *Effects*: Does nothing.

```cpp
template<class FIRST_ARGUMENT_TYPE, class SECOND_ARGUMENT_TYPE>
void h(FIRST_ARGUMENT_TYPE, SECOND_ARGUMENT_TYPE);
```

[#]{.pnum} *Mandates*: `sizeof(FIRST_ARGUMENT_TYPE) + sizeof(SECOND_ARGUMENT_TYPE) + sizeof(FIRST_ARGUMENT_TYPE) < sizeof(SECOND_ARGUMENT_TYPE) + sizeof(FIRST_ARGUMENT_TYPE) * 64 + sizeof(SECOND_ARGUMENT_TYPE)` is `true`.

[#]{.pnum} *Effects*: Does nothing.

```cpp
template<class FUNCTION_TYPE, class FIRST_ARGUMENT_TYPE, class SECOND_ARGUMENT_TYPE>
void k(FUNCTION_TYPE, FIRST_ARGUMENT_TYPE, SECOND_ARGUMENT_TYPE);
```

[#]{.pnum} *Constraints*: `FUNCTION_TYPE` models `holdable` and `call_result_t<FUNCTION_TYPE&, wrapped_reference_to_an_argument_with_a_rather_long_descriptive_name_t<FIRST_ARGUMENT_TYPE>, wrapped_reference_to_an_argument_with_a_rather_long_descriptive_name_t<SECOND_ARGUMENT_TYPE>>` models `holdable`.

[#]{.pnum} *Effects*: Does nothing.

:::
