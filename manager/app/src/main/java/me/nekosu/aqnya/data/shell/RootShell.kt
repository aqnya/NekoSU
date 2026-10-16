package me.nekosu.aqnya.data.shell

import java.util.concurrent.TimeUnit
import me.nekosu.aqnya.core.Ncore

object RootShell {
    data class Result(
        val code: Int,
        val output: String,
    ) {
        val ok: Boolean get() = code == 0
    }

    private const val SU_PATH = "/system/bin/su"

    @Volatile
    private var rootConfirmed = false

    fun available(): Boolean {
        if (rootConfirmed) return true
        val r = runCapture(listOf(SU_PATH, "-c", withPath("id")), timeoutSeconds = 60) ?: return false
        val ok = r.code == 0 && r.output.contains("uid=0")
        if (ok) rootConfirmed = true
        return ok
    }

    fun exec(cmd: String): Result {
        val full = withPath(cmd)
        return runCapture(listOf(SU_PATH, "-c", full), timeoutSeconds = 0)
            ?: Result(-1, "root shell unavailable")
    }

    /**
     * 以 root 重启设备。命令一般不会返回（设备随即重启），
     * 因此调用方应在 IO 线程执行，避免阻塞 UI。
     */
    fun reboot(): Boolean = exec("svc power reboot").ok

    /**
     * 以 root 读取文件。优先走常驻读取器（只 fork 一次，之后管道通信），读取器
     * 失败时回退到每次 fork 的 [Ncore.readFile]。
     */
    fun readFileBytes(path: String): ByteArray? {
        val viaReader = runCatching { Ncore.readFilePersistent(path) }.getOrNull()
        if (viaReader != null) return viaReader.takeIf { it.isNotEmpty() }
        return runCatching { Ncore.readFile(path) }.getOrNull()?.takeIf { it.isNotEmpty() }
    }

    fun execStreaming(
        cmd: String,
        onOutput: (String) -> Unit,
    ): Int {
        val full = withPath(cmd)
        return try {
            val process =
                ProcessBuilder(SU_PATH, "-c", full)
                    .redirectErrorStream(true)
                    .start()
            process.inputStream.bufferedReader().useLines { lines -> lines.forEach(onOutput) }
            process.waitFor()
        } catch (e: Exception) {
            onOutput("ERROR: ${e.message}")
            -1
        }
    }

    data class Output(
        val code: Int,
        val stdout: String,
        val stderr: String,
    )

    fun execSplit(cmd: String): Output {
        val full = withPath(cmd)
        return try {
            val process = ProcessBuilder(SU_PATH, "-c", full).redirectErrorStream(false).start()
            val stdout = StringBuilder()
            val stderr = StringBuilder()
            val outThread =
                Thread {
                    runCatching {
                        process.inputStream.bufferedReader().useLines { it.forEach { l -> stdout.append(l).append('\n') } }
                    }
                }
            val errThread =
                Thread {
                    runCatching {
                        process.errorStream.bufferedReader().useLines { it.forEach { l -> stderr.append(l).append('\n') } }
                    }
                }
            outThread.start()
            errThread.start()
            val code = process.waitFor()
            outThread.join(2000)
            errThread.join(2000)
            Output(code, stdout.toString(), stderr.toString())
        } catch (e: Exception) {
            Output(-1, "", e.message.orEmpty())
        }
    }

    fun execStreamingSplit(
        cmd: String,
        onStdout: (String) -> Unit,
        onStderr: (String) -> Unit,
    ): Int {
        val full = withPath(cmd)
        return try {
            val process = ProcessBuilder(SU_PATH, "-c", full).redirectErrorStream(false).start()
            val outThread =
                Thread {
                    runCatching {
                        process.inputStream.bufferedReader().useLines { it.forEach(onStdout) }
                    }
                }
            val errThread =
                Thread {
                    runCatching {
                        process.errorStream.bufferedReader().useLines { it.forEach(onStderr) }
                    }
                }
            outThread.start()
            errThread.start()
            val code = process.waitFor()
            outThread.join(2000)
            errThread.join(2000)
            code
        } catch (e: Exception) {
            onStderr("ERROR: ${e.message}")
            -1
        }
    }

    private fun withPath(cmd: String): String =
        "PATH=/sbin:/system/sbin:/system/bin:/system/xbin; export PATH; $cmd"

    private fun runCapture(
        command: List<String>,
        timeoutSeconds: Int,
    ): Result? =
        try {
            val process = ProcessBuilder(command).redirectErrorStream(true).start()
            val output = StringBuilder()
            val reader =
                Thread {
                    runCatching {
                        process.inputStream.bufferedReader().useLines { lines ->
                            lines.forEach { output.append(it).append('\n') }
                        }
                    }
                }
            reader.isDaemon = true
            reader.start()

            val finished =
                if (timeoutSeconds > 0) {
                    process.waitFor(timeoutSeconds.toLong(), TimeUnit.SECONDS)
                } else {
                    process.waitFor()
                    true
                }
            if (!finished) {
                process.destroyForcibly()
                null
            } else {
                reader.join(2000)
                Result(process.exitValue(), output.toString())
            }
        } catch (e: Exception) {
            null
        }
}
