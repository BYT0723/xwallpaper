# Spec: Localized web property labels

## Objective
`xwallpaper --list-properties` currently prints only `name<TAB>type<TAB>value`.
Wallpaper Engine projects already carry `general.properties.*.text` plus
`general.localization`, so add an opt-in `--labels` flag that appends the
localized label as a fourth TSV column. Default output stays byte-identical.

## Behavior
- Parse `general.localization` together with `general.properties`.
- Resolve one label per property: `localization[locale][property.text]`.
- Locale precedence: `LC_ALL`, `LC_MESSAGES`, `LANG`, then `en-us`.
- Map POSIX tags to WE keys: `zh_CN*` -> `zh-chs`, `zh_TW/HK/MO*` -> `zh-cht`,
  `ja*` -> `ja-jp`, `ko*` -> `ko-kr`, `ru*` -> `ru-ru`, `en*`/`C` -> `en-us`.
- Fallback: exact key, same-language key (`zh-*`), `en-us`, first available.
- Sanitize labels for TSV: strip HTML tags, replace tabs/newlines with spaces.
- `--labels` is only valid with `--list-properties`; other commands reject it.

## Commands
- Build: `make`
- Unit: `make test`
- Integration: `make test-xvfb`
- CLI libs: `make check-client-libs`

## Boundaries
- Always: keep the thin CLI free of heavy libs; keep default list output stable.
- Ask first: protocol field changes beyond the optional `labels=1` field.
- Never: add new dependencies, change property values, or touch the wallpaper.

## Success Criteria
- `--list-properties` output is unchanged.
- `--list-properties --labels` appends a non-empty localized label for the
  animeflash project (`zh_CN` -> Chinese, `en_US` -> English).
- Unknown/missing locale falls back to `en-us`, then the first locale.
- Unit + Xvfb tests cover label parsing, locale mapping, CLI flag, protocol
  encoding, and 4-column output.

## Open Questions
None. The user approved the opt-in `--labels` design.
