"""Exercise the literal plugin config adapter against a replaceable manager."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument("--build-dir", type=Path, required=True)
parser.add_argument("--cxx", default="c++")
args = parser.parse_args()
header = (args.source_root / "src/plugin_config.hpp").read_text()
body = header[header.index("namespace hypr_edgehover {"):]
fixture = (Path(__file__).parent / "config_host.cpp").read_text()
code = fixture.replace("// PRODUCTION_CONFIG_ADAPTER", body)
args.build_dir.mkdir(parents=True, exist_ok=True)
cpp = args.build_dir / "config_regression.cpp"
exe = args.build_dir / "config_regression"
cpp.write_text(code)
subprocess.run([args.cxx, "-std=c++23", "-O0", "-g", str(cpp), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
