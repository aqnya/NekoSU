package com.example.nekosu

import android.app.Activity
import android.content.Intent
import android.net.Uri
import io.flutter.embedding.android.FlutterActivity
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Platform-only helpers for the FFI-backed module screen.
 *
 * Module state and short commands are handled by ncore_ffi. This bridge opens
 * Android's document picker and streams long-running install/action commands
 * back to Flutter line by line.
 */
class ModuleBridge(
    private val activity: FlutterActivity,
    messenger: BinaryMessenger,
) {
    private companion object {
        const val CHANNEL = "nekosu/modules_platform"
        const val OUTPUT_CHANNEL = "nekosu/modules_platform/output"
        const val PICK_ZIP_REQUEST = 4210
        const val NCORE_LIBRARY = "libncore.so"
    }

    private val channel = MethodChannel(messenger, CHANNEL)
    private val outputChannel = EventChannel(messenger, OUTPUT_CHANNEL)
    private val executor = Executors.newSingleThreadExecutor()
    private val outputBusy = AtomicBoolean(false)
    private var pendingPickerResult: MethodChannel.Result? = null
    private var outputSink: EventChannel.EventSink? = null
    private var activeProcess: Process? = null

    fun register() {
        channel.setMethodCallHandler(::onMethodCall)
        outputChannel.setStreamHandler(
            object : EventChannel.StreamHandler {
                override fun onListen(
                    arguments: Any?,
                    events: EventChannel.EventSink?,
                ) {
                    outputSink = events
                    val payload = arguments as? Map<*, *>
                    startOutput(
                        payload?.get("operation")?.toString(),
                        payload?.get("argument")?.toString(),
                    )
                }

                override fun onCancel(arguments: Any?) {
                    outputSink = null
                }
            },
        )
    }

    fun dispose() {
        pendingPickerResult = null
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
        if (requestCode != PICK_ZIP_REQUEST) return false
        val result = pendingPickerResult ?: return true
        pendingPickerResult = null
        val uri = data?.data
        if (resultCode != Activity.RESULT_OK || uri == null) {
            result.success(null)
            return true
        }
        stageZip(result, uri)
        return true
    }

    private fun onMethodCall(
        call: MethodCall,
        result: MethodChannel.Result,
    ) {
        when (call.method) {
            "pickZip" -> pickZip(result)
            else -> result.notImplemented()
        }
    }

    private fun startOutput(
        operation: String?,
        argument: String?,
    ) {
        if (operation == null || argument.isNullOrBlank()) {
            failOutput("operation and argument are required")
            return
        }
        if (!outputBusy.compareAndSet(false, true)) {
            failOutput("Another output command is running")
            return
        }

        val ncore = File(activity.applicationInfo.nativeLibraryDir, NCORE_LIBRARY)
        if (!ncore.isFile) {
            failOutput("ncore runtime not found", releaseBusy = true)
            return
        }

        val command =
            when (operation) {
                "install" -> "${quote(ncore.absolutePath)} module install ${quote(argument)}"
                "action" -> "${quote(ncore.absolutePath)} module action ${quote(argument)}"
                else -> {
                    failOutput("Unsupported output operation", releaseBusy = true)
                    return
                }
            }

        try {
            executor.execute { runOutput(command) }
        } catch (error: Exception) {
            failOutput(
                error.message ?: "Unable to start output command",
                releaseBusy = true,
            )
        }
    }

    private fun failOutput(
        message: String,
        releaseBusy: Boolean = false,
    ) {
        emitOutputLine("ERROR: $message\n")
        emitOutputDone(-1, releaseBusy)
    }

    private fun runOutput(command: String) {
        var exitCode = -1
        try {
            val su = if (File("/system/bin/su").isFile) "/system/bin/su" else "su"
            val process =
                ProcessBuilder(su, "-c", withPath(command))
                    .redirectErrorStream(true)
                    .start()
            activeProcess = process
            process.inputStream.bufferedReader().useLines { lines ->
                lines.forEach { line -> emitOutputLine("$line\n") }
            }
            exitCode = process.waitFor()
        } catch (error: Exception) {
            emitOutputLine("ERROR: ${error.message ?: error.javaClass.simpleName}\n")
        } finally {
            activeProcess = null
            emitOutputDone(exitCode, releaseBusy = true)
        }
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

    private fun withPath(command: String): String {
        return "PATH=/sbin:/system/sbin:/system/bin:/system/xbin; export PATH; $command"
    }

    private fun quote(value: String): String {
        return "'${value.replace("'", "'\\''")}'"
    }

    private fun pickZip(result: MethodChannel.Result) {
        if (pendingPickerResult != null) {
            result.error("picker_busy", "The document picker is already open", null)
            return
        }
        pendingPickerResult = result
        val intent =
            Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
                putExtra(
                    Intent.EXTRA_MIME_TYPES,
                    arrayOf("application/zip", "application/octet-stream"),
                )
            }
        try {
            activity.startActivityForResult(intent, PICK_ZIP_REQUEST)
        } catch (error: Exception) {
            pendingPickerResult = null
            result.error("picker_failed", error.message ?: "Unable to open document picker", null)
        }
    }

    private fun stageZip(
        result: MethodChannel.Result,
        uri: Uri,
    ) {
        executor.execute {
            try {
                val file = File(activity.cacheDir, "module-install.zip")
                activity.contentResolver.openInputStream(uri).use { input ->
                    requireNotNull(input) { "Unable to open the selected archive" }
                    file.outputStream().use { output -> input.copyTo(output) }
                }
                activity.runOnUiThread { result.success(file.absolutePath) }
            } catch (error: Exception) {
                activity.runOnUiThread {
                    result.error(
                        "picker_copy_failed",
                        error.message ?: "Unable to stage module archive",
                        null,
                    )
                }
            }
        }
    }
}
