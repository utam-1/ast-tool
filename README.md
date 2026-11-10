# docmerge

A config-driven 3-way merge tool for documentation files (`NEWS`, `ChangeLog`, `CHANGELOG.md`, release notes).

`docmerge` resolves the merge conflicts developers hate most—*"both of us added a changelog entry at the top of the file"*—by parsing documentation into a hierarchy of blocks defined in `.docmerge.toml`, merging children by key, ordering new entries deterministically (`date-desc`, `version-desc`, `alpha-asc`, or position-based), and verifying safety invariants before writing.

I vibe coded almost 70% of this (just like most projects out there). The algorithm
and the idea in general is something I came up with while working on open source projects. If you just came across this project, use it and let me know if it works for you.

---

## Build & Install

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

# Optional system-wide install (e.g. /usr/local/bin/docmerge)
sudo cmake --install build
```

---

## Quick Start for Open-Source Projects (GitHub / GitLab)

### 1. Generate or add `.docmerge.toml` at your repository root
You can infer a starter config from an existing documentation file:
```bash
docmerge init CHANGELOG.md > .docmerge.toml
docmerge check CHANGELOG.md
```

### 2. Add `.gitattributes` to your repository
Commit `.gitattributes` so Git knows which files use `docmerge`:
```gitattributes
NEWS          merge=docmerge
ChangeLog     merge=docmerge
CHANGELOG.md  merge=docmerge
```

### 3. Activate the Git merge driver locally
Each developer (or CI job) runs once inside the repo:
```bash
docmerge install
```
This configures `.git/config`:
```ini
[merge "docmerge"]
    name = documentation-aware merge
    driver = docmerge merge %O %A %B %L %P
```

### 4. Resolving conflicts created by GitHub / GitLab Web UI
Web "Merge" / "Rebase" buttons on GitHub or GitLab do not run custom local Git merge drivers. If a conflict already exists in your working tree, resolve it in place with:
```bash
git checkout --conflict=diff3 -- CHANGELOG.md
docmerge resolve CHANGELOG.md
```

---

## CLI Reference

| Command | Description |
| :--- | :--- |
| `docmerge merge BASE OURS THEIRS [MARKER_SIZE] [PATH]` | Git merge driver mode; writes merged result atomically into `OURS` |
| `docmerge resolve FILE` | Resolve an already-conflicted file (with `diff3` markers) in place |
| `docmerge check [FILE...]` | Validate `.docmerge.toml` and check document block counts & sort order |
| `docmerge dump FILE` | Print the parsed block tree with byte ranges |
| `docmerge init FILE` | Analyse an existing file and print a draft `.docmerge.toml` |
| `docmerge install` | Configure the Git `docmerge` merge driver in `.git/config` |

Global flags: `--config PATH`, `--explain`, `--dry-run`, `--help`, `--version`.

---
## Contributing
If you stumbled upon this project and you're interested in improving it, feel free
to open up issues and create PRs.
