#!/usr/bin/env python3

import ast
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from xml.etree import ElementTree


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = REPOSITORY_ROOT / "THIRD_PARTY/font_awesome_eye.toml"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def load_manifest() -> dict[str, object]:
    manifest = {}
    for line_number, line in enumerate(MANIFEST_PATH.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        key, separator, encoded_value = stripped.partition("=")
        require(bool(separator), f"Manifest line {line_number} has no value separator.")
        key = key.strip()
        require(key.isidentifier(), f"Manifest line {line_number} has an invalid key.")
        require(key not in manifest, f"Manifest key {key} is duplicated.")
        try:
            manifest[key] = ast.literal_eval(encoded_value.strip())
        except (SyntaxError, ValueError) as error:
            raise RuntimeError(f"Manifest value for {key} is not a scalar literal.") from error
    return manifest


def verify_manifest(manifest: dict[str, object]) -> bytes:
    output_path = REPOSITORY_ROOT / str(manifest["output_path"])
    output_data = output_path.read_bytes()
    require(sha256(output_data) == manifest["output_sha256"], "Output hash drifted.")
    require(len(output_data) == manifest["output_size"], "Output size drifted.")

    recipe_path = REPOSITORY_ROOT / str(manifest["recipe"])
    require(
        sha256(recipe_path.read_bytes()) == manifest["recipe_sha256"],
        "Recipe hash drifted.",
    )

    license_path = REPOSITORY_ROOT / str(manifest["license_text"])
    require(
        sha256(license_path.read_bytes()) == manifest["license_text_sha256"],
        "License-text hash drifted.",
    )
    require(manifest["spdx_license_expression"] == "CC-BY-4.0", "License drifted.")
    require(manifest["compliance_status"] == "resolved", "Provenance is unresolved.")
    return output_data


def verify_svg(manifest: dict[str, object], output_data: bytes) -> None:
    root = ElementTree.fromstring(output_data)
    namespace = "{http://www.w3.org/2000/svg}"
    paths = root.findall(f"{namespace}path")
    require(root.attrib.get("viewBox") == manifest["view_box"], "viewBox drifted.")
    require(len(paths) == 1, "Expected exactly one SVG path.")
    require(paths[0].attrib.get("fill") == "#ffffff", "Product fill must stay white.")
    geometry = paths[0].attrib.get("d", "").encode("utf-8")
    require(sha256(geometry) == manifest["geometry_sha256"], "Path geometry drifted.")


def verify_recipe_twice(manifest: dict[str, object], output_data: bytes) -> None:
    product_fill = b'fill="#ffffff"'
    upstream_fill = b'fill="currentColor"'
    require(output_data.count(product_fill) == 1, "Expected one product fill.")
    upstream_data = output_data.replace(product_fill, upstream_fill)
    require(sha256(upstream_data) == manifest["input_sha256"], "Upstream hash drifted.")
    require(len(upstream_data) == manifest["input_size"], "Upstream size drifted.")

    recipe_path = REPOSITORY_ROOT / str(manifest["recipe"])
    with tempfile.TemporaryDirectory() as temporary_directory:
        temporary_root = Path(temporary_directory)
        input_path = temporary_root / "eye.svg"
        first_output = temporary_root / "first.svg"
        second_output = temporary_root / "second.svg"
        input_path.write_bytes(upstream_data)

        for output_path in (first_output, second_output):
            subprocess.run(
                [sys.executable, str(recipe_path), str(input_path), str(output_path)],
                check=True,
            )

        require(first_output.read_bytes() == output_data, "First recipe output drifted.")
        require(second_output.read_bytes() == output_data, "Second recipe output drifted.")


def main() -> None:
    manifest = load_manifest()
    output_data = verify_manifest(manifest)
    verify_svg(manifest, output_data)
    verify_recipe_twice(manifest, output_data)
    print("Font Awesome eye provenance is reproducible and internally consistent.")


if __name__ == "__main__":
    main()
