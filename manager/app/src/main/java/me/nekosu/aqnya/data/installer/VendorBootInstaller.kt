package me.nekosu.aqnya.data.installer

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import java.io.File
import java.io.IOException
import kotlin.random.Random
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import me.nekosu.aqnya.R
import me.nekosu.aqnya.core.Ncore
import me.nekosu.aqnya.data.shell.RootShell

object VendorBootInstaller {

    /** 内核模块在 assets 中的存放目录，例如 assets/ko/android14-6.1_nksu.ko。 */
    private const val KO_ASSET_DIR = "ko"

    private const val NCORE_LIB_NAME = "libncore.so"

    /** 本地 LKM 在安装目录中暂存的文件名。 */
    private const val LOCAL_KO_NAME = "local_nksu.ko"

    /** 安装输出文件名: nekosu_<随机串>_vendor_boot.img。 */
    private const val OUTPUT_PREFIX = "nekosu_"
    private const val OUTPUT_SUFFIX = "_vendor_boot.img"

    /** 本次安装从分区 dump 的临时原镜像；仅用于 patch 与回滚，安装后删除。 */
    private const val SOURCE_IMAGE_NAME = "vendor_boot.source.img"
    private const val RANDOM_TOKEN_LENGTH = 8
    private const val RANDOM_ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789"

    private fun outputImageName(): String = "$OUTPUT_PREFIX${randomToken()}$OUTPUT_SUFFIX"

    fun installDir(context: Context): File =
        File(context.filesDir, "nksu-install").apply { mkdirs() }

    fun ncorePath(context: Context): File =
        File(context.applicationInfo.nativeLibraryDir, NCORE_LIB_NAME)

    /**
     * 把内核模块放到安装目录并返回其文件：
     *  - 传了 [koUri] 时复制用户选择的本地 .ko；
     *  - 否则从 assets/ko 复制打包的 [koName]。
     */
    private fun resolveKo(
        context: Context,
        koName: String?,
        koUri: Uri?,
    ): File? {
        val dir = installDir(context)
        if (koUri != null) {
            val out = File(dir, LOCAL_KO_NAME)
            out.delete()
            context.contentResolver.openInputStream(koUri)?.use { input ->
                out.outputStream().use { input.copyTo(it) }
            } ?: return null
            out.setReadable(true, false)
            return out
        }
        if (koName != null) {
            val out = File(dir, koName)
            copyAsset(context, "$KO_ASSET_DIR/$koName", out)
            out.setReadable(true, false)
            return out
        }
        return null
    }

    private fun copyAsset(context: Context, assetName: String, dest: File) {
        try {
            // Asset names may contain a directory prefix (e.g. "ko/...");
            // FileOutputStream does not create parents, so make them first.
            dest.parentFile?.mkdirs()
            context.assets.open(assetName).use { input ->
                dest.outputStream().use { output ->
                    input.copyTo(output)
                }
            }
        } catch (e: Exception) {
            throw IllegalStateException(
                context.getString(R.string.install_err_copy_asset, assetName, e.message.orEmpty()),
                e,
            )
        }
    }

    fun koCandidates(context: Context): List<String> =
        (context.assets.list(KO_ASSET_DIR) ?: emptyArray())
            .filter { it.endsWith("_nksu.ko") }
            .sorted()

    fun stageVendorBoot(
        context: Context,
        uri: Uri,
    ): File {
        val dir = installDir(context)
        val out = File(dir, "vendor_boot.img")
        context.contentResolver.openInputStream(uri)?.use { input ->
            out.outputStream().use { input.copyTo(it) }
        } ?: throw IllegalStateException(context.getString(R.string.install_err_read_file))
        return out
    }

