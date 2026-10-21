package com.example.nekosu

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.view.ViewGroup
import android.webkit.ConsoleMessage
import android.webkit.JavascriptInterface
import android.webkit.JsPromptResult
import android.webkit.JsResult
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebChromeClient.FileChooserParams
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.EditText
import androidx.core.content.pm.PackageInfoCompat
import androidx.core.graphics.drawable.toBitmap
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.webkit.WebViewAssetLoader
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.StandardMessageCodec
import io.flutter.plugin.platform.PlatformView
import io.flutter.plugin.platform.PlatformViewFactory
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.net.URLConnection
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.math.roundToInt

private const val WEBUI_DOMAIN = "mui.kernelsu.org"
private const val WEBUI_CHANNEL_PREFIX = "nekosu/webui/"
private const val ICON_SIZE = 192
private const val BRIDGE_TIMEOUT_SECONDS = 300L

class WebUiViewFactory(
    private val messenger: BinaryMessenger,
) : PlatformViewFactory(StandardMessageCodec.INSTANCE) {
    override fun create(
        context: Context,
        viewId: Int,
        args: Any?,
    ): PlatformView {
        val params = args as? Map<*, *> ?: emptyMap<Any, Any>()
        return WebUiView(
            context = context,
            messenger = messenger,
            viewId = viewId,
            moduleId = params["moduleId"]?.toString().orEmpty(),
            webRootPath = params["webRoot"]?.toString().orEmpty(),
            entryName = params["entryName"]?.toString() ?: "index.html",
            colorsCss = params["colorsCss"]?.toString().orEmpty(),
            moduleInfo = (params["module"] as? Map<*, *>)?.entries?.associate { it.key.toString() to it.value }
                ?: emptyMap(),
        )
    }
}

