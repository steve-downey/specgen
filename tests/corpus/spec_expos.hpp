// tests/corpus/spec_expos.hpp                                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.5/§4.3: exposition-only
// members). The private storage `value_` carries `//! \expos`, so in the
// synopsis its name renders as an `\exposid` span (kebab-derived: trailing `_`
// stripped -> `value`) with a trailing `// exposition only` comment, produced
// via the §3.6 sentinel pipeline (rewrite -> clang-format -> recover spans).
// `count_` uses `\expos(size)` to override the derived name, and the private
// alias `raw_` is exposition too: it renders as `using raw = T; // exposition
// only` and the public `value_type`'s RHS names it by its exposid. Self-contained (no
// #includes) and built from a template parameter / int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_HPP

namespace demo {

template <class T>
class holder {
  public:
    // \ref{holder.obs}, observers
    T    get() const;
    void set(T value);

  private:
    //! \expos(raw)
    using raw_ = T;

  public:
    using value_type = raw_;

  private:
    union {
        //! \expos
        T value_;

        T spare_;
    };

    //! \expos(size)
    int count_;

    // \ref{holder.expos}, exposition only helpers
    //! \expos(convert-ref-init-val)
    //! \effects-equiv
    template <class U>
    void convert_ref_init_val(U&& value) {
        value_ = static_cast<T>(value);
    }
};

// \rSec3[holder.obs]{Observers}

//! \effects Returns `value_`.
template <class T>
T holder<T>::get() const {
    return value_;
}

//! \effects-equiv
template <class T>
void holder<T>::set(T value) {
    convert_ref_init_val(value);
}

// \rSec3[holder.expos]{Exposition only helpers}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_HPP
