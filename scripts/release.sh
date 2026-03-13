#!/bin/bash
set -euo pipefail

# AutoMix Engine one-click release script.
# What it does:
# 1) Run preflight checks (branch / clean workspace / semver / tag conflict)
# 2) Bump version in CMakeLists.txt and project.yml
# 3) Increment CURRENT_PROJECT_VERSION automatically
# 4) Build verification
# 5) Commit + annotated tag + push

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CMAKE_FILE="$PROJECT_ROOT/CMakeLists.txt"
XCODEGEN_FILE="$PROJECT_ROOT/apple/Apps/AutomixMac/project.yml"

VERSION=""
BRANCH=""
REMOTE="origin"
NO_PUSH=0
SKIP_BUILD=0
YES=0

log() { echo "[release] $*"; }
err() { echo "[release] ERROR: $*" >&2; }

usage() {
  cat <<'EOF'
Usage:
  ./scripts/release.sh --version X.Y.Z [options]

Required:
  --version X.Y.Z        Target SemVer version (e.g. 1.1.0)

Options:
  --branch NAME          Compatibility option; must equal remote default branch
  --remote NAME          Remote name (default: origin)
  --no-push              Create commit/tag only, do not push
  --skip-build           Skip local build verification
  -y, --yes              Non-interactive mode (auto confirm)
  -h, --help             Show this help

Behavior:
  - Validates clean git workspace and current branch
  - Release is only allowed from the remote default branch
  - Rejects existing local/remote tag vX.Y.Z
  - Updates:
      CMakeLists.txt                      project(... VERSION X.Y.Z)
      apple/Apps/AutomixMac/project.yml   MARKETING_VERSION: X.Y.Z
      apple/Apps/AutomixMac/project.yml   CURRENT_PROJECT_VERSION: +1
  - Builds cmake-build (unless --skip-build)
  - Commits, tags, and pushes
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="${2:-}"
      shift 2
      ;;
    --branch)
      BRANCH="${2:-}"
      shift 2
      ;;
    --remote)
      REMOTE="${2:-}"
      shift 2
      ;;
    --no-push)
      NO_PUSH=1
      shift
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    -y|--yes)
      YES=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      err "Unknown argument: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$VERSION" ]]; then
  err "--version is required"
  usage
  exit 1
fi

if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  err "Invalid version '$VERSION'. Expected SemVer format: X.Y.Z"
  exit 1
fi

if ! command -v git >/dev/null 2>&1; then
  err "git is required"
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  err "python3 is required"
  exit 1
fi

cd "$PROJECT_ROOT"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  err "Current directory is not a git repository: $PROJECT_ROOT"
  exit 1
fi

if ! git diff --quiet || ! git diff --cached --quiet; then
  err "Working tree is not clean. Commit/stash changes before release."
  exit 1
fi

if [[ ! -f "$CMAKE_FILE" || ! -f "$XCODEGEN_FILE" ]]; then
  err "Required file missing. Need both:"
  err "  - $CMAKE_FILE"
  err "  - $XCODEGEN_FILE"
  exit 1
fi

if ! git remote get-url "$REMOTE" >/dev/null 2>&1; then
  err "Remote '$REMOTE' does not exist."
  exit 1
fi

DEFAULT_BRANCH_REF="$(git symbolic-ref --quiet --short "refs/remotes/$REMOTE/HEAD" 2>/dev/null || true)"
if [[ -n "$DEFAULT_BRANCH_REF" ]]; then
  PRIMARY_BRANCH="${DEFAULT_BRANCH_REF#"$REMOTE/"}"
elif git show-ref --verify --quiet "refs/remotes/$REMOTE/main"; then
  PRIMARY_BRANCH="main"
elif git show-ref --verify --quiet "refs/remotes/$REMOTE/master"; then
  PRIMARY_BRANCH="master"
else
  err "Unable to detect $REMOTE remote default branch."
  err "Set remote HEAD first, or ensure $REMOTE/main or $REMOTE/master exists."
  exit 1
fi

