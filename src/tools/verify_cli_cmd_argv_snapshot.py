#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8", errors="ignore")

try:
    run_cli = main[main.index("int runCLI("):main.index("int main(", main.index("int runCLI("))]
    helper_start = main.index("static std::deque<std::string> parseCliBatchCommandsFromRawArgs", main.index('#include "main.moc"'))
    batch_helper = main[helper_start:main.index("int runCLI(", helper_start)]
    batch_runtime = run_cli[
        run_cli.index("std::deque<std::string> cliBatchCommands = parseCliBatchCommandsFromRawArgs(rawCliArgs);"):
        run_cli.index("if (!cliBatchCommands.empty())")
    ]
except ValueError as exc:
    raise SystemExit(f"CLI argv snapshot regression: FAIL - cannot locate runCLI/main boundary: {exc}")

checks = {
    "startup help/version can ignore command payload":
        "bool ignoreCommandPayload = false" in main and
        "if (ignoreCommandPayload &&" in main and
        'arg == "--cmd" || arg == "--command" || arg == "--exec"' in main,
    "top-level help/version ignore command payload":
        'startupHasArg(argc, argv, {"--help", "-h", "/?", "help"}, true)' in main and
        'startupHasArg(argc, argv, {"--version", "-v", "version"}, true)' in main,
    "raw argv is snapshotted before QCoreApplication":
        run_cli.find("std::vector<std::string> rawCliArgs;") >= 0 and
        run_cli.find("std::vector<std::string> rawCliArgs;") < run_cli.find("QCoreApplication cliApp"),
    "batch command parser uses raw argv snapshot":
        "std::deque<std::string> cliBatchCommands = parseCliBatchCommandsFromRawArgs(rawCliArgs);" in run_cli,
    "batch command parser runs before QCoreApplication":
        run_cli.find("parseCliBatchCommandsFromRawArgs(rawCliArgs)") >= 0 and
        run_cli.find("parseCliBatchCommandsFromRawArgs(rawCliArgs)") < run_cli.find("QCoreApplication cliApp"),
    "batch command helper parses raw args":
        "for (size_t i = 1; i < args.size(); ++i)" in batch_helper and
        "std::string arg = args[i];" in batch_helper,
    "joined --cmd value uses raw argv snapshot":
        "for (size_t j = i + 1; j < args.size(); ++j)" in batch_helper and
        "joined << args[j];" in batch_helper,
    "runCLI no longer walks Qt-mutated argv for batch commands":
        "std::string arg = argv[i];" not in batch_runtime and
        "rawCliArgs[i]" not in batch_runtime,
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    print("CLI argv snapshot regression: FAIL")
    for name in missing:
        print(f" - {name}")
    raise SystemExit(1)

print("CLI argv snapshot regression: PASS")
