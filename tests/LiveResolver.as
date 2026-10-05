bool PlayitemCheck(const string &in url) { return url == "https://fixture.test/live"; }
string PlayitemParse(const string &in url, dictionary &inout meta, array<dictionary> &inout choices) {
    meta["title"]="直播回看验证";meta["live"]=true;
    string source="http://127.0.0.1:18765/live.flv";
    dictionary stream={{"url",source},{"va","va"},{"quality","直播"},{"qualityDetail","H.264 + AAC · 缓存验证"},{"itag",1}};
    choices.insertLast(stream);return source;
}
