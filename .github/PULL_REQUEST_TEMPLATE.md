# Description

<!-- Quick description and explanation of changes -->

## Type of change

- [ ] Bugfix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Optimization / Refactor (performance or code quality improvement)
- [ ] Breaking change (fix or feature that would cause existing functionality/ABI to change)
- [ ] Documentation update
- [ ] Build / CI improvement

**Related issue (if applicable):**

- fixes #

## Library Footprint Impact

Please check the library code footprint impact before submitting. You can inspect the sizes locally by running:

```bash
ninja -C build footprint
# or
meson compile -C build footprint
```

or by reviewing the summary in the **Footprint** CI job.

| Library | Baseline Total Code | PR Total Code | Change | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `libfaac` | | | | |
| `libfaad` | | | | |
| `libfaam` | | | | |
| `libfaar` | | | | |

<!-- If there is a footprint increase, please explain why it is necessary and justified. -->

## Checklist:

- [ ] The code change has been tested and works locally.
- [ ] All existing and new tests pass (`meson test -C build`).
- [ ] I have verified the footprint impact of this change on the primary libraries (`ninja -C build footprint` or CI Footprint summary).
- [ ] Code follows existing conventions and style guidelines.