@SuppressLint("SetJavaScriptEnabled")
internal class WebUiView(
    context: Context,
    messenger: BinaryMessenger,
    viewId: Int,
    private val moduleId: String,
    private val webRootPath: String,
    entryName: String,
    private val colorsCss: String,
    moduleInfo: Map<String, Any?>,
) : PlatformView {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val channel = MethodChannel(messenger, "$WEBUI_CHANNEL_PREFIX$viewId")
    private val bridgeExecutor = Executors.newCachedThreadPool()
    private val iconCache = ConcurrentHashMap<String, ByteArray>()
    private val moduleInfoJson =
        JSONObject(moduleInfo).apply {
            put("moduleDir", "/data/adb/modules/$moduleId")
        }.toString()
    private val homeUrl = "https://$WEBUI_DOMAIN/${Uri.encode(entryName)}"
    private val webView: WebView
    @Volatile private var currentInsetsValue = Insets(0, 0, 0, 0)
    @Volatile private var insetsEnabled = false
    private var loaded = false
    private var viewSized = false
    private var dartReady = false
    private var disposed = false

    init {
        webView =
            WebView(context).apply {
                layoutParams =
                    ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT,
                    )
                setBackgroundColor(Color.TRANSPARENT)
                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                settings.allowFileAccess = false
                settings.allowContentAccess = false
            }

        val root = File(webRootPath)
        val assetLoader =
            WebViewAssetLoader
                .Builder()
                .setDomain(WEBUI_DOMAIN)
                .addPathHandler(
                    "/",
                    ModulePathHandler(
                        root = root,
                        insetsProvider = { currentInsetsValue },
                        onInsetsRequested = ::requestInsets,
                        colorsProvider = { colorsCss },
                    ),
                ).build()

        webView.webViewClient =
            object : WebViewClient() {
                override fun shouldInterceptRequest(
                    view: WebView,
                    request: WebResourceRequest,
                ): WebResourceResponse? {
                    val url = request.url
                    if (url.scheme.equals("ksu", ignoreCase = true) &&
                        url.host.equals("icon", ignoreCase = true)
                    ) {
                        return iconResponse(url.path?.trimStart('/').orEmpty())
                    }
                    val response = assetLoader.shouldInterceptRequest(url)
                    if (response == null && url.host.equals(WEBUI_DOMAIN, ignoreCase = true)) {
                        Log.w("NksuWebUI", "asset miss: $url")
                    }
                    return response
                }

                override fun onPageStarted(
                    view: WebView,
                    url: String,
                    favicon: Bitmap?,
                ) {
                    notifyDart("pageStarted", null)
                }

                override fun onPageFinished(
                    view: WebView,
                    url: String,
                ) {
                    view.evaluateJavascript(
                        "(function(){try{var b=document.body;return (b?b.innerText.length:-1)+'/'+" +
                            "(b?b.innerHTML.length:-1)+' viewport='+window.innerWidth+'x'+window.innerHeight;}catch(e){return 'err:'+e;}})()",
                    ) { result -> Log.d("NksuWebUI", "dom=$result") }
                    notifyDart("pageFinished", null)
                }

                override fun onReceivedError(
                    view: WebView,
                    request: WebResourceRequest,
                    error: WebResourceError,
                ) {
                    if (!request.isForMainFrame) return
                    val message = "${error.errorCode}: ${error.description}"
                    Log.w("NksuWebUI", "load error: $message for ${request.url}")
                    notifyDart("pageError", message)
                }

                override fun onReceivedHttpError(
                    view: WebView,
                    request: WebResourceRequest,
                    errorResponse: WebResourceResponse,
                ) {
                    if (!request.isForMainFrame) return
                    val message = "HTTP ${errorResponse.statusCode}: ${errorResponse.reasonPhrase.orEmpty()}"
                    Log.w("NksuWebUI", "load error: $message for ${request.url}")
                    notifyDart("pageError", message)
                }
            }

        webView.webChromeClient =
            object : WebChromeClient() {
                override fun onConsoleMessage(consoleMessage: ConsoleMessage?): Boolean {
                    Log.d(
                        "NksuWebUI",
                        "${consoleMessage?.message()} @ ${consoleMessage?.sourceId()}:${consoleMessage?.lineNumber()}",
                    )
                    return true
                }

                override fun onJsAlert(
                    view: WebView,
                    url: String?,
                    message: String?,
                    result: JsResult?,
                ): Boolean {
                    if (message == null || result == null) return false
                    val activity = findActivity(view.context)
                    if (activity == null) {
                        result.cancel()
                        return true
                    }
                    AlertDialog
                        .Builder(activity)
                        .setMessage(message)
                        .setPositiveButton(android.R.string.ok) { _, _ -> result.confirm() }
                        .setOnCancelListener { result.cancel() }
                        .show()
                    return true
                }

                override fun onJsConfirm(
                    view: WebView,
                    url: String?,
                    message: String?,
                    result: JsResult?,
                ): Boolean {
                    if (message == null || result == null) return false
                    val activity = findActivity(view.context)
                    if (activity == null) {
                        result.cancel()
                        return true
                    }
                    AlertDialog
                        .Builder(activity)
                        .setMessage(message)
                        .setPositiveButton(android.R.string.ok) { _, _ -> result.confirm() }
                        .setNegativeButton(android.R.string.cancel) { _, _ -> result.cancel() }
                        .setOnCancelListener { result.cancel() }
                        .show()
                    return true
                }

                override fun onJsPrompt(
                    view: WebView,
                    url: String?,
                    message: String?,
                    defaultValue: String?,
                    result: JsPromptResult?,
                ): Boolean {
                    if (message == null || result == null) return false
                    val activity = findActivity(view.context)
                    if (activity == null) {
                        result.cancel()
                        return true
                    }
                    val input = EditText(activity).apply { setText(defaultValue.orEmpty()) }
                    AlertDialog
                        .Builder(activity)
                        .setMessage(message)
                        .setView(input)
                        .setPositiveButton(android.R.string.ok) { _, _ ->
                            result.confirm(input.text.toString())
                        }.setNegativeButton(android.R.string.cancel) { _, _ -> result.cancel() }
                        .setOnCancelListener { result.cancel() }
                        .show()
                    return true
                }

                override fun onShowFileChooser(
                    view: WebView,
                    callback: ValueCallback<Array<Uri>>?,
                    params: FileChooserParams?,
                ): Boolean {
                    val activity = findActivity(view.context) ?: return false
                    if (callback == null) return false
                    WebUiFileChooser.open(activity, params, callback)
                    return true
                }
            }

        webView.addJavascriptInterface(WebUiBridge(), "ksu")

        channel.setMethodCallHandler { call, result ->
            when (call.method) {
                "reload" -> {
                    webView.reload()
                    result.success(null)
                }
                "canGoBack" -> result.success(webView.canGoBack())
                "goBack" -> {
                    if (webView.canGoBack()) webView.goBack()
                    result.success(null)
                }
                "ready" -> {
                    dartReady = true
                    maybeLoad()
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }

        ViewCompat.setOnApplyWindowInsetsListener(webView) { _, insets ->
            currentInsetsValue = insetsFrom(insets)
            if (insetsEnabled) eval(currentInsetsValue.js)
            insets
        }

        webView.addOnLayoutChangeListener { _, left, top, right, bottom, _, _, _, _ ->
            ViewCompat.getRootWindowInsets(webView)?.let { currentInsetsValue = insetsFrom(it) }
            viewSized = right > left && bottom > top
            maybeLoad()
        }
        webView.post {
            viewSized = webView.width > 0 && webView.height > 0
            maybeLoad()
        }
    }

    override fun getView(): View = webView

    override fun dispose() {
        disposed = true
        mainHandler.removeCallbacksAndMessages(null)
        bridgeExecutor.shutdownNow()
        channel.setMethodCallHandler(null)
        (webView.parent as? ViewGroup)?.removeView(webView)
        webView.stopLoading()
        webView.loadUrl("about:blank")
        webView.destroy()
    }

    private fun notifyDart(
        method: String,
        arguments: Any?,
    ) {
        mainHandler.post {
            if (!disposed) channel.invokeMethod(method, arguments)
        }
    }

    private fun eval(js: String) {
        mainHandler.post {
            if (!disposed) webView.evaluateJavascript(js, null)
        }
    }

    private fun maybeLoad() {
        if (loaded || disposed || !viewSized || !dartReady) return
        loaded = true
        Log.d("NksuWebUI", "loading $homeUrl (root=$webRootPath)")
        webView.loadUrl(homeUrl)
    }

    private fun insetsFrom(insets: WindowInsetsCompat?): Insets {
        if (insets == null) return Insets(0, 0, 0, 0)
        val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
        val density = webView.resources.displayMetrics.density
        return Insets(
            top = (bars.top / density).roundToInt(),
            bottom = (bars.bottom / density).roundToInt(),
            left = (bars.left / density).roundToInt(),
            right = (bars.right / density).roundToInt(),
        )
    }

    private fun requestInsets() {
        if (!insetsEnabled) {
            insetsEnabled = true
            notifyDart("edgeToEdge", true)
        }
        eval(currentInsetsValue.js)
    }

    private fun dartCallSync(
        method: String,
        arguments: Any?,
    ): Any? {
        val latch = CountDownLatch(1)
        var value: Any? = null
        var failure: RuntimeException? = null
        mainHandler.post {
            if (disposed) {
                failure = IllegalStateException("WebUI is closed")
                latch.countDown()
                return@post
            }
            channel.invokeMethod(
                method,
                arguments,
                object : MethodChannel.Result {
                    override fun success(result: Any?) {
                        value = result
                        latch.countDown()
                    }

                    override fun error(
                        errorCode: String,
                        errorMessage: String?,
                        errorDetails: Any?,
                    ) {
                        failure = RuntimeException("$errorCode: ${errorMessage ?: "WebUI bridge call failed"}")
                        latch.countDown()
                    }

                    override fun notImplemented() {
                        failure = UnsupportedOperationException("Unsupported WebUI call: $method")
                        latch.countDown()
                    }
                },
            )
        }
        if (!latch.await(BRIDGE_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
            throw IllegalStateException("WebUI bridge call timed out")
        }
        failure?.let { throw it }
        return value
    }

    private fun commandPayload(
        command: String,
        options: String?,
        args: String? = null,
    ): Map<String, Any?> =
        buildMap {
            put("command", command)
            put("options", options)
            if (args != null) put("args", args)
        }

    private fun executeSync(payload: Map<String, Any?>): Map<*, *> =
        dartCallSync("exec", payload) as? Map<*, *>
            ?: throw IllegalStateException("Invalid WebUI command result")

    private fun emitExecCallback(
        callbackName: String,
        result: Map<*, *>,
    ) {
        val code = (result["code"] as? Number)?.toInt() ?: -1
        val stdout = result["stdout"]?.toString().orEmpty()
        val stderr = result["stderr"]?.toString().orEmpty()
        eval(
            "(function(){try{var cb=window[${JSONObject.quote(callbackName)}];" +
                "if(typeof cb!=='function')return;" +
                "cb($code,${JSONObject.quote(stdout)},${JSONObject.quote(stderr)});}" +
                "catch(e){console.error(e);}})();",
        )
    }

    private fun emitExecError(
        callbackName: String,
        error: Throwable,
    ) {
        val message = error.message ?: error.toString()
        eval(
            "(function(){try{var cb=window[${JSONObject.quote(callbackName)}];" +
                "if(typeof cb!=='function')return;" +
                "cb(-1,'',${JSONObject.quote(message)});}" +
                "catch(e){console.error(e);}})();",
        )
    }

    private fun startExecCallback(
        payload: Map<String, Any?>,
        callbackName: String,
    ) {
        bridgeExecutor.execute {
            try {
                emitExecCallback(callbackName, executeSync(payload))
            } catch (error: Throwable) {
                emitExecError(callbackName, error)
            }
        }
    }

    private fun emitSpawnCallback(
        callbackName: String,
        result: Map<*, *>,
    ) {
        val code = (result["code"] as? Number)?.toInt() ?: -1
        val stdout = result["stdout"]?.toString().orEmpty()
        val stderr = result["stderr"]?.toString().orEmpty()
        val name = JSONObject.quote(callbackName)
        val js =
            buildString {
                append("(function(){try{var cb=window[$name];if(!cb)return;")
                if (stdout.isNotEmpty()) {
                    append("cb.stdout.emit('data',${JSONObject.quote(stdout)});")
                }
                if (stderr.isNotEmpty()) {
                    append("cb.stderr.emit('data',${JSONObject.quote(stderr)});")
                }
                append("cb.emit('exit',$code);")
                if (code != 0) {
                    append("var err=new Error(${JSONObject.quote(stderr)});")
                    append("err.exitCode=$code;cb.emit('error',err);")
                }
                append("}catch(e){console.error(e);}})();")
            }
        eval(js)
    }

    private fun startSpawnCallback(
        payload: Map<String, Any?>,
        callbackName: String,
    ) {
        bridgeExecutor.execute {
            try {
                emitSpawnCallback(callbackName, executeSync(payload))
            } catch (error: Throwable) {
                val message = error.message ?: error.toString()
                eval(
                    "(function(){try{var cb=window[${JSONObject.quote(callbackName)}];" +
                        "if(!cb)return;var e=new Error(${JSONObject.quote(message)});" +
                        "e.exitCode=-1;cb.emit('error',e);cb.emit('exit',-1);}" +
                        "catch(e){console.error(e);}})();",
                )
            }
        }
    }

    private fun listPackages(type: String): String {
        val pm = webView.context.packageManager
        val normalized = type.lowercase()
        val names =
            pm
                .getInstalledPackages(0)
                .filter { pkg ->
                    val flags = pkg.applicationInfo?.flags ?: 0
                    when (normalized) {
                        "system" -> flags and ApplicationInfo.FLAG_SYSTEM != 0
                        "user" -> flags and ApplicationInfo.FLAG_SYSTEM == 0
                        else -> true
                    }
                }.map { it.packageName }
                .sorted()
        return JSONArray(names).toString()
    }

    private fun getPackagesInfo(packageNamesJson: String): String {
        val pm = webView.context.packageManager
        val out = JSONArray()
        runCatching {
            val names = JSONArray(packageNamesJson)
            for (index in 0 until names.length()) {
                val name = names.getString(index)
                val obj = JSONObject().put("packageName", name)
                try {
                    val pkg = pm.getPackageInfo(name, 0)
                    val app = pkg.applicationInfo
                    obj.put("versionName", pkg.versionName ?: "")
                    obj.put("versionCode", PackageInfoCompat.getLongVersionCode(pkg))
                    obj.put("appLabel", app?.loadLabel(pm)?.toString() ?: name)
                    obj.put(
                        "isSystem",
                        app != null && (app.flags and ApplicationInfo.FLAG_SYSTEM) != 0,
                    )
                    obj.put("uid", app?.uid ?: JSONObject.NULL)
                } catch (_: Exception) {
                    obj.put("error", "Package not found or inaccessible")
                }
                out.put(obj)
            }
        }
        return out.toString()
    }

    private fun iconResponse(packageName: String): WebResourceResponse {
        val headers = mapOf("Access-Control-Allow-Origin" to "*")
        val bytes =
            if (packageName.isBlank()) {
                null
            } else {
                iconCache[packageName] ?: runCatching {
                    val drawable = webView.context.packageManager.getApplicationIcon(packageName)
                    val bitmap = drawable.toBitmap(ICON_SIZE, ICON_SIZE)
                    ByteArrayOutputStream().use { out ->
                        bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)
                        out.toByteArray()
                    }
                }.getOrNull()?.also { iconCache[packageName] = it }
            }
        return if (bytes != null) {
            WebResourceResponse(
                "image/png",
                null,
                200,
                "OK",
                headers,
                ByteArrayInputStream(bytes),
            )
        } else {
            WebResourceResponse(
                "text/plain",
                "utf-8",
                404,
                "Not Found",
                headers,
                ByteArrayInputStream("No such package".toByteArray()),
            )
        }
    }

    inner class WebUiBridge {
        @JavascriptInterface
        fun exec(command: String): String {
            val result = executeSync(commandPayload(command, null))
            val stdout = result["stdout"]?.toString().orEmpty()
            val stderr = result["stderr"]?.toString().orEmpty()
            return stdout + stderr
        }

        @JavascriptInterface
        fun exec(
            command: String,
            callbackName: String,
        ) {
            startExecCallback(commandPayload(command, null), callbackName)
        }

        @JavascriptInterface
        fun exec(
            command: String,
            options: String?,
            callbackName: String,
        ) {
            startExecCallback(commandPayload(command, options), callbackName)
        }

        @JavascriptInterface
        fun spawn(
            command: String,
            args: String,
            options: String?,
            callbackName: String,
        ) {
            startSpawnCallback(commandPayload(command, options, args), callbackName)
        }

        @JavascriptInterface
        fun toast(message: String) {
            notifyDart("toast", message)
        }

        @JavascriptInterface
        fun moduleInfo(): String = moduleInfoJson

        @JavascriptInterface
        fun listPackages(type: String): String = this@WebUiView.listPackages(type)

        @JavascriptInterface
        fun getPackagesInfo(packageNames: String): String = this@WebUiView.getPackagesInfo(packageNames)

        @JavascriptInterface
        fun exit() {
            notifyDart("exit", null)
        }

        @JavascriptInterface
        fun enableEdgeToEdge(enable: Boolean) {
            insetsEnabled = enable
            notifyDart("edgeToEdge", enable)
            eval(currentInsetsValue.js)
        }

        @JavascriptInterface
        fun fullScreen(enable: Boolean) {
            notifyDart("fullScreen", enable)
        }
    }
}

private class ModulePathHandler(
    private val root: File,
    private val insetsProvider: () -> Insets,
    private val onInsetsRequested: () -> Unit,
    private val colorsProvider: () -> String,
) : WebViewAssetLoader.PathHandler {
    override fun handle(path: String): WebResourceResponse {
        when (path) {
            "internal/insets.css" -> {
                onInsetsRequested()
                return css(insetsProvider().css)
            }
            "internal/colors.css" -> return css(colorsProvider())
        }

        val rootPath = root.toPath().normalize()
        var target = File(root, path).toPath().normalize()
        if (path.isBlank() || path.endsWith('/')) {
            target = target.resolve("index.html")
        }
        if (!target.startsWith(rootPath)) return notFound(path)

        val file = target.toFile()
        if (!file.isFile) return notFound(path)
        val bytes =
            runCatching { file.readBytes() }.getOrElse {
                Log.w("NksuWebUI", "unable to read webui:$path (root=$root)", it)
                return notFound(path)
            }
        return WebResourceResponse(mime(file.name), null, ByteArrayInputStream(bytes))
    }

    private fun css(text: String): WebResourceResponse =
        WebResourceResponse("text/css", "utf-8", ByteArrayInputStream(text.toByteArray(Charsets.UTF_8)))

    private fun notFound(path: String): WebResourceResponse {
        val html =
            "<!doctype html><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" +
                "<body style=\"font-family:monospace;padding:16px;line-height:1.6\">" +
                "<h3>NekoSU WebUI</h3><p>404: ${escapeHtml(path.ifBlank { "index.html" })}</p>" +
                "<p>webroot: ${escapeHtml(root.absolutePath)}</p>" +
                "<p>请确认模块存在 webroot/index.html，且管理器已获得 root。</p></body>"
        return WebResourceResponse(
            "text/html",
            "utf-8",
            404,
            "Not Found",
            emptyMap(),
            ByteArrayInputStream(html.toByteArray(Charsets.UTF_8)),
        )
    }

    private fun escapeHtml(value: String): String =
        value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

    private fun mime(name: String): String {
        val extension = name.substringAfterLast('.', "").lowercase()
        return mimeOverrides[extension] ?: URLConnection.guessContentTypeFromName(name) ?: "text/plain"
    }

    private companion object {
        val mimeOverrides =
            mapOf(
                "html" to "text/html",
                "htm" to "text/html",
                "js" to "application/javascript",
                "mjs" to "application/javascript",
                "css" to "text/css",
                "json" to "application/json",
                "map" to "application/json",
                "webmanifest" to "application/manifest+json",
                "svg" to "image/svg+xml",
                "png" to "image/png",
                "jpg" to "image/jpeg",
                "jpeg" to "image/jpeg",
                "gif" to "image/gif",
                "webp" to "image/webp",
                "ico" to "image/x-icon",
                "woff" to "font/woff",
                "woff2" to "font/woff2",
                "ttf" to "font/ttf",
                "otf" to "font/otf",
                "wasm" to "application/wasm",
                "txt" to "text/plain",
                "xml" to "text/xml",
                "mp4" to "video/mp4",
                "webm" to "video/webm",
                "mp3" to "audio/mpeg",
                "ogg" to "audio/ogg",
            )
    }
}

private data class Insets(
    val top: Int,
    val bottom: Int,
    val left: Int,
    val right: Int,
) {
    val css: String
        get() =
            buildString {
                appendLine(":root {")
                appendLine("\t--safe-area-inset-top: ${top}px;")
                appendLine("\t--safe-area-inset-right: ${right}px;")
                appendLine("\t--safe-area-inset-bottom: ${bottom}px;")
                appendLine("\t--safe-area-inset-left: ${left}px;")
                appendLine("\t--window-inset-top: var(--safe-area-inset-top, 0px);")
                appendLine("\t--window-inset-bottom: var(--safe-area-inset-bottom, 0px);")
                appendLine("\t--window-inset-left: var(--safe-area-inset-left, 0px);")
                appendLine("\t--window-inset-right: var(--safe-area-inset-right, 0px);")
                appendLine("\t--f7-safe-area-top: var(--window-inset-top, 0px) !important;")
                appendLine("\t--f7-safe-area-bottom: var(--window-inset-bottom, 0px) !important;")
                appendLine("\t--f7-safe-area-left: var(--window-inset-left, 0px) !important;")
                appendLine("\t--f7-safe-area-right: var(--window-inset-right, 0px) !important;")
                append("}")
            }

    val js: String
        get() =
            "(function(){var s=document.documentElement.style;" +
                "s.setProperty('--safe-area-inset-top','${top}px');" +
                "s.setProperty('--safe-area-inset-right','${right}px');" +
                "s.setProperty('--safe-area-inset-bottom','${bottom}px');" +
                "s.setProperty('--safe-area-inset-left','${left}px');})();"
}

internal object WebUiFileChooser {
    private const val REQUEST_CODE = 4312
    private var callback: ValueCallback<Array<Uri>>? = null

    fun open(
        activity: Activity,
        params: FileChooserParams?,
        callback: ValueCallback<Array<Uri>>,
    ) {
        this.callback?.onReceiveValue(null)
        this.callback = callback
        val intent =
            params?.createIntent()
                ?: Intent(Intent.ACTION_GET_CONTENT).apply { type = "*/*" }
        if (params?.mode == FileChooserParams.MODE_OPEN_MULTIPLE) {
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        }
        runCatching { activity.startActivityForResult(intent, REQUEST_CODE) }
            .onFailure {
                this.callback = null
                callback.onReceiveValue(null)
            }
    }

    fun onActivityResult(
        requestCode: Int,
        resultCode: Int,
        data: Intent?,
    ): Boolean {
        if (requestCode != REQUEST_CODE) return false
        val pending = callback ?: return true
        callback = null
        if (resultCode != Activity.RESULT_OK) {
            pending.onReceiveValue(null)
            return true
        }
        val uris =
            data?.clipData?.let { clip ->
                Array(clip.itemCount) { index -> clip.getItemAt(index).uri }
            } ?: data?.data?.let { uri -> arrayOf(uri) }
        pending.onReceiveValue(uris)
        return true
    }
}

private fun findActivity(context: Context): Activity? {
    var current: Context? = context
    while (current is ContextWrapper) {
        if (current is Activity) return current
        current = current.baseContext
    }
    return current as? Activity
}
