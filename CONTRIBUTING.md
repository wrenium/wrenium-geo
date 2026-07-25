# Contributing

See `README.md` for building and testing, and `AGENTS.md` for
codebase-specific conventions (those apply to human contributors too).

## Commits

- One topic per commit. If a change touches unrelated things -- a
  rename alongside a comment cleanup, a bug fix alongside a
  refactor -- split them into separate commits, even within the same
  file (`git add -p` selects individual hunks).
- Keep commit messages short but specific about what changed and,
  where it's not obvious, why.
- Don't leave a commit in a broken intermediate state -- e.g. a
  `CMakeLists.txt` `add_subdirectory()` into a path that doesn't exist
  yet at that commit.

## Before opening a PR

- `ctest --test-dir build --output-on-failure` passes.
- `clang-format --dry-run --Werror` is clean (CI enforces this).
- If you touched a public header, regenerate the docs
  (`doxygen Doxyfile`) and confirm no new warnings.
