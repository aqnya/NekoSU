// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.data.kernel

import me.nekosu.aqnya.core.Ncore
import me.nekosu.aqnya.proto.featureEntry

object KernelFeature {
    const val SELINUX_HIDE = 1
    const val UMOUNT = 2

    // 复用同一个 Regex，避免每行都构造一次。
    private val WHITESPACE = Regex("\\s+")

    data class Info(
        val id: Int,
        val name: String,
        val enabled: Boolean,
    )

    fun list(): List<Info> =
        runCatching { Ncore.featureList() }
            .getOrNull()
            ?.lineSequence()
            ?.mapNotNull { line ->
                val parts = line.trim().split(WHITESPACE)
                if (parts.size < 3) return@mapNotNull null
                val id = parts[0].toIntOrNull() ?: return@mapNotNull null
                Info(
                    id = id,
                    name = parts[1],
                    enabled = parts[2].toLongOrNull() != 0L,
                )
            }?.toList()
            .orEmpty()

    fun set(
        id: Int,
        enabled: Boolean,
    ): Boolean {
        val ret =
            runCatching {
                Ncore.featureSet(
                    featureEntry {
                        this.id = id
                        value = if (enabled) 1L else 0L
                    }.toByteArray(),
                )
            }.getOrNull() ?: return false
        return ret == 0
    }
}
