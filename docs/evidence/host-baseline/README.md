# Fresh macOS host baseline

Run on 2026-09-22 with Python 3.12.14, CMake 3.31.6, Xcode 27.0 (27A266a), Apple Swift 6.4 and Apple Clang 21.0.0 on arm64 macOS. Dependencies are recorded in python-lock.txt. Local isolated tools: `/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin`.

Executed from repository root:

```sh
PATH='/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin:'"$PATH" HSS_VERIFICATION_OUT='/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/outputs/hybrid-spatial-scanner/docs/evidence/host-baseline' bash tools/run_verification.sh
```

Observed: 99 pytest cases passed; CMake configured and built the two reference executables with address/undefined sanitizers; 2/2 CTest checks passed; Swift C-header import typecheck passed. Raw logs are adjacent. Build outputs are ignored. The original Linux evidence in verification/ is preserved.

These are reference tests only. Production engine linkage, native app builds, sensor capture and physical-device metrology remain separate requirements. No iPhone test has been executed. B-001 was independently reviewed and integrated; these tests do not complete any additional B task.