    suspend fun install(
        context: Context,
        vendorBoot: File,
        koName: String?,
        koUri: Uri? = null,
        onOutput: (String) -> Unit,
    ): Int =
        withContext(Dispatchers.IO) {
            val dir = installDir(context)
            val ncore = ncorePath(context)
            if (!ncore.exists() || !ncore.canExecute()) {
                throw IllegalStateException(
                    context.getString(R.string.install_err_ncore, ncore.absolutePath),
                )
            }

            val koFile =
                resolveKo(context, koName, koUri)
                    ?: throw IllegalStateException(context.getString(R.string.install_err_ko_missing))

            val patched = patchImage(ncore, vendorBoot, koFile, onOutput)
            if (patched == null) {
                onOutput(context.getString(R.string.install_log_err_patch))
                return@withContext -1
            }

            val produced = File(dir, outputImageName())
            produced.delete()
            patched.copyTo(produced, overwrite = true)
            runCatching { exportToDownload(context, produced) }
                .onSuccess { onOutput(context.getString(R.string.install_log_exported, it.absolutePath)) }
                .onFailure { onOutput(context.getString(R.string.install_log_export_failed, it.message.orEmpty())) }
            0
        }

    /**
     * 用 ncore 直接把 [koFile] 打进 [sourceImage]（v3/v4 vendor_boot）：
     * ncore 负责解包、把模块安装到 `lib/modules/nksu.ko`、登记 `modules.load` /
     * `modules.dep`，保持各 ramdisk 的压缩与 v4 ramdisk 表，最后写出
     * `<image>.new`。成功返回该文件，失败返回 null。
     *
     * 只操作本地文件，以当前进程身份运行 ncore，无需 root。
     */
    private fun patchImage(
        ncore: File,
        sourceImage: File,
        koFile: File,
        onOutput: (String) -> Unit,
    ): File? {
        val dir = sourceImage.parentFile ?: return null
        val output = File(dir, "${sourceImage.name}.new")
        output.delete()
        val args = listOf("-p", sourceImage.absolutePath, koFile.absolutePath)
        if (!runNcore(ncore, args, dir, onOutput)) {
            return null
        }
        return output.takeIf { it.isFile }
    }

    /** 运行 ncore 并把输出逐行回调；退出码为 0 返回 true。 */
    private fun runNcore(
        ncore: File,
        args: List<String>,
        workDir: File,
        onOutput: (String) -> Unit,
    ): Boolean =
        try {
            val process =
                ProcessBuilder(listOf(ncore.absolutePath) + args)
                    .directory(workDir)
                    .redirectErrorStream(true)
                    .start()
            process.inputStream.bufferedReader().useLines { lines -> lines.forEach(onOutput) }
            process.waitFor() == 0
        } catch (e: IOException) {
            onOutput(e.message.orEmpty())
            false
        }

    fun detectVendorBootPartition(): String? = findVendorBoot(currentSlotSuffix(), allowSlotless = true)

    fun detectInactiveVendorBootPartition(): String? {
        val inactive = inactiveSlotSuffix() ?: return null
        return findVendorBoot(inactive, allowSlotless = false)
    }

    fun currentSlotSuffix(): String? = Ncore.getSlotSuffix()

    /** 非活动 slot 后缀；非 A/B 设备返回 null。 */
    fun inactiveSlotSuffix(): String? =
        when (currentSlotSuffix()) {
            "_a" -> "_b"
            "_b" -> "_a"
            else -> null
        }

    /**
     * 查找 vendor_boot 分区路径。
     *
     * @slotSuffix null/空按无后缀处理；@allowSlotless 允许回退到无后缀的
     * `vendor_boot` 链接（只对当前 slot 安全，非活动 slot 不能回退，否则会
     * 误写当前分区）。
     */
    private fun findVendorBoot(
        slotSuffix: String?,
        allowSlotless: Boolean,
    ): String? = Ncore.findVendorBoot(slotSuffix.orEmpty(), allowSlotless)

