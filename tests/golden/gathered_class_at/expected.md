::: wording

## Header <box> synopsis [box.syn]{- .sref} {-}

```cpp
struct crate {
  int size;
};

template<class T>
struct pallet {
  T slot;
};

struct label {
  int id;
};

struct crumb {
  int weight;
};

struct strap {
  int length;
};
```

[#]{.pnum} A program that instantiates `crumb` is ill-formed unless `sizeof(short) >= 2` is `true`.

[#]{.pnum} *Remarks*: A `crumb` is described where it is written.

### Class `crate` [box.crate]{- .sref} {-}

[#]{.pnum} A program that instantiates `crate` is ill-formed unless `sizeof(int) >= 2` is `true`.

[#]{.pnum} *Remarks*: A `crate` owns no storage.

### Class template `pallet` [box.pallet]{- .sref} {-}

[#]{.pnum} A program that instantiates `pallet<T>` is ill-formed unless `sizeof(char) == 1` is `true`.

### Class `label` [box.label]{- .sref} {-}

[#]{.pnum} *Remarks*: A `label` names a `crate` and nothing else.

### Class `hinge` [box.hinge]{- .sref} {-}

```cpp
struct hinge {
  int pin;
};
```

#### General [box.hinge.general]{- .sref} {-}

[#]{.pnum} A program that instantiates `hinge` is ill-formed unless `sizeof(int) >= 2` is `true`.

[#]{.pnum} *Remarks*: A `hinge` joins two crates.

:::
