"""Compile literal EdgeHover methods against a small offline input host."""
from pathlib import Path
import argparse
import subprocess
import shlex

parser = argparse.ArgumentParser()
parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument("--build-dir", type=Path, required=True)
parser.add_argument("--cxx", default="c++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
root = args.source_root.resolve()
fixtures = Path(__file__).resolve().parent
src = (root / "src/edgehover.cpp").read_text()
hdr = (root / "src/edgehover.hpp").read_text()
helpers = src[src.index("Rect boxToRect("):src.index("bool noFocus(")]
methods = src[src.index("void EdgeHover::clearSynthetic()"):]
cls = hdr[hdr.index("namespace hypr_edgehover {"):].replace("  private:", "  public:")
destructor = src[src.index("EdgeHover::~EdgeHover()"):src.index("bool EdgeHover::initialize()")]
code = ((fixtures / "input_host.hpp").read_text() + cls +
        "\nnamespace hypr_edgehover { namespace {\n" + helpers + "} }\n" +
        (fixtures / "input_bridge.hpp").read_text() + destructor + methods +
        (fixtures / "input_cases.cpp").read_text())
args.build_dir.mkdir(parents=True, exist_ok=True)
cpp = args.build_dir / "input_regression.cpp"
exe = args.build_dir / "input_regression"
cpp.write_text(code)
flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "wayland-server"], text=True))
sanitizers = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
subprocess.run([args.cxx, "-std=c++23", "-O0", "-g", *sanitizers, "-I" + str(root / "src"),
                str(cpp), str(root / "src/geometry.cpp"), "-o", str(exe), *flags], check=True)
subprocess.run([str(exe)], check=True)
