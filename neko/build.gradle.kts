// SPDX-License-Identifier: GPL-3.0-or-later
plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "me.nekosu.neko"
    compileSdk {
        version =
            release(37) {
                minorApiLevel = 1
            }
    }

    defaultConfig {
        minSdk = 31
    }
    sourceSets.getByName("main").jniLibs.srcDir(
        layout.buildDirectory.dir("generated/rust/jniLibs").get().asFile,
    )
    packaging {
        jniLibs {
            // Cargo strips release binaries; do not make AGP look for an NDK
            // just to strip the AAR's jniLibs again.
            keepDebugSymbols += "**/*.so"
        }
    }
}

// The Android Rust target and linker are expected to be configured by the
// build environment. Gradle only invokes cargo and packages the result.
val buildRust =
    tasks.register("buildRust") {
        group = "rust"
        description = "Build ncore and the JNI library for Android arm64"

        doLast {
            val targetTriple = "aarch64-linux-android"
            val targetDir = layout.buildDirectory.dir("rust/target").get().asFile
            val outJniDir = layout.buildDirectory.dir("generated/rust/jniLibs").get().asFile
            val abiDir = File(outJniDir, "arm64-v8a")
            abiDir.deleteRecursively()
            abiDir.mkdirs()

            providers.exec {
                workingDir = projectDir
                environment("KSU_PACKAGE_NAME", "me.aqnya.nekosu")
                commandLine(
                    "cargo",
                    "build",
                    "--release",
                    "--target",
                    targetTriple,
                    "--target-dir",
                    targetDir.absolutePath,
                )
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

tasks.matching {
    (it.name.startsWith("merge") && it.name.endsWith("JniLibFolders")) ||
        (it.name.startsWith("bundle") && it.name.endsWith("Aar"))
}.configureEach {
    dependsOn(buildRust)
}
