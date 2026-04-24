# Contributing

Thanks for your interest in PhoneFilter.

## Workflow

1. Open an issue to discuss larger changes or ambiguous behavior.
2. Fork the repository and create a focused branch.
3. Match existing code style and keep pull requests small when possible.
4. Build with `pio run -e esp32dev` before submitting.
5. When you touch filtering, configuration, or telephony timing, add or extend tests under `test/` when practical and run `pio test --environment native`.

## Scope

Prefer minimal diffs: avoid drive-by refactors or unrelated formatting in the same change as a bug fix or feature.
