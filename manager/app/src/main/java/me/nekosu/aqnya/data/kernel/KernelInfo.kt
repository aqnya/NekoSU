package me.nekosu.aqnya.data.kernel

import me.nekosu.aqnya.core.Ncore

object KernelInfo {
    private val ANDROID_TAG = Regex("-android(\\d+)")

    fun version(): String =
        normalize(requireNotNull(Ncore.kernelVersion()) { "ncore kernel version unavailable" })

    fun isGki(): Boolean = Ncore.isGki()

    fun currentKmi(): String = "android${androidVersion()}-${version()}"

    fun kmiOf(koName: String): String = koName.removeSuffix("_nksu.ko")

    fun matchKo(
        candidates: List<String>,
        kmi: String,
    ): String? {
        if (candidates.isEmpty()) return null
        candidates.firstOrNull { kmiOf(it) == kmi }?.let { return it }
        val v = version()
        candidates.firstOrNull { kmiOf(it).endsWith("-$v") }?.let { return it }
        return candidates.first()
    }

    private fun normalize(raw: String): String {
        val trimmed = raw.trim()
        val parts = trimmed.split('.', limit = 3)
        val major = parts.getOrNull(0)?.takeWhile { it.isDigit() }?.toIntOrNull() ?: return trimmed
        val minor = parts.getOrNull(1)?.takeWhile { it.isDigit() }?.toIntOrNull() ?: return trimmed
        return "$major.$minor"
    }

    /** KMI 中的 Android 版本号，从 uname 原始 release 的 `-androidNN` 标记解析。 */
    private fun androidVersion(): String {
        val release = requireNotNull(Ncore.kernelRelease()) { "ncore kernel release unavailable" }
        return ANDROID_TAG.find(release)?.groupValues?.getOrNull(1)
            ?: error("cannot resolve android version from kernel release: $release")
    }
}
