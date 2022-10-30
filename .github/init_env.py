import os
import sys
from enum import Enum


class BuildTarget(Enum):
    SIMUTRANS = "simutrans"
    NETTOOL = "nettool"
    MAKEOBJ = "makeobj"


class Backend(Enum):
    GDI = "gdi"
    SDL2 = "sdl2"
    POSIX = "posix"


class BuildType(Enum):
    RELEASE = "Release"
    DEBUG = "Debug"


class TargetSystem(Enum):
    LINUX = "linux"
    MAC = "mac"
    # ANDROID = "android"
    WINDOWS = "windows"
    MINGW = "mingw"


# Pipeline yoloism
target = BuildTarget(sys.argv[1])
backend = Backend(sys.argv[2])
build_type = BuildType(sys.argv[3])
target_system = TargetSystem(sys.argv[4])

artifact_name = f"{target.value}-{target_system.value}"
artifact_name += f"-{backend.value}" if target == BuildTarget.SIMUTRANS else ""
# Assemble the path of the resulting binary. This is quite a mess.
# The path will be something like
# build/{target}/{build_type}/{target}.exe
# where some sections might be missing or renamed in some cases depending on the target

artifact_path = "build/"
artifact_path += "nettools/" if target == BuildTarget.NETTOOL else f"{target.value}/"
match target_system:
    case TargetSystem.WINDOWS if target != BuildTarget.SIMUTRANS:
        artifact_path += f"{build_type.value}/{target.value}-extended.exe"
    case TargetSystem.WINDOWS | TargetSystem.MINGW:
        artifact_path += f"{target.value}-extended.exe"
    case TargetSystem.LINUX | TargetSystem.MAC:
        artifact_path += f"{target.value}-extended"

with open(os.getenv('GITHUB_ENV'), "w") as gh_env:
    gh_env.write(f"st_artifact_name={artifact_name}\n")
    gh_env.write(f"st_artifact_path={artifact_path}\n")
