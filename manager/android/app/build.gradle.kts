import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

dependencies {
    implementation("androidx.core:core-ktx:1.19.1")
    implementation("androidx.webkit:webkit:1.18.0")
}

val nekoRoot = rootProject.projectDir.parentFile.parentFile

fun gitValue(vararg args: String): String =
    runCatching {
        ProcessBuilder(listOf("git") + args)
            .directory(nekoRoot)
            .start()
            .inputStream
            .bufferedReader()
            .use { it.readText() }
            .trim()
    }.getOrDefault("")

fun gitCommitCount(): Int = gitValue("rev-list", "--count", "HEAD").toIntOrNull() ?: 1

fun gitCommitHash(): String = gitValue("rev-parse", "--short=12", "HEAD").ifBlank { "unknown" }

fun nksuVersion(): String =
    nekoRoot
        .resolve("uapi/version.h")
        .takeIf { it.isFile }
        ?.readText()
        ?.let {
            Regex(
                "^#define\\s+NKSU_VERSION\\s+\"([^\"]+)\"",
                RegexOption.MULTILINE,
            ).find(it)
        }?.groupValues
        ?.getOrNull(1)
        ?.trim()
        ?.ifEmpty { null }
        ?: "unknown"

val baseVersion = nksuVersion()
val versionNameOverride: String? =
    (
        (project.findProperty("versionNameOverride") as? String)
            ?: System.getenv("VERSION_NAME")
    )?.trim()?.takeIf { it.isNotEmpty() }

val requestedVersionChannel: String? =
    (
        (project.findProperty("versionChannel") as? String)
            ?: System.getenv("VERSION_CHANNEL")
    )?.trim()?.lowercase()?.takeIf { it.isNotEmpty() }

val inferredVersionChannel: String? =
    versionNameOverride?.let {
        when {
            it.contains("-canary.") -> "canary"
            it.contains("-beta.") -> "beta"
            else -> "release"
        }
    }

val versionChannel: String =
    when {
        requestedVersionChannel != null && inferredVersionChannel != null -> {
            require(requestedVersionChannel == inferredVersionChannel) {
                "versionChannel $requestedVersionChannel does not match versionName $versionNameOverride"
            }
            requestedVersionChannel
        }
        requestedVersionChannel != null -> requestedVersionChannel
        inferredVersionChannel != null -> inferredVersionChannel
        else -> "canary"
    }

val versionChannelCode: Int =
    when (versionChannel) {
        "canary" -> 0
        "beta" -> 1
        "release" -> 2
        else -> error("unknown versionChannel: $versionChannel (expected canary, beta or release)")
    }

val buildVersionName: String =
    versionNameOverride
        ?: when (versionChannel) {
            "canary" -> "$baseVersion-canary.${gitCommitCount()}+${gitCommitHash()}"
            "beta" -> "$baseVersion-beta.${gitCommitCount()}+${gitCommitHash()}"
            else -> baseVersion
        }
val buildVersionCode: Int = gitCommitCount() * 10 + versionChannelCode

val keystorePropertiesFile = rootProject.file("key.properties")
val keystoreProperties =
    Properties().apply {
        if (keystorePropertiesFile.exists()) {
            keystorePropertiesFile.inputStream().use { load(it) }
        }
    }
val releaseKeystore = nekoRoot.resolve("release.keystore")
val hasReleaseSigning =
    releaseKeystore.isFile &&
        releaseKeystore.length() > 0 &&
        listOf("KEYSTORE_PASSWORD", "KEY_ALIAS", "KEY_PASSWORD").all {
            !System.getenv(it).isNullOrBlank()
        }
val hasPropertiesSigning =
    keystorePropertiesFile.isFile &&
        listOf("storePassword", "keyAlias", "keyPassword", "storeFile").all {
            !keystoreProperties.getProperty(it).isNullOrBlank()
        }
val requireReleaseSigning =
    (project.findProperty("requireReleaseSigning") as? String)?.toBoolean() == true ||
        System.getenv("REQUIRE_RELEASE_SIGNING")?.toBoolean() == true

if (requireReleaseSigning && !hasReleaseSigning && !hasPropertiesSigning) {
    error("release signing is required but release.keystore/key.properties or signing environment variables are missing")
}

android {
    namespace = "com.example.nekosu"
    compileSdk {
        version =
            release(37) {
                minorApiLevel = 1
            }
    }
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        applicationId = "me.aqnya.nekosu"
        minSdk = 31
        targetSdk = 37
        versionCode = buildVersionCode
        versionName = buildVersionName

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                keyAlias = System.getenv("KEY_ALIAS")
                keyPassword = System.getenv("KEY_PASSWORD")
                storeFile = releaseKeystore
                storePassword = System.getenv("KEYSTORE_PASSWORD")
            }
        } else if (hasPropertiesSigning) {
            create("release") {
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
                storeFile = file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
        resources {
            excludes += "DebugProbesKt.bin"
            excludes += "kotlin-tooling-metadata.json"
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}

val nekoRustJniLibsDir =
    rootProject.layout.buildDirectory.dir("neko/generated/rust/jniLibs").get().asFile
android.sourceSets.getByName("main").jniLibs.directories.add(nekoRustJniLibsDir.path)

tasks.matching { it.name.startsWith("merge") && it.name.endsWith("JniLibFolders") }.configureEach {
    dependsOn(":neko:buildRust")
}
