bool PlayitemCheck(const string &in url) { return url == "https://fixture.test/subtitles"; }
string PlayitemParse(const string &in url, dictionary &inout meta, array<dictionary> &inout choices) {
    string base="file:///"+HostGetScriptFolder();base.replace("\\","/");
    meta["title"]="Animated ASS fullscreen regression";
    string ass="[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Microsoft YaHei,36,&H00FFFFFF,&H0000FFFF,&H00101010,&H80000000,0,0,0,0,100,100,0,0,1,2,0,7,0,0,0,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for(int i=0;i<24;i++){
        int y=20+i*42;
        ass+="Dialogue: 0,0:00:00.00,0:01:00.00,Default,,0,0,0,,{\\move(1700,"+y+",-1000,"+y+",0,60000)} Animated fullscreen ASS / 弹幕同步交付验证 / 0123456789\n";
    }
    array<dictionary> tracks;
    dictionary danmaku={{"name","弹幕"},{"fileContent",ass}};
    dictionary main={{"name","Main caption"},{"fileContent","1\n00:00:00,000 --> 00:01:00,000\nPrimary caption across fullscreen\n"}};
    tracks.insertLast(danmaku);tracks.insertLast(main);
    meta["subtitle"]=tracks;
    return base+"muxed.mp4";
}
