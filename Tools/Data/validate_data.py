# Copyright LEFTRIGHT. All Rights Reserved.
"""Offline validator for LEFTRIGHT design data (Data/**/*.json).

Checks, without the engine:
  1. Every file is valid UTF-8 JSON.
  2. The envelope matches Data/Schema/Envelope.schema.json ({"type", "id", "version", "data"}).
  3. "data" matches Data/Schema/<type>.schema.json (JSON Schema draft 2020-12) when that schema exists
     (a missing schema is a warning, or an error with --strict).
  4. Every schema file under Data/Schema is itself a valid draft 2020-12 schema.
  5. No two files share the same (type, id) pair.

Skipped: Data/Schema/** and any folder whose name starts with '_' or '.' (scratch / automation temp files).

Exit code: 0 when there are no errors, 1 otherwise. Designed to run in isolated mode: python -I validate_data.py
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
	from jsonschema import Draft202012Validator
	from jsonschema.exceptions import SchemaError
except ImportError:  # pragma: no cover - environment problem, reported to the user
	print("ERROR: the 'jsonschema' package is missing. Install it with:", file=sys.stderr)
	print("    python -m pip install -r Tools/Data/requirements.txt", file=sys.stderr)
	sys.exit(1)

try:
	from referencing import Registry, Resource
	from referencing.jsonschema import DRAFT202012

	HAVE_REFERENCING = True
except ImportError:  # jsonschema < 4.18: cross-file $ref is unavailable, everything else works.
	HAVE_REFERENCING = False

SCHEMA_BASE_URI = "https://leftright.local/schema/"
ENVELOPE_SCHEMA_NAME = "Envelope"
SCHEMA_SUFFIX = ".schema.json"


@dataclass
class Report:
	"""Accumulates findings per file and prints them at the end."""

	errors: list[str] = field(default_factory=list)
	warnings: list[str] = field(default_factory=list)
	files_ok: int = 0
	files_checked: int = 0

	def error(self, where: str, message: str) -> None:
		self.errors.append(f"{where}: {message}")

	def warn(self, where: str, message: str) -> None:
		self.warnings.append(f"{where}: {message}")


def relative(path: Path, root: Path) -> str:
	"""Repository-relative path with forward slashes, for readable reports."""
	try:
		return path.relative_to(root).as_posix()
	except ValueError:
		return path.as_posix()


def load_json(path: Path) -> tuple[Any, str | None]:
	"""Loads a JSON file. Returns (value, None) or (None, error message)."""
	try:
		raw = path.read_bytes()
	except OSError as exc:
		return None, f"cannot read file ({exc})"
	if raw.startswith(b"\xef\xbb\xbf"):
		raw = raw[3:]
	try:
		text = raw.decode("utf-8")
	except UnicodeDecodeError as exc:
		return None, f"not valid UTF-8 ({exc})"
	try:
		return json.loads(text), None
	except json.JSONDecodeError as exc:
		return None, f"malformed JSON at line {exc.lineno}, column {exc.colno}: {exc.msg}"


def format_error_path(error: Any, prefix: str) -> str:
	"""Turns a jsonschema error location into a readable path like data.Tiers[2].Multiplier."""
	parts = [prefix]
	for element in error.absolute_path:
		if isinstance(element, int):
			parts.append(f"[{element}]")
		else:
			parts.append(f".{element}")
	return "".join(parts)


class SchemaStore:
	"""Loads, checks and caches every Data/Schema/*.schema.json file."""

	def __init__(self, schema_dir: Path, repo_root: Path, report: Report) -> None:
		self.schema_dir = schema_dir
		self.repo_root = repo_root
		self.report = report
		self.schemas: dict[str, dict[str, Any]] = {}
		self.validators: dict[str, Draft202012Validator] = {}
		self.registry = None
		self._load_all()

	def _load_all(self) -> None:
		if not self.schema_dir.is_dir():
			self.report.error(relative(self.schema_dir, self.repo_root), "schema folder not found")
			return

		resources = []
		for path in sorted(self.schema_dir.glob(f"*{SCHEMA_SUFFIX}")):
			where = relative(path, self.repo_root)
			name = path.name[: -len(SCHEMA_SUFFIX)]
			schema, problem = load_json(path)
			if problem:
				self.report.error(where, problem)
				continue
			if not isinstance(schema, dict):
				self.report.error(where, "a schema must be a JSON object")
				continue
			declared = schema.get("$schema")
			if declared not in (None, "https://json-schema.org/draft/2020-12/schema"):
				self.report.warn(where, f"$schema is '{declared}', expected draft 2020-12")
			try:
				Draft202012Validator.check_schema(schema)
			except SchemaError as exc:
				self.report.error(where, f"invalid JSON Schema: {exc.message}")
				continue

			# Give every schema a stable URI so relative cross-file $ref ("Common.schema.json#/$defs/X") resolves.
			schema = dict(schema)
			schema.setdefault("$id", SCHEMA_BASE_URI + path.name)
			self.schemas[name] = schema
			if HAVE_REFERENCING:
				resources.append((schema["$id"], Resource.from_contents(schema, default_specification=DRAFT202012)))

		if HAVE_REFERENCING:
			self.registry = Registry().with_resources(resources)

	def has(self, name: str) -> bool:
		return name in self.schemas

	def validator(self, name: str) -> Draft202012Validator | None:
		"""Cached validator for a schema name (file stem without .schema.json)."""
		if name not in self.schemas:
			return None
		if name not in self.validators:
			if self.registry is not None:
				self.validators[name] = Draft202012Validator(self.schemas[name], registry=self.registry)
			else:
				self.validators[name] = Draft202012Validator(self.schemas[name])
		return self.validators[name]


def iter_data_files(data_dir: Path, schema_dir: Path) -> list[Path]:
	"""Every *.json under Data/, excluding Data/Schema and folders starting with '_' or '.'."""
	result = []
	for path in data_dir.rglob("*.json"):
		rel_parts = path.relative_to(data_dir).parts
		if path.is_relative_to(schema_dir):
			continue
		if any(part.startswith(("_", ".")) for part in rel_parts[:-1]):
			continue
		if path.is_file():
			result.append(path)
	return sorted(result, key=lambda p: p.as_posix().lower())


def validate_file(path: Path, store: SchemaStore, report: Report, seen: dict[tuple[str, str], str], strict: bool) -> None:
	"""Validates one data file and records its (type, id)."""
	where = relative(path, store.repo_root)
	report.files_checked += 1
	errors_before = len(report.errors)

	document, problem = load_json(path)
	if problem:
		report.error(where, problem)
		return

	envelope_validator = store.validator(ENVELOPE_SCHEMA_NAME)
	if envelope_validator is None:
		report.error(where, f"cannot validate envelope: Data/Schema/{ENVELOPE_SCHEMA_NAME}{SCHEMA_SUFFIX} is missing or invalid")
		return

	envelope_errors = sorted(envelope_validator.iter_errors(document), key=lambda e: [str(p) for p in e.absolute_path])
	for error in envelope_errors:
		report.error(where, f"envelope {format_error_path(error, '$')}: {error.message}")
	if envelope_errors or not isinstance(document, dict):
		return

	type_name = document["type"]
	data_id = document["id"]

	key = (type_name, data_id)
	if key in seen:
		report.error(where, f"duplicate (type, id) = ({type_name}, {data_id}); first defined in {seen[key]}")
	else:
		seen[key] = where

	if type_name == ENVELOPE_SCHEMA_NAME:
		report.error(where, f"'{ENVELOPE_SCHEMA_NAME}' is reserved and cannot be used as a data type")
		return

	data_validator = store.validator(type_name)
	if data_validator is None:
		message = f"no schema Data/Schema/{type_name}{SCHEMA_SUFFIX}; 'data' not validated"
		if strict:
			report.error(where, message)
		else:
			report.warn(where, message)
	else:
		data_errors = sorted(data_validator.iter_errors(document["data"]), key=lambda e: [str(p) for p in e.absolute_path])
		for error in data_errors:
			report.error(where, f"{format_error_path(error, 'data')}: {error.message}")

	if len(report.errors) == errors_before:
		report.files_ok += 1


def main(argv: list[str] | None = None) -> int:
	default_root = Path(__file__).resolve().parents[2]
	parser = argparse.ArgumentParser(description="Validate LEFTRIGHT Data/**/*.json against Data/Schema.")
	parser.add_argument("--repo-root", type=Path, default=default_root, help="repository root (default: inferred from this script)")
	parser.add_argument("--data-dir", type=Path, default=None, help="data folder (default: <repo-root>/Data)")
	parser.add_argument("--strict", action="store_true", help="treat warnings (missing schema) as errors")
	parser.add_argument("--quiet", action="store_true", help="only print errors and the summary")
	args = parser.parse_args(argv)

	repo_root = args.repo_root.resolve()
	data_dir = (args.data_dir or repo_root / "Data").resolve()
	schema_dir = data_dir / "Schema"

	report = Report()
	if not data_dir.is_dir():
		print(f"ERROR: data folder not found: {data_dir}")
		return 1

	store = SchemaStore(schema_dir, repo_root, report)
	if not HAVE_REFERENCING:
		report.warn("validator", "the 'referencing' package is unavailable; cross-file $ref will not resolve (jsonschema >= 4.18 recommended)")

	files = iter_data_files(data_dir, schema_dir)
	seen: dict[tuple[str, str], str] = {}
	for path in files:
		validate_file(path, store, report, seen, args.strict)

	if not files:
		report.warn(relative(data_dir, repo_root), "no data files found")

	print("LEFTRIGHT data validation")
	print(f"  data folder : {data_dir}")
	print(f"  schemas     : {len(store.schemas)} loaded from {relative(schema_dir, repo_root)}")
	print(f"  files       : {report.files_checked} checked, {report.files_ok} OK")
	if report.warnings and not args.quiet:
		print("")
		print(f"Warnings ({len(report.warnings)}):")
		for line in report.warnings:
			print(f"  WARN  {line}")
	if report.errors:
		print("")
		print(f"Errors ({len(report.errors)}):")
		for line in report.errors:
			print(f"  ERROR {line}")
	print("")
	if report.errors:
		print(f"FAILED: {len(report.errors)} error(s), {len(report.warnings)} warning(s).")
		return 1
	print(f"PASSED: 0 errors, {len(report.warnings)} warning(s).")
	return 0


if __name__ == "__main__":
	sys.exit(main())
