# third_party_patches

This directory stores compatibility patches for vendored third-party code.

Current bundle:

- `rsocket/0001-s7-rsocket-compat.patch`

Apply manually:

```bash
cd /home/al/projects/s7/s7pp
./scripts/apply_third_party_patches.sh
```

Dry-run:

```bash
cd /home/al/projects/s7/s7pp
./scripts/apply_third_party_patches.sh --dry-run
```

Notes:
- Patches are idempotent (`already applied` is detected and skipped).
- When you later fork rsocket and move fixes upstream, this bundle can be retired.

