# Slices of the working draft

Each `.draft` file here is a run of lines copied unchanged from the C++ working
draft (`source/utilities.tex`, at the commit named in the file's first comment
line). They are the inputs of the draft importer's tests: the corpus is
hermetic, so a checkout of the draft is never needed to run them.

Updating one is a deliberate act: copy the same stable name's lines from a newer
draft, and let the importer's goldens show what moved.

The extension is `.draft`, not `.tex`: these are not this project's own LaTeX, and
the repository checks keep paper files out of the tree.
