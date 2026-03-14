Import("env")
from os.path import exists, join

env.Append(
    LINKFLAGS=[
        "-mfloat-abi=hard",
        "-mfpu=fpv4-sp-d16",
    ]
)


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
