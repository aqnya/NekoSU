package com.example.nekosu

import android.content.ContentValues
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.util.Log
import android.util.LruCache
import android.view.MotionEvent
import androidx.core.content.FileProvider
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.ByteArrayOutputStream
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.math.min
import kotlin.math.roundToInt

class MainActivity : FlutterActivity() {
    private companion object {
        const val CHANNEL = "nekosu/apps"
        const val SETTINGS_CHANNEL = "nekosu/settings"
        const val MAX_ICON_SIZE = 160
        const val MIUI_APPS_PERMISSION = "com.android.permission.GET_INSTALLED_APPS"
        const val MIUI_SECURITY_PACKAGE = "com.lbe.security.miui"
        const val MIUI_PERMISSION_REQUEST = 4102
        const val TAG = "NekosuTapGuard"
        const val OBSCURED_FLAGS =
            MotionEvent.FLAG_WINDOW_IS_OBSCURED or MotionEvent.FLAG_WINDOW_IS_PARTIALLY_OBSCURED
    }

    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())
    private var moduleBridge: ModuleBridge? = null
    private var installBridge: InstallBridge? = null
    private val iconCache =
        object : LruCache<String, ByteArray>(8 * 1024 * 1024) {
            override fun sizeOf(
                key: String,
                value: ByteArray,
            ): Int = value.size
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.setHideOverlayWindows(true)
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if ((event.flags and OBSCURED_FLAGS) != 0) {
            if (event.actionMasked == MotionEvent.ACTION_DOWN) {
                Log.w(TAG, "blocked touch while the window is obscured")
            }
            return true
        }
        return super.dispatchTouchEvent(event)
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        flutterEngine.platformViewsController.registry.registerViewFactory(
            "nekosu/webui",
            WebUiViewFactory(flutterEngine.dartExecutor.binaryMessenger),
        )
        moduleBridge = ModuleBridge(this, flutterEngine.dartExecutor.binaryMessenger).also { it.register() }
        installBridge = InstallBridge(this, flutterEngine.dartExecutor.binaryMessenger).also { it.register() }

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "listApps" -> runAsync(result) { listApps() }
                "getIcon" -> {
                    val packageName = call.argument<String>("packageName")
                    if (packageName.isNullOrEmpty()) {
                        result.error("invalid_argument", "packageName is required", null)
                    } else {
                        runAsync(result) { getIcon(packageName) }
                    }
                }
                else -> result.notImplemented()
            }
        }

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, SETTINGS_CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                // Legacy settings written before the shared_preferences migration.
                "load" -> {
                    val prefs = getSharedPreferences("app_preferences", MODE_PRIVATE)
                    result.success(
                        mapOf(
                            "themeMode" to prefs.getInt("theme_mode", 0),
                            "themeColor" to prefs.getInt("theme_color", 0),
                            "fontWeight" to prefs.getInt("font_weight", 1),
                            "amoled" to prefs.getBoolean("amoled_mode", false),
                            "floatingNavBar" to prefs.getBoolean("floating_navbar", true),
                            "navBarStyle" to prefs.getString("nav_bar_style", null),
                            "language" to prefs.getString("language", "system"),
                            "displayMode" to prefs.getString("display_mode", null),
                        ),
                    )
                }
                "exportLogs" -> exportLogs(
                    call.argument<String>("title") ?: "NekoSU logs",
                    result,
                )
                "saveOutputLog" -> saveOutputLog(
                    call.argument<String>("fileName") ?: "nekosu-output.log",
                    call.argument<String>("content") ?: "",
                    result,
                )
                else -> result.notImplemented()
            }
        }

        mainHandler.postDelayed({ requestMiuiAppListPermission() }, 300)
    }

    override fun onDestroy() {
        moduleBridge?.dispose()
        moduleBridge = null
        installBridge?.dispose()
        installBridge = null
        executor.shutdownNow()
        super.onDestroy()
    }

    override fun onActivityResult(
        requestCode: Int,
        resultCode: Int,
        data: Intent?,
    ) {
        if (WebUiFileChooser.onActivityResult(requestCode, resultCode, data)) return
        if (moduleBridge?.onActivityResult(requestCode, resultCode, data) == true) return
        if (installBridge?.onActivityResult(requestCode, resultCode, data) == true) return
        super.onActivityResult(requestCode, resultCode, data)
    }

    private fun <T> runAsync(
        result: MethodChannel.Result,
        block: () -> T,
    ) {
        executor.execute {
            try {
                val value = block()
                mainHandler.post { result.success(value) }
            } catch (error: Exception) {
                mainHandler.post {
                    result.error("app_query_failed", error.message ?: "App query failed", null)
                }
            }
        }
    }

    private fun listApps(): List<Map<String, Any>> {
        val pm = packageManager
        val launcherIntent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        val resolved =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                pm.queryIntentActivities(launcherIntent, PackageManager.ResolveInfoFlags.of(0L))
            } else {
                @Suppress("DEPRECATION")
                pm.queryIntentActivities(launcherIntent, 0)
            }
        val launchable = resolved.mapNotNullTo(HashSet<String>()) { it.activityInfo?.packageName }

        val apps = ArrayList<Map<String, Any>>()
        for (pkg in pm.getInstalledPackages(0)) {
            val info = pkg.applicationInfo ?: continue
            val label = info.loadLabel(pm).toString().ifBlank { pkg.packageName }
            apps +=
                mapOf(
                    "name" to label,
                    "packageName" to pkg.packageName,
                    "uid" to info.uid,
                    "isSystem" to ((info.flags and ApplicationInfo.FLAG_SYSTEM) != 0),
                    "isLaunchable" to launchable.contains(pkg.packageName),
                    "versionName" to (pkg.versionName ?: ""),
                    "versionCode" to pkg.longVersionCode,
                )
        }
        return apps
    }

    private fun requestMiuiAppListPermission() {
        if (isFinishing || isDestroyed) return
        val permissionInfo =
            try {
                packageManager.getPermissionInfo(MIUI_APPS_PERMISSION, 0)
            } catch (_: PackageManager.NameNotFoundException) {
                return
            }
        if (permissionInfo.packageName != MIUI_SECURITY_PACKAGE) return
        if (checkSelfPermission(MIUI_APPS_PERMISSION) == PackageManager.PERMISSION_GRANTED) return
        try {
            requestPermissions(arrayOf(MIUI_APPS_PERMISSION), MIUI_PERMISSION_REQUEST)
        } catch (_: Exception) {}
    }

    private fun getIcon(packageName: String): ByteArray {
        iconCache.get(packageName)?.let { return it }

        val drawable = packageManager.getApplicationIcon(packageName)
        val width = drawable.intrinsicWidth.takeIf { it > 0 } ?: MAX_ICON_SIZE
        val height = drawable.intrinsicHeight.takeIf { it > 0 } ?: MAX_ICON_SIZE
        val scale = min(1f, MAX_ICON_SIZE.toFloat() / maxOf(width, height))
        val outWidth = maxOf(1, (width * scale).roundToInt())
        val outHeight = maxOf(1, (height * scale).roundToInt())

        val bitmap = Bitmap.createBitmap(outWidth, outHeight, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        drawable.setBounds(0, 0, outWidth, outHeight)
        drawable.draw(canvas)

        val stream = ByteArrayOutputStream()
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
        bitmap.recycle()
        val bytes = stream.toByteArray()
        iconCache.put(packageName, bytes)
        return bytes
    }

    private fun exportLogs(
        title: String,
        result: MethodChannel.Result,
    ) {
        executor.execute {
            try {
                val file = createLogArchive()
                mainHandler.post {
                    try {
                        val uri =
                            FileProvider.getUriForFile(
                                this,
                                "$packageName.fileprovider",
                                file,
                            )
                        val intent =
                            Intent(Intent.ACTION_SEND).apply {
                                type = "application/zip"
                                putExtra(Intent.EXTRA_STREAM, uri)
                                putExtra(Intent.EXTRA_SUBJECT, title)
                                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                            }
                        startActivity(Intent.createChooser(intent, title))
                        result.success(file.absolutePath)
                    } catch (error: Exception) {
                        result.error(
                            "log_export_share_failed",
                            error.message ?: "Unable to share logs",
                            null,
                        )
                    }
                }
            } catch (error: Exception) {
                mainHandler.post {
                    result.error(
                        "log_export_failed",
                        error.message ?: "Unable to export logs",
                        null,
                    )
                }
            }
        }
    }

    private fun saveOutputLog(
        fileName: String,
        content: String,
        result: MethodChannel.Result,
    ) {
        executor.execute {
            try {
                val safeName =
                    File(fileName).name.takeIf {
                        it.isNotBlank() && it != "." && it != ".."
                    } ?: "nekosu-output.log"
                val values =
                    ContentValues().apply {
                        put(MediaStore.MediaColumns.DISPLAY_NAME, safeName)
                        put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
                        put(
                            MediaStore.MediaColumns.RELATIVE_PATH,
                            Environment.DIRECTORY_DOWNLOADS,
                        )
                    }
                val uri =
                    requireNotNull(
                        contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values),
                    ) { "Unable to create the output file" }
                val output =
                    requireNotNull(contentResolver.openOutputStream(uri, "w")) {
                        "Unable to open the output file"
                    }
                output.use { it.write(content.toByteArray(Charsets.UTF_8)) }
                mainHandler.post { result.success(safeName) }
            } catch (error: Exception) {
                mainHandler.post {
                    result.error(
                        "output_save_failed",
                        error.message ?: "Unable to save output log",
                        null,
                    )
                }
            }
        }
    }

    private fun createLogArchive(): File {
        val directory = File(cacheDir, "logs").apply { mkdirs() }
        directory
            .listFiles()
            ?.filter { it.name.startsWith("nekosu-logs-") }
            ?.sortedByDescending { it.lastModified() }
            ?.drop(4)
            ?.forEach { it.delete() }

        val timestamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
        val file = File(directory, "nekosu-logs-$timestamp.zip")
        val packageInfo =
            packageManager.getPackageInfo(
                packageName,
                0,
            )

        val deviceInfo =
            buildString {
                appendLine("NekoSU diagnostic archive")
                appendLine("Generated: ${Date()}")
                appendLine("Package: $packageName")
                appendLine("Version: ${packageInfo.versionName} (${packageInfo.longVersionCode})")
                appendLine("Device: ${Build.MANUFACTURER} ${Build.MODEL}")
                appendLine("Android: ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})")
                appendLine("ABI: ${Build.SUPPORTED_ABIS.joinToString()}")
                appendLine("Kernel: ${System.getProperty("os.version")}")
                appendLine("Fingerprint: ${Build.FINGERPRINT}")
                appendLine()
                appendLine("system/: Android logcat, kernel log and system properties.")
                appendLine("nekosu/: logs emitted by this process and NekoSU boot logs.")
            }

        val systemLogcat = captureCommand(listOf("logcat", "-d", "-v", "threadtime", "-t", "20000"))
        val processLogcat =
            captureCommand(
                listOf(
                    "logcat",
                    "-d",
                    "-v",
                    "threadtime",
                    "-t",
                    "20000",
                    "--pid=${android.os.Process.myPid()}",
                ),
            )
        val kernelLog = captureRootCommand("dmesg 2>&1 | tail -n 12000")
        val systemProperties = captureCommand(listOf("getprop"))
        val ncoreLogs =
            captureRootCommand(
                "for f in /data/adb/nksu/log/*.log; do " +
                    "[ -f \"\$f\" ] || continue; " +
                    "echo; echo \"--- \$f ---\"; tail -n 12000 \"\$f\"; done 2>&1",
            )

        file.outputStream().buffered().use { output ->
            ZipOutputStream(output).use { archive ->
                addArchiveEntry(archive, "device-info.txt", deviceInfo)
                addArchiveEntry(archive, "system/logcat.txt", systemLogcat)
                addArchiveEntry(archive, "system/dmesg.txt", kernelLog)
                addArchiveEntry(archive, "system/properties.txt", systemProperties)
                addArchiveEntry(archive, "nekosu/logcat.txt", processLogcat)
                addArchiveEntry(archive, "nekosu/ncore-boot.log", ncoreLogs)
            }
        }
        return file
    }

    private fun addArchiveEntry(
        archive: ZipOutputStream,
        name: String,
        content: String,
    ) {
        archive.putNextEntry(ZipEntry(name))
        archive.write(content.toByteArray(Charsets.UTF_8))
        archive.closeEntry()
    }

    private fun captureCommand(command: List<String>): String {
        return try {
            readProcess(ProcessBuilder(command).redirectErrorStream(true).start())
        } catch (error: Exception) {
            "Unavailable: ${error.message}\n"
        }
    }

    private fun captureRootCommand(command: String): String {
        return try {
            readProcess(
                ProcessBuilder("su", "-c", command)
                    .redirectErrorStream(true)
                    .start(),
            )
        } catch (error: Exception) {
            "Unavailable: ${error.message}\n"
        }
    }

    private fun readProcess(process: Process): String {
        val output = process.inputStream.bufferedReader().use { it.readText() }
        if (!process.waitFor(20, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            return output + "\n[command timed out]\n"
        }
        return output.ifBlank { "[no output, exit=${process.exitValue()}]\n" }
    }
}
