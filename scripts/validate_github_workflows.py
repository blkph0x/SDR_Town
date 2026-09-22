#!/usr/bin/env python3
"""Validate every GitHub Actions workflow before the expensive Windows build.

GitHub rejects malformed workflow YAML before creating a job, so a broken file
cannot validate itself.  This script is also suitable for a local/pre-commit
check and reports the exact file, line and column of YAML parser failures.
"""

from __future__ import annotations

import argparse
import sys
import textwrap
from pathlib import Path
from typing import Iterable

try:
    import yaml
except ImportError as exc:  # pragma: no cover - exercised by CI setup failure
    print(
        "workflow validator requires PyYAML (python -m pip install PyYAML==6.0.2)",
        file=sys.stderr,
    )
    raise SystemExit(2) from exc


WORKFLOW_GLOBS = ("*.yml", "*.yaml")


def workflow_paths(root: Path) -> list[Path]:
    workflow_dir = root / ".github" / "workflows"
    paths: set[Path] = set()
    for pattern in WORKFLOW_GLOBS:
        paths.update(path for path in workflow_dir.glob(pattern) if path.is_file())
    return sorted(paths)


def mapping_keys(node: yaml.nodes.Node) -> set[str]:
    if not isinstance(node, yaml.nodes.MappingNode):
        return set()
    keys: set[str] = set()
    for key_node, _ in node.value:
        if isinstance(key_node, yaml.nodes.ScalarNode):
            keys.add(str(key_node.value))
    return keys


def validate_text(text: str, display_name: str) -> list[str]:
    errors: list[str] = []
    try:
        documents = list(yaml.compose_all(text, Loader=yaml.BaseLoader))
    except yaml.MarkedYAMLError as exc:
        mark = exc.problem_mark or exc.context_mark
        location = ""
        if mark is not None:
            location = f":{mark.line + 1}:{mark.column + 1}"
        problem = exc.problem or exc.context or str(exc)
        return [f"{display_name}{location}: {problem}"]
    except yaml.YAMLError as exc:
        return [f"{display_name}: {exc}"]

    if len(documents) != 1:
        errors.append(
            f"{display_name}: expected exactly one YAML document, found {len(documents)}"
        )
        return errors

    root = documents[0]
    if root is None:
        errors.append(f"{display_name}: workflow is empty")
        return errors
    if not isinstance(root, yaml.nodes.MappingNode):
        errors.append(f"{display_name}: workflow root must be a mapping")
        return errors

    keys = mapping_keys(root)
    for required in ("on", "jobs"):
        if required not in keys:
            errors.append(f"{display_name}: missing required top-level key {required!r}")
    return errors


def validate_files(paths: Iterable[Path], root: Path) -> list[str]:
    errors: list[str] = []
    for path in paths:
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append(f"{path}: cannot read UTF-8 workflow: {exc}")
            continue
        try:
            display = path.relative_to(root).as_posix()
        except ValueError:
            display = str(path)
        errors.extend(validate_text(text, display))
    return errors


def run_self_test() -> int:
    valid = textwrap.dedent(
        """\
        name: Valid workflow
        on:
          push:
        jobs:
          test:
            runs-on: ubuntu-latest
            steps:
              - run: echo ok
        """
    )
    malformed = textwrap.dedent(
        """\
        name: Broken workflow
        on:
          push:
        jobs:
          prepare:
            runs-on: ubuntu-latest
            steps:
              - name: Write release notes
                run: |
                  python - <<'PY'
                  notes = \"\"\"# Release notes
        ## Receiver takeover recovery
        - This unindented Markdown escaped the YAML block scalar.
        \"\"\"
                  PY
        """
    )

    valid_errors = validate_text(valid, "self-test-valid.yml")
    malformed_errors = validate_text(malformed, "self-test-malformed.yml")
    if valid_errors:
        print("validator self-test rejected valid YAML:", file=sys.stderr)
        for error in valid_errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    if not malformed_errors:
        print("validator self-test failed to reject malformed YAML", file=sys.stderr)
        return 1
    print("workflow validator self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate GitHub Actions workflow YAML syntax and structure."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="Specific workflow files (default: .github/workflows/*.yml and *.yaml)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Prove the validator accepts a valid workflow and rejects the known failure pattern.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        result = run_self_test()
        if result != 0:
            return result

    root = Path.cwd().resolve()
    paths = [path.resolve() for path in args.paths] if args.paths else workflow_paths(root)
    if not paths:
        print("no GitHub Actions workflow files found", file=sys.stderr)
        return 1

    errors = validate_files(paths, root)
    if errors:
        print("GitHub Actions workflow validation failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(f"GitHub Actions workflow validation passed: {len(paths)} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
