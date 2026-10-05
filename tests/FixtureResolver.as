// Integration fixture only. Production Bilibili parsing uses the fixed upstream script.
bool PlayitemCheck(const string &in url) { return url == "https://fixture.test/dual"; }
string PlayitemParse(const string &in url, dictionary &inout meta, array<dictionary> &inout choices) {
    string base = "file:///" + HostGetScriptFolder();
    base.replace("\\", "/");
    meta["title"] = "独立音视频 · 集成验证";
    dictionary video = {{"url", base + "video.mp4"}, {"va", "v"}, {"quality", "测试视频"}, {"qualityDetail", "测试视频详情"}, {"fps", 30}, {"format", "mp4, avc, 2Mbps"}, {"bitrateVal", 2000000}, {"itag", 1}};
    dictionary low = {{"url", base + "audio-low.m4a"}, {"va", "a"}, {"quality", "低码率音频"}, {"bitrateVal", 32000}, {"audioIsDefault", true}, {"itag", 2}};
    dictionary audio = {{"url", base + "audio.m4a"}, {"va", "a"}, {"quality", "高码率音频"}, {"bitrateVal", 128000}, {"audioIsDefault", false}, {"itag", 3}};
    choices.insertLast(low); choices.insertLast(audio); choices.insertLast(video);
    dictionary unavailable = {{"url", base + "missing-media.mp4"}, {"va", "v"}, {"quality", "不可用测试流"}, {"itag", 4}};
    choices.insertLast(unavailable);
    return base + "video.mp4";
}
