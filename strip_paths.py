# PlatformIO pre-script: keep local paths out of the firmware.
# The Arduino core's log and assert macros embed __FILE__, which would otherwise carry
# the builder's home directory into the binary.
Import("env")

for d in (env.subst("$PROJECT_PACKAGES_DIR"), env.subst("$PROJECT_DIR")):
    d = d.replace("\\", "/").rstrip("/") + "/"
    env.Append(CCFLAGS=["-fmacro-prefix-map=%s=" % d, "-ffile-prefix-map=%s=" % d])