if [[ -n "$BRANCH" && "$BRANCH" != "$PRIMARY_BRANCH" ]]; then
  err "Release is only allowed from the remote default branch: '$PRIMARY_BRANCH'."
  err "Provided --branch '$BRANCH' is not allowed."
  exit 1
fi

BRANCH="$PRIMARY_BRANCH"
log "Using remote default branch: $BRANCH"

CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [[ "$CURRENT_BRANCH" != "$BRANCH" ]]; then
  err "Current branch is '$CURRENT_BRANCH', expected remote default branch '$BRANCH'"
  err "Switch branch first: git checkout $BRANCH"
  exit 1
fi

TARGET_TAG="v$VERSION"

if git rev-parse -q --verify "refs/tags/$TARGET_TAG" >/dev/null; then
  err "Local tag already exists: $TARGET_TAG"
  exit 1
fi

log "Fetching tags from $REMOTE ..."
git fetch --tags "$REMOTE"

if git ls-remote --exit-code --tags "$REMOTE" "refs/tags/$TARGET_TAG" >/dev/null 2>&1; then
  err "Remote tag already exists: $TARGET_TAG"
  exit 1
fi

if ! git fetch "$REMOTE" "$BRANCH" >/dev/null 2>&1; then
  err "Failed to fetch $REMOTE/$BRANCH"
  exit 1
fi

LOCAL_HEAD="$(git rev-parse HEAD)"
REMOTE_HEAD="$(git rev-parse "$REMOTE/$BRANCH")"
if [[ "$LOCAL_HEAD" != "$REMOTE_HEAD" ]]; then
  err "Local branch is not up-to-date with $REMOTE/$BRANCH."
  err "Run: git pull --ff-only $REMOTE $BRANCH"
  exit 1
fi

VERSION_INFO_RAW="$(python3 - "$CMAKE_FILE" "$XCODEGEN_FILE" "$VERSION" <<'PY'
import pathlib
import re
import sys

cmake_file = pathlib.Path(sys.argv[1])
xcodegen_file = pathlib.Path(sys.argv[2])
target_version = sys.argv[3]

cmake_text = cmake_file.read_text(encoding="utf-8")
yml_text = xcodegen_file.read_text(encoding="utf-8")

cmake_match = re.search(r"project\s*\(\s*automix\b[^)]*\bVERSION\s+(\d+\.\d+\.\d+)", cmake_text, flags=re.DOTALL)
if not cmake_match:
    print("ERR:CMakeLists.txt does not contain 'project(automix VERSION X.Y.Z ...)'")
    sys.exit(1)
cmake_version = cmake_match.group(1)

marketing_match = re.search(r"(^\s*MARKETING_VERSION:\s*)(\d+\.\d+\.\d+)\s*$", yml_text, flags=re.M)
if not marketing_match:
    print("ERR:project.yml does not contain MARKETING_VERSION")
    sys.exit(1)
marketing_version = marketing_match.group(2)

build_match = re.search(r"(^\s*CURRENT_PROJECT_VERSION:\s*)(\d+)\s*$", yml_text, flags=re.M)
if not build_match:
    print("ERR:project.yml does not contain CURRENT_PROJECT_VERSION")
    sys.exit(1)
current_build = int(build_match.group(2))
next_build = current_build + 1

def parse_semver(v: str):
    return tuple(int(x) for x in v.split("."))

if cmake_version != marketing_version:
    print(f"ERR:version skew: CMakeLists.txt has {cmake_version} but project.yml has {marketing_version}. Sync them before releasing.")
    sys.exit(1)

if parse_semver(target_version) <= parse_semver(marketing_version):
    print(f"ERR:target version {target_version} must be greater than current MARKETING_VERSION {marketing_version}")
    sys.exit(1)

print(cmake_version)
print(marketing_version)
print(str(current_build))
print(str(next_build))
PY
)"

