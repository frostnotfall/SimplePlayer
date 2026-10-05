// Metadata fixture deliberately puts danmaku first and invalid URLs alongside
// inline content, proving that fileContent is used without a network fetch.
bool PlayitemCheck(const string &in url) { return url == "https://fixture.test/subtitles"; }
string PlayitemParse(const string &in url, dictionary &inout meta, array<dictionary> &inout choices) {
    HostPrintUTF8("Subtitle fixture: PlayitemParse");
    string base="file:///"+HostGetScriptFolder();base.replace("\\","/");
    meta["title"]="字幕元数据验证";
    array<dictionary> tracks;
    dictionary danmaku={{"name","弹幕"},{"url","https://invalid.test/unused"},{"fileContent","1\n00:00:00,000 --> 00:01:00,000\nDanmaku from fileContent\n"}};
    dictionary main={{"name","简体中文 · 主字幕"},{"langTranslated","Wrong label"},{"fileContent","1\n00:00:00,000 --> 00:01:00,000\nPrimary from fileContent\n"}};
    dictionary alternate={{"name","English alternative subtitle with a long name"},{"fileContent","1\n00:00:00,000 --> 00:01:00,000\nAlternate from fileContent\n"}};
    tracks.insertLast(danmaku);tracks.insertLast(main);tracks.insertLast(alternate);meta["subtitle"]=tracks;
    return base+"muxed.mp4";
}