    /**
     * 直接安装：以 root 读取指定 vendor_boot 分区，打入 nksu.ko 后写回，
     * 不再需要用户手动 fastboot。
     *
     * 流程：dump 原镜像 -> `ncore -p <镜像> <ko>` 打补丁 -> 导出补丁镜像 ->
     * 写回分区 -> 读回校验（失败则自动回滚原镜像）。KernelSU 的「直接安装 /
     * 刷入非活动分区」都走这里，区别只是 @partition 指向当前或非活动 slot。
     *
     * patch 全程只操作本地临时文件，不需要 root；只有 dump / 写回分区用到 su。
     * 不再保留原镜像备份：每次安装都从分区现 dump 一份临时镜像供 patch 与
     * 失败回滚使用，结束即删除。
     *
     * 需要设备已具备 root（Magisk/KernelSU 等 su，或已装 nksu）。
     */
    suspend fun directInstall(
        context: Context,
        koName: String?,
        koUri: Uri? = null,
        partition: String,
        onOutput: (String) -> Unit,
    ): Int =
        withContext(Dispatchers.IO) {
            val dir = installDir(context)
            val ncore = ncorePath(context)

            if (!ncore.exists() || !ncore.canExecute()) {
                onOutput(context.getString(R.string.install_log_err_ncore, ncore.absolutePath))
                return@withContext -1
            }
            val koFile = resolveKo(context, koName, koUri)
            if (koFile == null || !koFile.exists()) {
                onOutput(context.getString(R.string.install_log_err_ko, koName ?: koUri?.toString().orEmpty()))
                return@withContext -1
            }
            if (partition.isBlank()) {
                onOutput(context.getString(R.string.install_log_err_no_part))
                return@withContext -1
            }
            if (!RootShell.available()) {
                onOutput(context.getString(R.string.install_log_err_no_root))
                return@withContext -1
            }

            val part = partition
            onOutput(context.getString(R.string.install_log_partition, part))

            // 已具备 root：顺便把 ncore 放到 init.rc 会 exec 的固定路径，
            // 免去“首次重启后再由管理器补装、需二次重启”的往返。
            if (NcoreBoot.install(context)) {
                onOutput(context.getString(R.string.install_log_ncore_installed, NcoreBoot.BOOT_PATH))
            } else {
                onOutput(context.getString(R.string.install_log_ncore_warn))
            }

            // 1) 从当前分区 dump 一份原镜像，仅用于本次 patch 与校验失败回滚；
            //    不导出、不跨次复用，安装结束后删除。
            val source = File(dir, SOURCE_IMAGE_NAME)
            source.delete()
            source.createNewFile()
            if (!Ncore.writeBlock(part, source.absolutePath) || !source.exists()) {
                onOutput(context.getString(R.string.install_log_err_dump))
                source.delete()
                return@withContext -1
            }

            val produced = File(dir, outputImageName())
            produced.delete()

            // 2) 交给 ncore 直接把模块打进镜像（无需 root），得到补丁镜像。
            val patched = patchImage(ncore, source, koFile, onOutput)
            if (patched == null) {
                onOutput(context.getString(R.string.install_log_err_patch))
                source.delete()
                return@withContext -1
            }
            patched.copyTo(produced, overwrite = true)
            runCatching { exportToDownload(context, produced) }
                .onSuccess { onOutput(context.getString(R.string.install_log_patched_exported, it.absolutePath)) }
                .onFailure { onOutput(context.getString(R.string.install_log_patched_export_failed, it.message.orEmpty())) }

            // 3) 写回分区并读回校验（容量检查在 writeBlock 内）；不一致则回滚原镜像。
            val size = produced.length()

            if (!Ncore.writeBlock(produced.absolutePath, part)) {
                onOutput(context.getString(R.string.install_log_err_flash))
                source.delete()
                return@withContext -1
            }

            val readback = File(dir, "vendor_boot.readback.img")
            readback.delete()
            readback.createNewFile()
            val verified =
                Ncore.writeBlock(part, readback.absolutePath) &&
                    readback.exists() &&
                    filesMatchPrefix(produced, readback, size)
            readback.delete()
            if (!verified) {
                onOutput(context.getString(R.string.install_log_verify_failed))
                Ncore.writeBlock(source.absolutePath, part)
                source.delete()
                onOutput(context.getString(R.string.install_log_err_flash))
                return@withContext -1
            }

            source.delete()
            onOutput(context.getString(R.string.install_log_verify_ok))
            onOutput(context.getString(R.string.install_log_direct_done))
            0
        }

