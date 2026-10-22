package com.example.nekosu

import android.app.Activity
import android.content.ContentValues
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.provider.OpenableColumns
import io.flutter.embedding.android.FlutterActivity
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.io.IOException
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.random.Random

/**
 * Native side of the vendor_boot installer.
 *
 * Everything that needs an Android API (document picker, MediaStore and root
 * partition access) lives here. The actual image patching is delegated to the
 * bundled libbootpatcher.so binary. Long-running work is streamed through the
 * same event shape as ModuleBridge so Flutter can reuse OutputPage.
 */
class InstallBridge(
    private val activity: FlutterActivity,
    messenger: BinaryMessenger,
) {
    private companion object {
        const val CHANNEL = "nekosu/install_platform"
        const val OUTPUT_CHANNEL = "nekosu/install_platform/output"
        const val PICK_FILE_REQUEST = 4310
        const val BOOTPATCHER_LIBRARY = "libbootpatcher.so"
        const val KO_ASSET_DIR = "ko"
        const val LOCAL_KO_NAME = "local_nksu.ko"
        const val SOURCE_IMAGE_NAME = "vendor_boot.source.img"
        const val OUTPUT_PREFIX = "nekosu_"
        const val OUTPUT_SUFFIX = "_vendor_boot.img"
        const val RANDOM_TOKEN_LENGTH = 8
        const val RANDOM_ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789"
        val BLOCK_DIRECTORIES =
            listOf(
                "/dev/block/by-name",
                "/dev/block/bootdevice/by-name",
                "/dev/block/platform/soc/1d84000.ufshc/by-name",
            )
    }

    private enum class PickerKind { VENDOR_BOOT, LOCAL_KO }

    private data class PendingPicker(
        val kind: PickerKind,
        val result: MethodChannel.Result,
    )

    private data class InstallRequest(
        val operation: String,
        val vendorBootPath: String?,
        val koName: String?,
        val koPath: String?,
        val partition: String?,
    )

    private val channel = MethodChannel(messenger, CHANNEL)
    private val outputChannel = EventChannel(messenger, OUTPUT_CHANNEL)
    private val executor = Executors.newSingleThreadExecutor()
    private val outputBusy = AtomicBoolean(false)
    private var pendingPicker: PendingPicker? = null
    private var outputSink: EventChannel.EventSink? = null
    private var activeProcess: Process? = null
    private var cachedSuPath: String? = null

    private val installDir: File
        get() = File(activity.filesDir, "nksu-install").apply { mkdirs() }

    private val bootpatcher: File
        get() = File(activity.applicationInfo.nativeLibraryDir, BOOTPATCHER_LIBRARY)

    fun register() {
        channel.setMethodCallHandler(::onMethodCall)
        outputChannel.setStreamHandler(
            object : EventChannel.StreamHandler {
                override fun onListen(
                    arguments: Any?,
                    events: EventChannel.EventSink?,
                ) {
                    outputSink = events
                    startOutput(arguments)
                }

                override fun onCancel(arguments: Any?) {
                    outputSink = null
                }
            },
        )
    }

    fun dispose() {
        pendingPicker = null
        activeProcess?.destroy()
        activeProcess = null
        outputSink = null
        outputBusy.set(false)
        channel.setMethodCallHandler(null)
        outputChannel.setStreamHandler(null)
        executor.shutdownNow()
    }

    fun onActivityResult(
        requestCode: Int,
        resultCode: Int,
        data: Intent?,
    ): Boolean {
        if (requestCode != PICK_FILE_REQUEST) return false
        val pending = pendingPicker ?: return true
        pendingPicker = null
        val uri = data?.data
        if (resultCode != Activity.RESULT_OK || uri == null) {
            pending.result.success(null)
            return true
        }
        executor.execute { stagePickedFile(pending, uri) }
        return true
    }

    private fun onMethodCall(
        call: MethodCall,
        result: MethodChannel.Result,
    ) {
        when (call.method) {
            "getEnvironment" -> executor.execute { loadEnvironment(result) }
            "pickVendorBoot" -> pickFile(PickerKind.VENDOR_BOOT, result)
            "pickLocalKo" -> pickFile(PickerKind.LOCAL_KO, result)
            else -> result.notImplemented()
        }
    }

    private fun loadEnvironment(result: MethodChannel.Result) {
        try {
            val slot = currentSlotSuffix()
            val su = resolveSu()
            val environment =
                mapOf<String, Any?>(
                    "kernelRelease" to System.getProperty("os.version").orEmpty(),
                    "androidVersion" to Build.VERSION.RELEASE.orEmpty(),
                    "koAssets" to koAssets(),
                    "rootAvailable" to (su != null),
                    "bootpatcherAvailable" to bootpatcherFile().canExecute(),
                    "currentSlot" to slot,
                    "currentVendorBoot" to findVendorBoot(slot, allowSlotless = true, su = su),
                    "inactiveVendorBoot" to
                        inactiveSlotSuffix(slot)?.let { findVendorBoot(it, allowSlotless = false, su = su) },
                )
            activity.runOnUiThread { result.success(environment) }
        } catch (error: Exception) {
            activity.runOnUiThread {
                result.error(
                    "install_environment_failed",
                    error.message ?: "Unable to inspect the install environment",
                    null,
                )
            }
        }
    }

    private fun pickFile(
        kind: PickerKind,
        result: MethodChannel.Result,
    ) {
        if (pendingPicker != null) {
            result.error("picker_busy", "The document picker is already open", null)
            return
        }
        pendingPicker = PendingPicker(kind, result)
        val intent =
            Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
            }
        try {
            activity.startActivityForResult(intent, PICK_FILE_REQUEST)
        } catch (error: Exception) {
            pendingPicker = null
            result.error("picker_failed", error.message ?: "Unable to open document picker", null)
        }
    }

    private fun stagePickedFile(
        pending: PendingPicker,
        uri: Uri,
    ) {
        try {
            val displayName = queryDisplayName(uri)
            val fileName =
                when (pending.kind) {
                    PickerKind.VENDOR_BOOT -> "vendor_boot.img"
                    PickerKind.LOCAL_KO -> LOCAL_KO_NAME
                }
            val dest = File(installDir, fileName).apply { delete() }
            activity.contentResolver.openInputStream(uri)?.use { input ->
                dest.outputStream().use { output -> input.copyTo(output) }
            } ?: throw IllegalStateException("Unable to open the selected file")
            if (!dest.isFile || dest.length() == 0L) {
                dest.delete()
                throw IllegalStateException("The selected file is empty")
            }
            dest.setReadable(true, false)
            val payload =
                mapOf(
                    "name" to (displayName ?: fileName),
                    "path" to dest.absolutePath,
                    "size" to dest.length(),
                )
            activity.runOnUiThread { pending.result.success(payload) }
        } catch (error: Exception) {
            activity.runOnUiThread {
                pending.result.error(
                    "picker_copy_failed",
                    error.message ?: "Unable to stage the selected file",
                    null,
                )
            }
        }
    }

    private fun queryDisplayName(uri: Uri): String? =
        runCatching {
            activity.contentResolver
                .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                        if (index >= 0) cursor.getString(index) else null
                    } else {
                        null
                    }
                }
        }.getOrNull()

    private fun startOutput(arguments: Any?) {
        val payload = arguments as? Map<*, *>
        val request =
            InstallRequest(
                operation = payload?.get("operation")?.toString().orEmpty(),
                vendorBootPath = payload?.get("vendorBootPath")?.toString(),
                koName = payload?.get("koName")?.toString(),
                koPath = payload?.get("koPath")?.toString(),
                partition = payload?.get("partition")?.toString(),
            )
        if (request.operation !in setOf("file", "fileDirect", "direct", "inactive")) {
            failOutput("Unsupported install operation", releaseBusy = false)
            return
        }
        if (!outputBusy.compareAndSet(false, true)) {
            failOutput("Another install command is running", releaseBusy = false)
            return
        }
        try {
            executor.execute { runInstall(request) }
        } catch (error: Exception) {
            failOutput(
                error.message ?: "Unable to start the install command",
                releaseBusy = true,
            )
        }
    }

    private fun runInstall(request: InstallRequest) {
        var exitCode = -1
        try {
            val tool = bootpatcherFile()
            if (!tool.canExecute()) {
                emitOutputLine("ERROR: libbootpatcher.so is not executable\n")
                return
            }
            val ko = resolveKo(request)
            if (ko == null) {
                emitOutputLine("ERROR: kernel module not found\n")
                return
            }
            exitCode =
                when (request.operation) {
                    "file" -> installFromFile(tool, request, ko)
                    "fileDirect" -> installFromFile(tool, request, ko, flash = true)
                    "direct" -> directInstall(tool, request, ko, inactive = false)
                    "inactive" -> directInstall(tool, request, ko, inactive = true)
                    else -> -1
                }
        } catch (error: Exception) {
            emitOutputLine("ERROR: ${error.message ?: error.javaClass.simpleName}\n")
        } finally {
            activeProcess = null
            emitOutputDone(exitCode, releaseBusy = true)
        }
    }

    private fun resolveKo(request: InstallRequest): File? {
        val localPath = request.koPath?.takeIf { it.isNotBlank() }
        if (localPath != null) {
            return File(localPath).takeIf { it.isFile && it.length() > 0 }
        }

        val name = request.koName?.takeIf { it.isNotBlank() } ?: return null
        if (name != File(name).name || !name.endsWith("_nksu.ko")) return null
        val dest = File(installDir, name)
        dest.delete()
        activity.assets.open("$KO_ASSET_DIR/$name").use { input ->
            dest.outputStream().use { output -> input.copyTo(output) }
        }
        dest.setReadable(true, false)
        return dest.takeIf { it.isFile && it.length() > 0 }
    }

    private fun bootpatcherFile(): File {
        val tool = bootpatcher
        if (tool.isFile && !tool.canExecute()) {
            tool.setExecutable(true, false)
        }
        return tool
    }

    private fun installFromFile(
        tool: File,
        request: InstallRequest,
        ko: File,
        flash: Boolean = false,
    ): Int {
        val source = request.vendorBootPath?.takeIf { it.isNotBlank() }?.let(::File)
        if (source == null || !source.isFile || source.length() == 0L) {
            emitOutputLine("ERROR: select a vendor_boot image first\n")
            return -1
        }

        emitOutputLine("[nekosu] patching vendor_boot image...\n")
        val patched = patchImage(tool, source, ko) ?: return -1

        if (flash) {
            val partition =
                request.partition?.takeIf { it.isNotBlank() }
                    ?: findVendorBoot(currentSlotSuffix(), allowSlotless = true, su = resolveSu())
            if (partition.isNullOrBlank()) {
                source.delete()
                patched.delete()
                emitOutputLine("ERROR: vendor_boot partition not found\n")
                return -1
            }
            val capacity = partitionSize(partition)
            if (capacity != null && patched.length() > capacity) {
                emitOutputLine(
                    "ERROR: patched image (${patched.length()} bytes) exceeds partition capacity ($capacity bytes)\n",
                )
                source.delete()
                patched.delete()
                return -1
            }

            emitOutputLine("[nekosu] writing the patched image to $partition...\n")
            if (!copyFileToPartition(patched, partition)) {
                source.delete()
                patched.delete()
                emitOutputLine("ERROR: failed to flash vendor_boot\n")
                return -1
            }

            source.delete()
            patched.delete()
            emitOutputLine("[nekosu] direct install complete; reboot to take effect\n")
            return 0
        }

        val outputName = outputImageName()
        val output = File(installDir, outputName)
        output.delete()
        if (!patched.renameTo(output)) {
            patched.copyTo(output, overwrite = true)
            patched.delete()
        }

        exportToDownloads(output)
        emitOutputLine("[nekosu] exported to Download/$outputName\n")
        output.delete()
        source.delete()
        emitOutputLine("[nekosu] done\n")
        return 0
    }

    private fun directInstall(
        tool: File,
        request: InstallRequest,
        ko: File,
        inactive: Boolean,
    ): Int {
        if (resolveSu() == null) {
            emitOutputLine("ERROR: root access is required for direct install\n")
            return -1
        }

        val slot = currentSlotSuffix()
        val partition =
            request.partition?.takeIf { it.isNotBlank() }
                ?: if (inactive) {
                    inactiveSlotSuffix(slot)?.let { findVendorBoot(it, allowSlotless = false, su = resolveSu()) }
                } else {
                    findVendorBoot(slot, allowSlotless = true, su = resolveSu())
                }
        if (partition.isNullOrBlank()) {
            emitOutputLine("ERROR: vendor_boot partition not found\n")
            return -1
        }
        emitOutputLine("[nekosu] vendor_boot: $partition\n")

        val source = File(installDir, SOURCE_IMAGE_NAME).apply { delete() }
        source.createNewFile()
        emitOutputLine("[nekosu] reading the current vendor_boot image...\n")
        if (!copyPartitionToFile(partition, source) || source.length() == 0L) {
            source.delete()
            emitOutputLine("ERROR: failed to read vendor_boot\n")
            return -1
        }

        emitOutputLine("[nekosu] patching vendor_boot image...\n")
        val patched = patchImage(tool, source, ko)
        if (patched == null) {
            source.delete()
            return -1
        }

        val capacity = partitionSize(partition)
        if (capacity != null && patched.length() > capacity) {
            emitOutputLine(
                "ERROR: patched image (${patched.length()} bytes) exceeds partition capacity ($capacity bytes)\n",
            )
            source.delete()
            patched.delete()
            return -1
        }

        emitOutputLine("[nekosu] writing the patched vendor_boot image...\n")
        if (!copyFileToPartition(patched, partition)) {
            emitOutputLine("[nekosu] write failed, restoring the original image\n")
            copyFileToPartition(source, partition)
            source.delete()
            patched.delete()
            emitOutputLine("ERROR: failed to flash vendor_boot\n")
            return -1
        }

        source.delete()
        patched.delete()
        emitOutputLine("[nekosu] direct install complete; reboot to take effect\n")
        return 0
    }

    private fun patchImage(
        tool: File,
        source: File,
        ko: File,
    ): File? {
        val output = File(source.parentFile, "${source.name}.new")
        output.delete()
        val process =
            ProcessBuilder(
                tool.absolutePath,
                "-p",
                source.absolutePath,
                ko.absolutePath,
            )
                .directory(source.parentFile)
                .redirectErrorStream(true)
                .start()
        activeProcess = process
        process.inputStream.bufferedReader().useLines { lines ->
            lines.forEach { line -> emitOutputLine("$line\n") }
        }
        val exitCode = process.waitFor()
        activeProcess = null
        if (exitCode != 0 || !output.isFile || output.length() == 0L) {
            output.delete()
            emitOutputLine("ERROR: libbootpatcher failed with exit code $exitCode\n")
            return null
        }
        return output
    }

    private fun copyPartitionToFile(
        partition: String,
        destination: File,
    ): Boolean {
        val command = "/system/bin/dd if=${quote(partition)} of=${quote(destination.absolutePath)} bs=4M 2>&1"
        return runRootCommand(command, stream = true) == 0
    }

    private fun copyFileToPartition(
        source: File,
        partition: String,
    ): Boolean {
        val command = "/system/bin/dd if=${quote(source.absolutePath)} of=${quote(partition)} bs=4M conv=fsync 2>&1"
        return runRootCommand(command, stream = true) == 0
    }

    private fun partitionSize(partition: String): Long? {
        val command = "/system/bin/blockdev --getsize64 ${quote(partition)} 2>/dev/null"
        val (code, output) = runRootCommandCapture(command)
        return if (code == 0) output.trim().toLongOrNull() else null
    }

    private fun runRootCommand(
        command: String,
        stream: Boolean,
    ): Int {
        val su = resolveSu() ?: return -1
        val process =
            ProcessBuilder(su, "-c", withPath(command))
                .redirectErrorStream(true)
                .start()
        activeProcess = process
        if (stream) {
            process.inputStream.bufferedReader().useLines { lines ->
                lines.forEach { line -> emitOutputLine("$line\n") }
            }
        } else {
            process.inputStream.bufferedReader().use { it.readText() }
        }
        val code = process.waitFor()
        activeProcess = null
        return code
    }

    private fun runRootCommandCapture(command: String): Pair<Int, String> {
        val su = resolveSu() ?: return -1 to ""
        val process =
            ProcessBuilder(su, "-c", withPath(command))
                .redirectErrorStream(true)
                .start()
        val output = process.inputStream.bufferedReader().use { it.readText() }
        return process.waitFor() to output
    }

    private fun exportToDownloads(source: File) {
        val values =
            ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, source.name)
                put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream")
                put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            }
        val resolver = activity.contentResolver
        val uri =
            resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                ?: throw IOException("Unable to create the output file in Download")
        try {
            resolver.openOutputStream(uri, "w")?.use { output ->
                source.inputStream().use { input -> input.copyTo(output) }
            } ?: throw IOException("Unable to open the Download output stream")
            values.clear()
            values.put(MediaStore.MediaColumns.IS_PENDING, 0)
            resolver.update(uri, values, null, null)
        } catch (error: Exception) {
            resolver.delete(uri, null, null)
            throw error
        }
    }

    private fun koAssets(): List<String> =
        (activity.assets.list(KO_ASSET_DIR) ?: emptyArray())
            .filter { it.endsWith("_nksu.ko") }
            .sorted()

    private fun currentSlotSuffix(): String? {
        val raw =
            getProperty("ro.boot.slot_suffix")
                .ifBlank { getProperty("ro.boot.slot") }
                .trim()
        return when {
            raw == "_a" || raw == "_b" -> raw
            raw == "a" || raw == "b" -> "_$raw"
            else -> null
        }
    }

    private fun inactiveSlotSuffix(slot: String?): String? =
        when (slot) {
            "_a" -> "_b"
            "_b" -> "_a"
            else -> null
        }

    private fun findVendorBoot(
        slot: String?,
        allowSlotless: Boolean,
        su: String? = null,
    ): String? {
        val names = buildList {
            if (!slot.isNullOrBlank()) add("vendor_boot$slot")
            if (slot.isNullOrBlank() || allowSlotless) add("vendor_boot")
        }
        for (directory in BLOCK_DIRECTORIES) {
            for (name in names) {
                val path = File(directory, name)
                if (path.exists()) return path.absolutePath
            }
        }
        if (su == null) return null

        /*
         * The app sandbox cannot stat /dev/block, so a rooted device would
         * otherwise never offer the direct-install methods.  Resolve the same
         * candidates through su and hand the resulting block device back.
         */
        val candidates =
            BLOCK_DIRECTORIES.flatMap { directory ->
                names.map { name -> quote("$directory/$name") }
            }
        val command =
            "for p in ${candidates.joinToString(" ")}; " +
                "do if [ -e \"\$p\" ]; then readlink -f \"\$p\"; break; fi; done"
        val (code, output) = runRootCommandCapture(command)
        if (code != 0) return null
        return output.lineSequence().firstOrNull { it.isNotBlank() }?.trim()
    }

    private fun getProperty(name: String): String =
        runCatching {
            val process =
                ProcessBuilder("/system/bin/getprop", name)
                    .redirectErrorStream(true)
                    .start()
            val output = process.inputStream.bufferedReader().use { it.readText() }
            process.waitFor()
            output.trim()
        }.getOrDefault("")

    private fun resolveSu(): String? {
        cachedSuPath?.let { path -> if (File(path).canExecute()) return path }
        val candidates =
            listOf(
                "/system/bin/su",
                "/system/xbin/su",
                "/sbin/su",
                "/debug_ramdisk/su",
                "/data/adb/ksu/bin/su",
                "/data/adb/magisk/su",
            )
        val direct = candidates.firstOrNull { File(it).canExecute() }
        if (direct != null) {
            cachedSuPath = direct
            return direct
        }
        val fromPath =
            runCatching {
                val process =
                    ProcessBuilder("/system/bin/sh", "-c", "command -v su 2>/dev/null")
                        .redirectErrorStream(true)
                        .start()
                val output = process.inputStream.bufferedReader().use { it.readText() }.trim()
                process.waitFor()
                output.lineSequence().firstOrNull { it.isNotBlank() }
            }.getOrNull()
        if (fromPath != null) cachedSuPath = fromPath
        return fromPath
    }

    private fun outputImageName(): String {
        val token =
            (1..RANDOM_TOKEN_LENGTH)
                .map { RANDOM_ALPHABET[Random.nextInt(RANDOM_ALPHABET.length)] }
                .joinToString("")
        return "$OUTPUT_PREFIX$token$OUTPUT_SUFFIX"
    }

    private fun failOutput(
        message: String,
        releaseBusy: Boolean,
    ) {
        emitOutputLine("ERROR: $message\n")
        emitOutputDone(-1, releaseBusy)
    }

    private fun emitOutputLine(line: String) {
        activity.runOnUiThread {
            outputSink?.success(mapOf("type" to "line", "text" to line))
        }
    }

    private fun emitOutputDone(
        code: Int,
        releaseBusy: Boolean,
    ) {
        activity.runOnUiThread {
            outputSink?.success(mapOf("type" to "done", "code" to code))
            if (releaseBusy) outputBusy.set(false)
        }
    }

    private fun withPath(command: String): String =
        "PATH=/sbin:/system/sbin:/system/bin:/system/xbin; export PATH; $command"

    private fun quote(value: String): String = "'${value.replace("'", "'\\''")}'"
}
