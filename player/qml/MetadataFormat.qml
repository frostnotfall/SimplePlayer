pragma Singleton
import QtQuick

QtObject {
    function present(value) { return value !== undefined && value !== null && String(value).trim().length > 0 }
    function text(value) { return present(value) ? String(value) : "" }
    function duration(value) {
        if (!present(value)) return ""
        if (String(value).includes(":")) return String(value)
        const milliseconds = Number(value)
        if (!isFinite(milliseconds) || milliseconds <= 0) return ""
        const seconds = Math.floor(milliseconds / 1000), hours = Math.floor(seconds / 3600)
        return (hours ? hours + ":" : "") + String(Math.floor(seconds / 60) % 60).padStart(2,"0") + ":" + String(seconds % 60).padStart(2,"0")
    }
    function count(value) {
        if (!present(value)) return ""
        const number = Number(value)
        if (!isFinite(number) || number < 0) return String(value)
        if (number >= 100000000) return (number/100000000).toFixed(1).replace(/\.0$/,"") + "亿"
        if (number >= 10000) return (number/10000).toFixed(1).replace(/\.0$/,"") + "万"
        return String(value)
    }
    function stats(info) {
        const parts = []
        if (present(info.viewCount)) parts.push((info.live ? "观看 " : "播放 ") + count(info.viewCount))
        if (present(info.likeCount)) parts.push("赞 " + count(info.likeCount))
        if (present(info.dislikeCount)) parts.push("踩 " + count(info.dislikeCount))
        return parts.join(" · ")
    }
    function byline(info, compact) {
        const parts = []
        if (present(info.author)) parts.push(String(info.author))
        if (present(info.date)) parts.push(compact ? String(info.date).split(/[ T]/)[0] : String(info.date))
        return parts.join(" · ")
    }
    function summary(info) {
        return [byline(info,false), duration(info.duration), stats(info)].filter(s => s.length > 0).join(" · ")
    }
    function details(info) {
        const lines = []
        for (const field of [["title","标题"],["author","作者"],["date","日期"]])
            if (present(info[field[0]])) lines.push(field[1] + "：" + info[field[0]])
        const length = duration(info.duration)
        if (length) lines.push("时长：" + length)
        for (const field of [["viewCount",info.live ? "观看" : "播放"],["likeCount","点赞"],["dislikeCount","踩"]])
            if (present(info[field[0]])) lines.push(field[1] + "：" + info[field[0]])
        const url = text(info.webUrl || info.url)
        if (url) lines.push("原网页：" + url)
        if (present(info.content)) lines.push("\n简介\n" + info.content)
        return lines.join("\n")
    }
    function preview(info) {
        const full = details(info)
        return full.length > 800 ? full.slice(0,800) + "\n…点击 ⋯ 查看完整信息" : full
    }
}