    private fun randomToken(): String =
        (1..RANDOM_TOKEN_LENGTH)
            .map { RANDOM_ALPHABET[Random.nextInt(RANDOM_ALPHABET.length)] }
            .joinToString("")

    /** 分块比较 [a] 与 [b] 的前 [length] 字节，避免整段读进内存。 */
    private fun filesMatchPrefix(
        a: File,
        b: File,
        length: Long,
    ): Boolean {
        if (length <= 0 || a.length() < length || b.length() < length) return false
        a.inputStream().use { ia ->
            b.inputStream().use { ib ->
                val bufA = ByteArray(1 shl 16)
                val bufB = ByteArray(1 shl 16)
                var remaining = length
                while (remaining > 0) {
                    val want = minOf(remaining, bufA.size.toLong()).toInt()
                    if (readFully(ia, bufA, want) != want) return false
                    if (readFully(ib, bufB, want) != want) return false
                    for (i in 0 until want) {
                        if (bufA[i] != bufB[i]) return false
                    }
                    remaining -= want
                }
            }
        }
        return true
    }

    private fun readFully(
        input: java.io.InputStream,
        buffer: ByteArray,
        length: Int,
    ): Int {
        var off = 0
        while (off < length) {
            val n = input.read(buffer, off, length - off)
            if (n < 0) break
            off += n
        }
        return off
    }

    /**
     * 把生成的补丁镜像导出到 /sdcard/Download。
     *
     * - 已授予「所有文件访问」时直接写入公共目录，保持生成的名称
     *   (Download/nekosu_<随机串>_vendor_boot.img)。
     * - 否则走 MediaStore.Downloads，无需存储权限即可发布文件。
     *
     * 返回导出后的目标文件。
     */
    fun exportToDownload(
        context: Context,
        source: File,
    ): File {
        val downloads =
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)

        // 没有「所有文件访问」就写不了公共目录，改走 MediaStore。
        if (!Environment.isExternalStorageManager()) {
            return exportViaMediaStore(context, source, downloads)
        }

        if (!downloads.exists() && !downloads.mkdirs()) {
            throw IllegalStateException(context.getString(R.string.install_err_mkdir, downloads.absolutePath))
        }
        val dest = File(downloads, source.name)
        source.copyTo(dest, overwrite = true)
        return dest
    }

    private fun exportViaMediaStore(
        context: Context,
        source: File,
        downloads: File,
    ): File {
        val resolver = context.contentResolver
        val collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI
        // 删除同名旧条目, 保证导出路径/文件名稳定 (失败不致命)。
        runCatching {
            resolver.delete(
                collection,
                "${MediaStore.MediaColumns.DISPLAY_NAME}=?",
                arrayOf(source.name),
            )
        }

        val values =
            ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, source.name)
                put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream")
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            }
        val uri =
            resolver.insert(collection, values)
                ?: throw IllegalStateException(context.getString(R.string.install_err_mediastore_insert))
        try {
            resolver.openOutputStream(uri)?.use { out ->
                source.inputStream().use { it.copyTo(out) }
            } ?: throw IllegalStateException(context.getString(R.string.install_err_open_stream))
        } catch (e: Exception) {
            resolver.delete(uri, null, null)
            throw e
        }
        values.clear()
        values.put(MediaStore.MediaColumns.IS_PENDING, 0)
        resolver.update(uri, values, null, null)
        return File(downloads, source.name)
    }
}
