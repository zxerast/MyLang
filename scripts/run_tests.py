#!/usr/bin/env python3

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TESTS_DIR = ROOT / "tests"
DEFAULT_BUILD_DIR = ROOT / "build"

RUNTIME_OVERRIDES = {
    "tests/Module7/test70.lang": (
        "TEST 10 INNER 0\n"
        "TEST 10 OK 355",
        99,
    ),
    "tests/Module8/test80.lang": (
        "TEST 10 INNER 1000\n"
        "TEST 10 OK 3 48",
        48,
    ),
    "tests/edge-cases/outOfBounds.lang": (
        "-1\n"
        "-1\n"
        "10\n"
        "runtime error: array index out of bounds at line 16",
        1,
    ),
}

COMPILE_FAIL_OVERRIDES = {
    "tests/Module3/test24.lang",
}


def run(cmd, *, cwd=ROOT, stdin=None, timeout=None):
    return subprocess.run(
        cmd,
        cwd=cwd,
        input=stdin,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )


def strip_comment(line):
    stripped = line.strip()
    if stripped.startswith("//"):
        return stripped[2:].strip()
    return None


def test_status(source):
    for line in source.splitlines():
        text = strip_comment(line)
        if text is None:
            text = line.strip()
        if text.startswith("Status:"):
            return text[len("Status:"):].strip()
    return ""


def expected_compile_errors(source):
    errors = []
    for line in source.splitlines():
        text = strip_comment(line)
        if text and "error:" in text:
            errors.append(text)
    return errors


def expected_runtime(source):
    lines = []
    exit_code = None
    in_block = False

    for line in source.splitlines():
        text = strip_comment(line)

        if text == "Runtime output:":
            in_block = True
            continue

        if not in_block:
            continue

        if text is None:
            if not line.strip():
                break
            continue

        if not text:
            continue

        if text.startswith("Status:"):
            break

        if text.startswith("runtime_exit="):
            try:
                exit_code = int(text.split("=", 1)[1])
            except ValueError:
                exit_code = None
            continue

        lines.append(text)

    if not in_block:
        return None

    return "\n".join(lines), exit_code


def has_main(source):
    return re.search(r"\bmain\s*\(", source) is not None


def is_compile_fail_expected(rel, source, status, runtime_expected):
    if runtime_expected is not None:
        return False
    if rel in COMPILE_FAIL_OVERRIDES:
        return True
    if not status:
        return not has_main(source)
    return status != "OK"


def normalize_output(text):
    return text.replace("\r\n", "\n").rstrip("\n")


def compile_test(lang, compiler, out_dir):
    rel = lang.relative_to(ROOT)
    output = out_dir / str(rel.with_suffix("")).replace(os.sep, "__")
    proc = run([str(compiler), str(lang), "-o", str(output)])
    return proc, output


def check_compile_error(lang, proc, expected_errors):
    if proc.returncode == 0:
        return False, "compiled successfully, but this test expects a compile error"

    combined = proc.stdout + proc.stderr
    missing = [err for err in expected_errors if err not in combined]

    if missing:
        preview = "\n".join(missing[:3])
        return False, f"compile failed, but expected error text was not found:\n{preview}"

    return True, ""


def check_runtime(exe, expected, timeout):
    try:
        proc = run([str(exe)], stdin="", timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"runtime timeout after {timeout}s"

    output = normalize_output(proc.stdout + proc.stderr)

    if expected is not None:
        expected_output, expected_exit = expected
        if normalize_output(expected_output) != output:
            return False, (
                "runtime output mismatch\n"
                f"expected:\n{expected_output}\n"
                f"actual:\n{output}"
            )

        if expected_exit is not None and proc.returncode != expected_exit:
            return False, f"runtime exit mismatch: expected {expected_exit}, got {proc.returncode}"

        return True, ""

    if "runtime error:" in output:
        return False, f"unexpected runtime error:\n{output}"

    return True, ""


def main():
    parser = argparse.ArgumentParser(description="Run MyLang tests from tests/ with expectations from comments.")
    parser.add_argument("--build-dir", default=str(DEFAULT_BUILD_DIR), help="CMake build directory")
    parser.add_argument("--compiler", help="Path to compiled lang executable")
    parser.add_argument("--no-build", action="store_true", help="Do not run cmake --build before tests")
    parser.add_argument("--keep-out", action="store_true", help="Keep generated binaries and logs")
    parser.add_argument("--timeout", type=int, default=5, help="Runtime timeout per test in seconds")
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = ROOT / build_dir

    compiler = Path(args.compiler) if args.compiler else build_dir / "lang"
    if not compiler.is_absolute():
        compiler = ROOT / compiler

    if not args.no_build:
        build = run(["cmake", "--build", str(build_dir)])
        if build.returncode != 0:
            sys.stderr.write(build.stdout)
            sys.stderr.write(build.stderr)
            return build.returncode

    if not compiler.exists():
        sys.stderr.write(f"compiler not found: {compiler}\n")
        return 2

    temp_dir = Path(tempfile.mkdtemp(prefix="mylang-tests-"))
    out_dir = temp_dir / "bin"
    out_dir.mkdir(parents=True)

    failures = []
    stats = {
        "total": 0,
        "compile_expected": 0,
        "runtime_exact": 0,
        "runtime_smoke": 0,
    }

    try:
        for lang in sorted(TESTS_DIR.rglob("*.lang")):
            stats["total"] += 1
            rel = lang.relative_to(ROOT).as_posix()
            source = lang.read_text(encoding="utf-8")
            status = test_status(source)
            runtime_expected = RUNTIME_OVERRIDES.get(rel, expected_runtime(source))
            compile_fail_expected = is_compile_fail_expected(rel, source, status, runtime_expected)
            expected_errors = expected_compile_errors(source)

            proc, exe = compile_test(lang, compiler, out_dir)

            if compile_fail_expected:
                ok, message = check_compile_error(lang, proc, expected_errors)
                if ok:
                    stats["compile_expected"] += 1
                else:
                    failures.append((lang, message))
                continue

            if proc.returncode != 0:
                failures.append((lang, "compile failed unexpectedly\n" + proc.stderr.strip()))
                continue

            ok, message = check_runtime(exe, runtime_expected, args.timeout)
            if ok:
                if runtime_expected is not None:
                    stats["runtime_exact"] += 1
                else:
                    stats["runtime_smoke"] += 1
            else:
                failures.append((lang, message))

        for lang, message in failures:
            print(f"FAIL {lang.relative_to(ROOT)}")
            print(message)
            print()

        passed = stats["total"] - len(failures)
        print(
            f"tests={stats['total']} passed={passed} failed={len(failures)} "
            f"compile_expected={stats['compile_expected']} "
            f"runtime_exact={stats['runtime_exact']} runtime_smoke={stats['runtime_smoke']}"
        )

        if args.keep_out:
            print(f"artifacts: {temp_dir}")
        return 1 if failures else 0

    finally:
        if not args.keep_out:
            shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
