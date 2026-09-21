from pathlib import Path

path = Path("src/SdrplayProfile.cpp")
text = path.read_text(encoding="utf-8")
old = '''std::vector<std::string> windowsSoapyRoots() {
    std::vector<std::string> roots;

#ifdef _WIN32
    // The official API installer records its real install directory here.
    // Check both registry views so a 64-bit portable build also finds API
    // installations written by a 32-bit installer helper.
    const auto appendRegistryInstall = [&](REGSAM view) {
        HKEY key = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          "SOFTWARE\\\\SDRplay\\\\Service\\\\API",
                          0, KEY_READ | view, &key) != ERROR_SUCCESS || !key) return;
        char value[32768]{};
        DWORD type = 0;
        DWORD bytes = static_cast<DWORD>(sizeof(value));
        const LSTATUS status = RegQueryValueExA(
            key, "Install_Dir", nullptr, &type,
            reinterpret_cast<LPBYTE>(value), &bytes);
        RegCloseKey(key);
        if (status != ERROR_SUCCESS || bytes <= 1 ||
            (type != REG_SZ && type != REG_EXPAND_SZ)) return;
        std::string installDir(value);
        if (type == REG_EXPAND_SZ) {
            char expanded[32768]{};
            const DWORD n = ExpandEnvironmentStringsA(
                installDir.c_str(), expanded, static_cast<DWORD>(sizeof(expanded)));
            if (n > 0 && n <= sizeof(expanded)) installDir.assign(expanded);
        }
        appendRoot(roots, installDir);
    };
    appendRegistryInstall(KEY_WOW64_64KEY);
    appendRegistryInstall(KEY_WOW64_32KEY);
#endif

    // Explicit overrides are first so portable and managed installations win.
    if (const char* api = std::getenv("SDRPLAY_API_DIR"); api && *api)
        appendRoot(roots, api);
    if (const char* root = std::getenv("SOAPY_SDR_ROOT"); root && *root)
        appendRoot(roots, root);
'''
new = '''std::vector<std::string> windowsSoapyRoots() {
    std::vector<std::string> roots;

    // Explicit overrides are first so portable and managed installations win.
    if (const char* api = std::getenv("SDRPLAY_API_DIR"); api && *api)
        appendRoot(roots, api);
    if (const char* root = std::getenv("SOAPY_SDR_ROOT"); root && *root)
        appendRoot(roots, root);

#ifdef _WIN32
    // The official API installer records its real install directory here.
    // Check both registry views so a 64-bit portable build also finds API
    // installations written by a 32-bit installer helper.
    const auto appendRegistryInstall = [&](REGSAM view) {
        HKEY key = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          "SOFTWARE\\\\SDRplay\\\\Service\\\\API",
                          0, KEY_READ | view, &key) != ERROR_SUCCESS || !key) return;
        char value[32768]{};
        DWORD type = 0;
        DWORD bytes = static_cast<DWORD>(sizeof(value));
        const LSTATUS status = RegQueryValueExA(
            key, "Install_Dir", nullptr, &type,
            reinterpret_cast<LPBYTE>(value), &bytes);
        RegCloseKey(key);
        if (status != ERROR_SUCCESS || bytes <= 1 ||
            (type != REG_SZ && type != REG_EXPAND_SZ)) return;
        std::string installDir(value);
        if (type == REG_EXPAND_SZ) {
            char expanded[32768]{};
            const DWORD n = ExpandEnvironmentStringsA(
                installDir.c_str(), expanded, static_cast<DWORD>(sizeof(expanded)));
            if (n > 0 && n <= sizeof(expanded)) installDir.assign(expanded);
        }
        appendRoot(roots, installDir);
    };
    appendRegistryInstall(KEY_WOW64_64KEY);
    appendRegistryInstall(KEY_WOW64_32KEY);
#endif
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one roots block, found {text.count(old)}")
path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")

for temporary in (
    ".github/workflows/apply-sdrplay-root-order.yml",
    "scripts/reorder_sdrplay_roots.py",
):
    p = Path(temporary)
    if p.exists():
        p.unlink()
