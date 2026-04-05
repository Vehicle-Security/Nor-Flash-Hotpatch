Import("env")
from os import getenv
from os import environ
from os.path import exists, join
from shutil import which

env.Append(
    LINKFLAGS=[
        "-mfloat-abi=hard",
        "-mfpu=fpv4-sp-d16",
        "-Wl,--no-warn-mismatch",
    ]
)

if getenv("APP_STARTUP_SMOKE_TEST", "0") == "1":
    env.Append(CPPDEFINES=["APP_STARTUP_SMOKE_TEST=1"])


def _build_firmware_bin(source, target, env):
    elf_path = str(target[0])
    bin_path = join(env.subst("$BUILD_DIR"), f"{env.subst('${PROGNAME}')}.bin")
    env.Execute(
        env.VerboseAction(
            f'"{env.subst("$OBJCOPY")}" -O binary "{elf_path}" "{bin_path}"',
            f"Building {bin_path}",
        )
    )


def _jlink_cmd_script(env):
    build_dir = env.subst("$BUILD_DIR")
    script_path = join(build_dir, "upload.jlink")
    firmware_bin = join(build_dir, f"{env.subst('${PROGNAME}')}.bin")
    commands = [
        f'loadbin "{firmware_bin}",0x0',
        "r",
        "g",
        "q",
    ]

    with open(script_path, "w") as fp:
        fp.write("\n".join(commands))
    return script_path


def _resolve_jlink_exe(env):
    program_files = [
        environ.get("ProgramFiles"),
        environ.get("ProgramFiles(x86)"),
    ]

    for base_dir in program_files:
        if not base_dir:
            continue

        candidate = join(base_dir, "SEGGER", "JLink", "JLink.exe")
        if exists(candidate):
            return candidate

    system_jlink = which("JLink.exe") or which("JLink")
    if system_jlink and "SEGGER" in system_jlink:
        return system_jlink

    package_dir = env.PioPlatform().get_package_dir("tool-jlink")
    if package_dir:
        candidate = join(package_dir, "JLink.exe")
        if exists(candidate):
            return candidate
    return "JLink.exe"


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _build_firmware_bin)
env.Replace(
    UPLOAD_PROTOCOL="custom",
    UPLOADER=_resolve_jlink_exe(env),
    __jlink_cmd_script=_jlink_cmd_script,
    UPLOADERFLAGS=[
        "-device",
        "nRF52840_xxAA",
        "-speed",
        env.GetProjectOption("debug_speed", "4000"),
        "-if",
        "swd",
        "-autoconnect",
        "1",
        "-NoGui",
        "1",
    ],
    UPLOADCMD='"$UPLOADER" $UPLOADERFLAGS -CommanderScript "${__jlink_cmd_script(__env__)}"',
)
