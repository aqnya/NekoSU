// SPDX-License-Identifier: GPL-3.0-or-later
plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "me.nekosu.neko"
    ndkVersion = "29.0.14206865"
    compileSdk {
        version =
            release(37) {
                minorApiLevel = 1
            }
    }

    defaultConfig {
        minSdk = 31
    }
    packaging {
        jniLibs {
            keepDebugSymbols += "**/libncore.so"
        }
    }
}

// Same layout as LSPosed-cirno's Rust modules: build with cargo-ndk and let
// the app merge the generated jniLibs directory.
val ndkDirProvider = androidComponents.sdkComponents.ndkDirectory

val buildRust =
    tasks.register("buildRust") {
        group = "rust"
        description = "Build ncore and the JNI library for Android arm64"

        doLast {
            val ndkFile = ndkDirProvider.get().asFile
            val prebuilt =
                File(ndkFile, "toolchains/llvm/prebuilt").listFiles()?.firstOrNull()
                    ?: error("NDK LLVM toolchain not found under $ndkFile")
            val binDir = File(prebuilt, "bin")
            val strip = File(binDir, "llvm-strip")

            val targetDir = layout.buildDirectory.dir("rust/target").get().asFile
            val outJniDir = layout.buildDirectory.dir("generated/rust/jniLibs").get().asFile
            val abiDir = File(outJniDir, "arm64-v8a")
            abiDir.deleteRecursively()
            abiDir.mkdirs()

            val cargoArgs = mutableListOf("cargo", "ndk", "--platform", "28", "-t", "arm64-v8a")
            cargoArgs += listOf("build", "--release", "--target-dir", targetDir.absolutePath)

            providers.exec {
                workingDir = projectDir
                environment("ANDROID_NDK_HOME", ndkFile.absolutePath)
                environment("KSU_PACKAGE_NAME", "me.aqnya.nekosu")
                commandLine(cargoArgs)
            }.result.get().assertNormalExitValue()

            val signingKey = File(projectDir, "../signing/signing_key.pem")
            val signingPassword = System.getenv("SIGNING_PASSWORD").orEmpty()

            listOf(
                "ncore" to "libncore.so",
                "libjni.so" to "libjni.so",
            ).forEach { (binary, name) ->
                val source = File(targetDir, "aarch64-linux-android/release/$binary")
                check(source.isFile) { "Rust binary not found: $source" }
                val target = File(abiDir, name)
                source.copyTo(target, overwrite = true)

                providers.exec {
                    commandLine(strip.absolutePath, "--strip-all", target.absolutePath)
                }.result.get().assertNormalExitValue()

                if (name == "libncore.so") {
                    if (signingKey.isFile && signingPassword.isNotEmpty()) {
                        val sigFile = File(targetDir, "libncore.sig")
                        providers.exec {
                            environment("SIGNING_PASSWORD", signingPassword)
                            commandLine(
                                "openssl", "dgst", "-sha256", "-sign", signingKey.absolutePath,
                                "-passin", "env:SIGNING_PASSWORD",
                                "-out", sigFile.absolutePath,
                                target.absolutePath,
                            )
                        }.result.get().assertNormalExitValue()

                        val sig = sigFile.readBytes()
                        check(sig.size == 512) {
                            "Expected 512-byte RSA-4096 signature, got ${sig.size}. " +
                                "Check that ../signing/signing_key.pem really is RSA-4096."
                        }
                        target.appendBytes(sig)
                        sigFile.delete()
                        logger.lifecycle(
                            "Signed ${target.name}: ${target.length()} bytes total " +
                                "(${sig.size}-byte RSA-4096 signature appended)",
                        )
                    } else {
                        logger.lifecycle(
                            "signing_key.pem or SIGNING_PASSWORD missing; ${target.name} stays unsigned",
                        )
                    }
                }
            }
        }
    }

tasks.matching { it.name.startsWith("merge") && it.name.endsWith("JniLibFolders") }.configureEach {
    dependsOn(buildRust)
}
