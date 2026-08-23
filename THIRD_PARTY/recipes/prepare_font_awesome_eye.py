#!/usr/bin/env python3

import argparse
import hashlib
import os
from pathlib import Path
import tempfile


EXPECTED_INPUT_SHA256 = "5d4b7b083f52b73884180f1390f73c1e359d2792b115d4dd4b8d84cf2de12bfe"
EXPECTED_OUTPUT_SHA256 = "31958c92e88b29a7e89d998fbc9b811303fb9e8669031e8d17c18d6c1a93219d"
EXPECTED_ATTRIBUTION = (
    b"<!--! Font Awesome Free 7.3.1 by @fontawesome - https://fontawesome.com "
    b"License - https://fontawesome.com/license/free (Icons: CC BY 4.0, "
    b"Fonts: SIL OFL 1.1, Code: MIT License) Copyright 2026 Fonticons, Inc. -->"
)
UPSTREAM_FILL = b'fill="currentColor"'
PRODUCT_FILL = b'fill="#ffffff"'


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def prepare_eye(input_path: Path, output_path: Path) -> None:
    if input_path == output_path:
        raise RuntimeError("Input and output paths must be different.")

    input_data = input_path.read_bytes()
    input_hash = sha256(input_data)
    if input_hash != EXPECTED_INPUT_SHA256:
        raise RuntimeError(
            f"Input SHA-256 mismatch: expected {EXPECTED_INPUT_SHA256}, found {input_hash}."
        )
    if EXPECTED_ATTRIBUTION not in input_data:
        raise RuntimeError("The pinned upstream attribution notice is missing.")
    if input_data.count(UPSTREAM_FILL) != 1:
        raise RuntimeError("The pinned upstream SVG must contain exactly one currentColor fill.")

    output_data = input_data.replace(UPSTREAM_FILL, PRODUCT_FILL)
    output_hash = sha256(output_data)
    if output_hash != EXPECTED_OUTPUT_SHA256:
        raise RuntimeError(
            f"Output SHA-256 mismatch: expected {EXPECTED_OUTPUT_SHA256}, found {output_hash}."
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output_path.parent,
            prefix=f".{output_path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary.write(output_data)
            temporary_path = Path(temporary.name)
        os.replace(temporary_path, output_path)
        temporary_path = None
        print(output_hash)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare the white Font Awesome eye used by the Varinomics mark."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    prepare_eye(arguments.input.resolve(), arguments.output.resolve())


if __name__ == "__main__":
    main()
