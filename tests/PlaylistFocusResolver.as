bool PlayitemCheck(const string &in url) { return url.find("https://fixture.test/item/")==0; }
bool PlaylistCheck(const string &in url) { return PlayitemCheck(url); }
array<dictionary> PlaylistParse(const string &in url) {
    HostPrintUTF8("Focus fixture: PlaylistParse");
    array<dictionary> list;
    int count=url=="https://fixture.test/item/empty"?0:url=="https://fixture.test/item/solo"?1:url=="https://fixture.test/item/short"?3:60;
    for(int i=0;i<count;i++){
        string page=count==1?url:"https://fixture.test/item/"+i;
        dictionary item={{"url",page},{"title","播放列表定位验证 · "+i},{"current",page==url?"1":"0"},{"author","@测试作者"},{"date","2026-10-05 12:30:00"},{"duration",int64(93123)},{"viewCount",int64(32001)},{"likeCount","864"},{"dislikeCount",int64(0)},{"webUrl",page},{"content","完整列表简介\n<b>普通文字</b>"}};list.insertLast(item);
    }
    return list;
}
string PlayitemParse(const string &in url, dictionary &inout meta, array<dictionary> &inout choices) {
    HostPrintUTF8("Focus fixture: PlayitemParse");meta["title"]="播放列表定位验证";
    if(url!="https://fixture.test/item/empty"){
        meta["author"]="@测试作者";meta["date"]="2026-10-05 12:30:00";meta["duration"]="93123";
        meta["viewCount"]=int64(32001);meta["likeCount"]="864";meta["dislikeCount"]=int64(0);
        meta["webUrl"]=url;meta["content"]="完整媒体简介\n<b>普通文字</b>";
    }
    string base="file:///"+HostGetScriptFolder();base.replace("\\","/");return base+"muxed.mp4";
}
