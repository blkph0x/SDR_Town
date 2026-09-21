from pathlib import Path

path = Path("src/SdrplayProfile.cpp")
text = path.read_text(encoding="utf-8")

function_start = text.index("std::vector<std::string> windowsSoapyRoots() {")
registry_start = text.index("#ifdef _WIN32", function_start)
registry_end = text.index("#endif", registry_start) + len("#endif")
overrides_start = text.index(
    "    // Explicit overrides are first so portable and managed installations win.",
    registry_end,
)
overrides_end = text.index("\n\n    const char* programFiles64", overrides_start)

registry_block = text[registry_start:registry_end]
overrides_block = text[overrides_start:overrides_end]

if registry_start >= overrides_start:
    raise SystemExit("registry block is not before override block")
if text.count("const char* api = std::getenv(\"SDRPLAY_API_DIR\")") != 1:
    raise SystemExit("unexpected SDRPLAY_API_DIR override count")
if text.count("appendRegistryInstall(KEY_WOW64_64KEY)") != 1:
    raise SystemExit("unexpected SDRplay registry block count")

updated = (
    text[:registry_start]
    + overrides_block
    + "\n\n"
    + registry_block
    + text[overrides_end:]
)
path.write_text(updated, encoding="utf-8", newline="\n")

for temporary in (
    ".github/workflows/apply-sdrplay-root-order.yml",
    "scripts/reorder_sdrplay_roots.py",
):
    p = Path(temporary)
    if p.exists():
        p.unlink()
