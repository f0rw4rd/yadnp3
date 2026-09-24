#!/usr/bin/env python3
"""
Version management for opendnp3 Python bindings.

Format: OPENDNP3_VERSION.REVISION
- OPENDNP3_VERSION: The opendnp3 C++ library version (3.1.2)
- REVISION: Python package revision for that library version

Example: 3.2.1.2 (opendnp3 v3.2.1, second package revision)
"""

import os
import subprocess
import sys

# Static version configuration
OPENDNP3_VERSION = "3.2.1"
PACKAGE_REVISION = 2


def get_git_tag_version():
    """Extract version from git tag if available."""
    try:
        result = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match", "HEAD"],
            cwd=os.path.dirname(__file__) or ".",
            stderr=subprocess.DEVNULL,
        )
        tag = result.decode().strip()
        if tag.startswith("v"):
            return tag[1:]
        return tag
    except (subprocess.CalledProcessError, FileNotFoundError, ValueError):
        return None


def get_version():
    """Generate the package version string."""
    git_version = get_git_tag_version()
    if git_version:
        return git_version
    return f"{OPENDNP3_VERSION}.{PACKAGE_REVISION}"


def get_opendnp3_version():
    """Get the opendnp3 C++ library version string."""
    return OPENDNP3_VERSION


if __name__ == "__main__":
    if len(sys.argv) > 1:
        if sys.argv[1] == "--lib":
            print(get_opendnp3_version())
        elif sys.argv[1] == "--help":
            print("Usage:")
            print("  python version.py          # Package version (3.2.1.2)")
            print("  python version.py --lib    # C++ library version (3.2.1)")
        else:
            print("Unknown option. Use --help for usage.")
    else:
        print(get_version())
