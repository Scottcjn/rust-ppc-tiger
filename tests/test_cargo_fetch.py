import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from cargo_fetch import (
    CrateDep,
    generate_build_manifest,
    parse_cargo_lock,
    parse_crate_features,
    topo_sort,
)


def test_crate_dep_git_source_helpers():
    dep = CrateDep(
        "demo",
        "0.1.0",
        source="git+https://github.com/example/demo?branch=next#abc123",
    )

    assert dep.is_git
    assert not dep.is_registry
    assert dep.git_url == "https://github.com/example/demo"
    assert dep.git_branch == "next"
    assert dep.git_rev == "abc123"


def test_parse_cargo_lock_reads_registry_git_and_local_packages(tmp_path):
    lockfile = tmp_path / "Cargo.lock"
    lockfile.write_text(
        """
version = 3

[[package]]
name = "app"
version = "0.1.0"
dependencies = [
 "serde 1.0.219 (registry+https://github.com/rust-lang/crates.io-index)",
 "gitdep 0.2.0 (git+https://github.com/example/gitdep#abc123)",
]

[[package]]
name = "serde"
version = "1.0.219"
source = "registry+https://github.com/rust-lang/crates.io-index"
checksum = "abc"

[[package]]
name = "gitdep"
version = "0.2.0"
source = "git+https://github.com/example/gitdep#abc123"
""".strip(),
        encoding="utf8",
    )

    crates = {crate.name: crate for crate in parse_cargo_lock(lockfile)}

    assert crates["app"].is_local
    assert crates["app"].dependencies == ["serde", "gitdep"]
    assert crates["serde"].is_registry
    assert crates["serde"].checksum == "abc"
    assert crates["gitdep"].is_git
    assert crates["gitdep"].git_rev == "abc123"


def test_parse_crate_features_reads_feature_and_dependency_sections(tmp_path):
    crate_dir = tmp_path / "demo-0.1.0"
    crate_dir.mkdir()
    (crate_dir / "Cargo.toml").write_text(
        """
[features]
default = ["std", "serde/derive"]
alloc = []

[dependencies]
serde = "1"
hashbrown = { version = "0.14", optional = true }

[build-dependencies]
cc = "1"
""".strip(),
        encoding="utf8",
    )

    features, deps = parse_crate_features(crate_dir)

    assert features["default"] == ["std", "serde/derive"]
    assert features["alloc"] == []
    assert deps == ["serde", "hashbrown", "cc"]


def test_topo_sort_orders_dependencies_before_dependents():
    app = CrateDep("app", "0.1.0")
    app.dependencies = ["serde", "utils"]
    utils = CrateDep("utils", "0.1.0")
    utils.dependencies = ["serde"]
    serde = CrateDep("serde", "1.0.219")

    order = topo_sort([app, utils, serde])

    assert order.index("serde") < order.index("utils")
    assert order.index("utils") < order.index("app")


def test_generate_build_manifest_counts_sources_and_detects_targets(tmp_path):
    vendor_dir = tmp_path / "vendor"
    crate_dir = vendor_dir / "demo-0.1.0"
    (crate_dir / "src" / "nested").mkdir(parents=True)
    (crate_dir / "src" / "lib.rs").write_text("pub fn demo() {}\n", encoding="utf8")
    (crate_dir / "src" / "nested" / "mod.rs").write_text("", encoding="utf8")

    dep = CrateDep(
        "demo",
        "0.1.0",
        source="registry+https://github.com/rust-lang/crates.io-index",
    )
    dep.dependencies = ["serde"]
    output_path = tmp_path / "manifest.json"

    manifest = generate_build_manifest([dep], str(vendor_dir), output_path)
    saved = json.loads(output_path.read_text(encoding="utf8"))
    entry = manifest["crates"][0]

    assert saved["total_crates"] == 1
    assert entry["source_type"] == "registry"
    assert entry["source_files"] == ["src/lib.rs", "src/nested/mod.rs"]
    assert entry["source_count"] == 2
    assert entry["is_lib"] is True
    assert entry["is_bin"] is False
    assert manifest["total_source_files"] == 2