VERSION_INFO_LINE_1="$(printf '%s\n' "$VERSION_INFO_RAW" | sed -n '1p')"
VERSION_INFO_LINE_2="$(printf '%s\n' "$VERSION_INFO_RAW" | sed -n '2p')"
VERSION_INFO_LINE_3="$(printf '%s\n' "$VERSION_INFO_RAW" | sed -n '3p')"
VERSION_INFO_LINE_4="$(printf '%s\n' "$VERSION_INFO_RAW" | sed -n '4p')"

if [[ "${VERSION_INFO_LINE_1:-}" == ERR:* ]]; then
  err "${VERSION_INFO_LINE_1#ERR:}"
  exit 1
fi

CURRENT_CMAKE_VERSION="${VERSION_INFO_LINE_1}"
CURRENT_MARKETING_VERSION="${VERSION_INFO_LINE_2}"
CURRENT_BUILD_VERSION="${VERSION_INFO_LINE_3}"
NEXT_BUILD_VERSION="${VERSION_INFO_LINE_4}"

log "Current CMake version      : $CURRENT_CMAKE_VERSION"
log "Current MARKETING_VERSION  : $CURRENT_MARKETING_VERSION"
log "Current CURRENT_PROJECT_VERSION: $CURRENT_BUILD_VERSION"
log "Target release version     : $VERSION"
log "Next build number          : $NEXT_BUILD_VERSION"

if [[ "$YES" -ne 1 ]]; then
  echo
  read -r -p "Continue release? [y/N] " answer
  case "$answer" in
    y|Y|yes|YES) ;;
    *)
      log "Cancelled."
      exit 0
      ;;
  esac
fi

python3 - "$CMAKE_FILE" "$XCODEGEN_FILE" "$VERSION" "$NEXT_BUILD_VERSION" <<'PY'
import pathlib
import re
import sys

cmake_file = pathlib.Path(sys.argv[1])
xcodegen_file = pathlib.Path(sys.argv[2])
target_version = sys.argv[3]
next_build = sys.argv[4]

cmake_text = cmake_file.read_text(encoding="utf-8")
yml_text = xcodegen_file.read_text(encoding="utf-8")

cmake_text_new = re.sub(
    r"(project\(\s*automix\s+VERSION\s+)(\d+\.\d+\.\d+)(\s+LANGUAGES\s+CXX\s+C\s*\))",
    rf"\g<1>{target_version}\g<3>",
    cmake_text,
    count=1,
)

yml_text_new = re.sub(
    r"(^\s*MARKETING_VERSION:\s*)(\d+\.\d+\.\d+)\s*$",
    rf"\g<1>{target_version}",
    yml_text,
    count=1,
    flags=re.M,
)

yml_text_new = re.sub(
    r"(^\s*CURRENT_PROJECT_VERSION:\s*)(\d+)\s*$",
    rf"\g<1>{next_build}",
    yml_text_new,
    count=1,
    flags=re.M,
)

cmake_file.write_text(cmake_text_new, encoding="utf-8")
xcodegen_file.write_text(yml_text_new, encoding="utf-8")
PY

log "Version files updated."

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  if [[ ! -d "$PROJECT_ROOT/cmake-build" ]]; then
    err "cmake-build directory not found. Build check failed."
    err "Create it first, or rerun with --skip-build."
    exit 1
  fi

  if ! command -v cmake >/dev/null 2>&1; then
    err "cmake not found. Build check failed."
    exit 1
  fi

  CPU_COUNT="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  log "Running build verification ..."
  cmake --build "$PROJECT_ROOT/cmake-build" -j"$CPU_COUNT"
fi

git add "$CMAKE_FILE" "$XCODEGEN_FILE"
git commit -m "chore(release): bump version to $VERSION"
git tag -a "$TARGET_TAG" -m "release: $TARGET_TAG"

if [[ "$NO_PUSH" -eq 0 ]]; then
  git push "$REMOTE" "$BRANCH"
  git push "$REMOTE" "$TARGET_TAG"
  log "Release completed and pushed: $TARGET_TAG"
else
  log "Release commit/tag created locally (push skipped)."
  log "Manual push:"
  log "  git push $REMOTE $BRANCH"
  log "  git push $REMOTE $TARGET_TAG"
fi

log "Done."
